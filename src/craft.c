/*
 * Crafting System
 * 
 * From d20MUD
 * Ported and re-written by Zusuk
 * 
 * craft.h has most of the header info
 */

/*
 * Hard metal -> Mining
 * Leather -> Hunting
 * Wood -> Foresting
 * Cloth -> Knitting
 * Crystals / Essences -> Chemistry
 */

#include "conf.h"
#include "sysdep.h"

#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "spells.h"
#include "interpreter.h"
#include "constants.h"
#include "handler.h"
#include "db.h"
#include "craft.h"
#include "spells.h"
#include "mud_event.h"
#include "modify.h" // for parse_at()
#include "treasure.h"

/* global variables */
int mining_nodes = 0;
int farming_nodes = 0;
int hunting_nodes = 0;
int foresting_nodes = 0;


/***********************************/
/* crafting local utility functions*/
/***********************************/

/* bare in mind any CAP that has been established for num/size of dice */
int weapon_damage[MAX_WEAPON_DAMAGE + 1][2] = {
  /* damage  num_dice  siz_dice */
  /*     0 */
  { 0, 0,},
  /*     1 */
  { 1, 1,},
  /*     2 */
  { 1, 2,},
  /*     3 */
  { 1, 2,},
  /*     4 */
  { 1, 4,},
  /*     5 */
  { 2, 2,},
  /*     6 */
  { 1, 6,},
  /*     7 */
  { 1, 6,},
  /*     8 */
  { 1, 8,},
  /*     9 */
  { 2, 4,},
  /*     10*/
  { 1, 10,},
  /*     11*/
  { 1, 10,},
  /*     12*/
  { 1, 12,},
  /*     13*/
  { 1, 12 ,},
  /*     14*/
  { 2, 6,},
  /*     15*/
  { 2, 6,},
  /*     16*/
  { 2, 8,},
  /*     17*/
  { 2, 8,},
  /*     18*/
  { 2, 10,},
  /*     19*/
  { 2, 10,},
  /*     20*/
  { 2, 10,},
  /*     21*/
  { 2, 10,},
  /*     22*/
  { 2, 12,},
  /*     23*/
  { 2, 12,},
  /*     24*/
  { 2, 12,}
};

/* the primary use of this function is to modify a weapons damage
 * when resizing it...
 *   weapon:  object, needs to be a weapon
 *   scaling:  integer, negative or positive indicating how many
 *             size classes the weapon is shifting
 * 
 * returns TRUE if successful, FALSE if failed
 */
bool scale_damage(struct obj_data *weapon, int scaling) {
  int max_damage = 0; // number-of-dice * size-of-dice of weapon
  int new_max_damage = 0; // new weapon damage
  int num_of_dice = 0; // number-of-dice rolled for weapon dam
  int size_of_dice = 0; // size-of-dice rolled for weapon dam
  int size = -1; // starting size of weapon
  int new_size = -1; // new size of weapon

  /* no scaling to be done */
  if (!scaling)
    return FALSE;

  if (!weapon)
    return FALSE;

  /* this only works for weapons */
  if (GET_OBJ_TYPE(weapon) != ITEM_WEAPON)
    return FALSE;

  /* assigned for ease-of-use */
  num_of_dice = GET_OBJ_VAL(weapon, 1); // how many dice are we rolling?
  size_of_dice = GET_OBJ_VAL(weapon, 2); // how big is the current dice roll?
  size = GET_OBJ_SIZE(weapon); // what is current size of weapon?
  new_size = size + scaling; // what size will weapon be now?
  max_damage = num_of_dice * size_of_dice; // weapons max damage
  new_max_damage = max_damage + (scaling * WEAPON_RESIZE_INC); // new max dam

  /* make sure the weapon is not becoming too big or too small */
  if (new_size >= NUM_SIZES ||
          new_size <= SIZE_RESERVED)
    return FALSE;

  /* more cap-checks, min/max damage */
  if (new_max_damage > MAX_WEAPON_DAMAGE ||
          new_max_damage < MIN_WEAPON_DAMAGE)
    return FALSE;

  /* OK, passed all our checks, modify weapon damage */
  GET_OBJ_VAL(weapon, 1) = weapon_damage[new_max_damage][0];
  GET_OBJ_VAL(weapon, 2) = weapon_damage[new_max_damage][1];

  return TRUE;
}

/* this function will switch the material of an item based on the
   conversion crafting system
 */
int convert_material(int material) {
  switch (material) {
    case MATERIAL_IRON:
      return MATERIAL_COLD_IRON;
    case MATERIAL_SILVER:
      return MATERIAL_ALCHEMAL_SILVER;
    default:
      return material;
  }

  return material;
}

/* simple function to reset craft data */
void reset_craft(struct char_data *ch) {
  /* initialize values */
  GET_CRAFTING_TYPE(ch) = 0; // SCMD_ of craft
  GET_CRAFTING_TICKS(ch) = 0;
  GET_CRAFTING_OBJ(ch) = NULL;
  GET_CRAFTING_REPEAT(ch) = 0;
}

/* simple function to reset auto craft data */
void reset_acraft(struct char_data *ch) {
  /* initialize values */
  GET_AUTOCQUEST_VNUM(ch) = 0;
  GET_AUTOCQUEST_MAKENUM(ch) = 0;
  GET_AUTOCQUEST_QP(ch) = 0;
  GET_AUTOCQUEST_EXP(ch) = 0;
  GET_AUTOCQUEST_GOLD(ch) = 0;
  GET_AUTOCQUEST_MATERIAL(ch) = 0;
  free(GET_AUTOCQUEST_DESC(ch)); // I have no idea if this is actually needed
  GET_AUTOCQUEST_DESC(ch) = strdup("nothing");
}

/* compartmentalized auto-quest crafting reporting since its done
   a few times in the code
 */
void cquest_report(struct char_data *ch) {
  if (GET_AUTOCQUEST_VNUM(ch)) {
    if (GET_AUTOCQUEST_MAKENUM(ch) <= 0)
      send_to_char(ch, "You have completed your supply order for %s.\r\n",
            GET_AUTOCQUEST_DESC(ch));
    else
      send_to_char(ch, "You have not yet completed your supply order "
            "for %s.\r\n"
            "You still need to make %d more.\r\n",
            GET_AUTOCQUEST_DESC(ch), GET_AUTOCQUEST_MAKENUM(ch));
    send_to_char(ch, "Once completed/turned-in you will receive the"
            " following:\r\n"
            "You will receive %d reputation points.\r\n"
            "%d gold will be awarded to you.\r\n"
            "You will receive %d experience points.\r\n"
            "(type 'supplyorder complete' at the supply office)\r\n",
            GET_AUTOCQUEST_QP(ch), GET_AUTOCQUEST_GOLD(ch),
            GET_AUTOCQUEST_EXP(ch));
  } else
    send_to_char(ch, "Type 'supplyorder new' for a new supply order, "
          "'supplyorder complete' to finish your supply "
          "order and receive your reward or 'supplyorder quit' "
          "to quit your current supply order.\r\n");
}

/* this function determines the factor of bonus for crystal_value/level */
int crystal_bonus(struct obj_data *crystal, int mod) {
  int bonus = mod + (GET_OBJ_LEVEL(crystal) / BONUS_FACTOR);

  switch (GET_OBJ_VAL(crystal, 0)) {

    case APPLY_CHAR_WEIGHT:
    case APPLY_CHAR_HEIGHT:
      bonus *= 10;
      break;

    case APPLY_HIT:
      bonus *= 12;
      break;

    case APPLY_MOVE:
      bonus *= 24;
      break;
      
    case APPLY_AC:
      bonus *= -5;
      break;
      
    case APPLY_HITROLL:
    case APPLY_DAMROLL:
      bonus += 2;
      break;

    default: // default - unmodified
      break;
  }

  return bonus;
}

/*
 * Our current list of materials distributed in this manner:
 * METALS (hard)
 * bronze
 * iron
 * steel
 * cold iron
 * alchemal silver
 * mithril
 * adamantine
 * METALS (precious)
 * copper
 * brass
 * silver
 * gold
 * platinum
 * LEATHERS
 * leather
 * dragonhide
 * WOODS
 * wood
 * darkwood
 * CLOTH
 * burlap
 * hemp
 * cotton
 * wool
 * velvet
 * satin
 * silk
 */

/* this function returns an appropriate keyword(s) based on material */
char *node_keywords(int material) {

  switch (material) {
    case MATERIAL_STEEL:
      return strdup("vein iron ore");
    case MATERIAL_COLD_IRON:
      return strdup("vein cold iron ore");
    case MATERIAL_MITHRIL:
      return strdup("vein mithril ore");
    case MATERIAL_ADAMANTINE:
      return strdup("vein adamantine ore");
    case MATERIAL_SILVER:
      return strdup("vein copper silver ore");
    case MATERIAL_GOLD:
      return strdup("vein gold platinum ore");
    case MATERIAL_WOOD:
      return strdup("tree fallen");
    case MATERIAL_DARKWOOD:
      return strdup("tree darkwood fallen");
    case MATERIAL_LEATHER:
      return strdup("game freshly killed");
    case MATERIAL_DRAGONHIDE:
      return strdup("wyvern freshly killed");
    case MATERIAL_HEMP:
      return strdup("hemp plants");
    case MATERIAL_COTTON:
      return strdup("cotton plants");
    case MATERIAL_WOOL:
      return strdup("cache of wool");
    case MATERIAL_VELVET:
      return strdup("cache of cloth");
    case MATERIAL_SATIN:
      return strdup("cache of satin");
    case MATERIAL_SILK:
      return strdup("silkworms");
  }
  return strdup("node harvesting");
}

/* this function returns an appropriate short-desc based on material */
char *node_sdesc(int material) {
  switch (material) {
    case MATERIAL_STEEL:
      return strdup("a vein of iron ore");
    case MATERIAL_COLD_IRON:
      return strdup("a vein of cold iron ore");
    case MATERIAL_MITHRIL:
      return strdup("a vein of mithril ore");
    case MATERIAL_ADAMANTINE:
      return strdup("a vein of adamantine ore");
    case MATERIAL_SILVER:
      return strdup("a vein of copper and silver ore");
    case MATERIAL_GOLD:
      return strdup("a vein of gold and platinum ore");
    case MATERIAL_WOOD:
      return strdup("a fallen tree");
    case MATERIAL_DARKWOOD:
      return strdup("a fallen darkwood tree");
    case MATERIAL_LEATHER:
      return strdup("the corpse of some freshly killed game");
    case MATERIAL_DRAGONHIDE:
      return strdup("the corpse of a freshly killed baby wyvern");
    case MATERIAL_HEMP:
      return strdup("a patch of hemp plants");
    case MATERIAL_COTTON:
      return strdup("a patch of cotton plants");
    case MATERIAL_WOOL:
      return strdup("an abandoned cache of cloth");
    case MATERIAL_VELVET:
      return strdup("an abandoned cache of cloth");
    case MATERIAL_SATIN:
      return strdup("an abandoned cache of cloth");
    case MATERIAL_SILK:
      return strdup("a large family of silkworms");
  }
  return strdup("a harvesting node");
}

/* this function returns an appropriate desc based on material */
char *node_desc(int material) {
  switch (material) {
    case MATERIAL_STEEL:
      return strdup("A vein of iron ore is here (\tYharvest\tn).");
    case MATERIAL_COLD_IRON:
      return strdup("A vein of cold iron ore is here (\tYharvest\tn).");
    case MATERIAL_MITHRIL:
      return strdup("A vein of mithril ore is here (\tYharvest\tn).");
    case MATERIAL_ADAMANTINE:
      return strdup("A vein of adamantine ore is here (\tYharvest\tn).");
    case MATERIAL_SILVER:
      return strdup("A vein of copper and silver ore is here (\tYharvest\tn).");
    case MATERIAL_GOLD:
      return strdup("A vein of gold and platinum ore is here (\tYharvest\tn).");
    case MATERIAL_WOOD:
      return strdup("A fallen tree is here (\tYharvest\tn).");
    case MATERIAL_DARKWOOD:
      return strdup("A fallen darkwood tree is here (\tYharvest\tn).");
    case MATERIAL_LEATHER:
      return strdup("The corpse of some freshly killed game is here (\tYharvest\tn).");
    case MATERIAL_DRAGONHIDE:
      return strdup("The corpse of a freshly killed baby wyvern is here (\tYharvest\tn).");
    case MATERIAL_HEMP:
      return strdup("A patch of hemp plants is here (\tYharvest\tn).");
    case MATERIAL_COTTON:
      return strdup("A patch of cotton plants is here (\tYharvest\tn).");
    case MATERIAL_WOOL:
      return strdup("An abandoned cache of cloth is here (\tYharvest\tn).");
    case MATERIAL_VELVET:
      return strdup("An abandoned cache of cloth is here (\tYharvest\tn).");
    case MATERIAL_SATIN:
      return strdup("An abandoned cache of cloth is here (\tYharvest\tn).");
    case MATERIAL_SILK:
      return strdup("A large family of silkworms is here (\tYharvest\tn).");
  }
  return strdup("A harvesting node is here.  Please inform an imm, this is an error.");
}

/* a function to try and make an intelligent(?) decision
   about what material a harvesting node should be */
int random_node_material(int allowed) {
  int rand = 0;

  if (mining_nodes >= (allowed * 2)  && foresting_nodes >= allowed &&
          farming_nodes >= allowed && hunting_nodes >= allowed)
    return MATERIAL_STEEL;

  rand = rand_number(1, 100);
  /* 34% mining, blacksmithing or goldsmithing */
  if (rand <= 34) {

    // mining
    if (mining_nodes >= (allowed * 2))
      return random_node_material(allowed);

    rand = rand_number(1, 100);
    /* 80% chance of blacksmithing (iron/steel/cold-iron/mithril/adamantine */
    if (rand <= 80) {

      rand = rand_number(1, 100);
      // blacksmithing

      if (rand <= 85)
        return MATERIAL_STEEL;
      else if (rand <= 93)
        return MATERIAL_COLD_IRON;
      else if (rand <= 98)
        return MATERIAL_MITHRIL;
      else
        return MATERIAL_ADAMANTINE;

      /* 20% of goldsmithing (silver/gold) */
    } else {

      // goldsmithing

      if (rand_number(1, 100) <= 90)
        return MATERIAL_SILVER;
      else
        return MATERIAL_GOLD;
    }

    /* 33% farming (hemp/cotton/wool/velvet/satin/silk) */
  } else if (rand <= 67) {

    rand = rand_number(1, 100);
    // farming

    if (farming_nodes >= allowed)
      return random_node_material(allowed);

    if (rand <= 30)
      return MATERIAL_HEMP;
    else if (rand <= 70)
      return MATERIAL_COTTON;
    else if (rand <= 85)
      return MATERIAL_WOOL;
    else if (rand <= 96)
      return MATERIAL_VELVET;
    else if (rand <= 98)
      return MATERIAL_SATIN;
    else
      return MATERIAL_SILK;

    /* 33% foresting (leather/dragonhide/wood/darkwood) */
  } else {
    // foresting

    if (foresting_nodes >= allowed)
      return random_node_material(allowed);

    rand = dice(1, 100);

    if (rand <= 50) {

      rand = dice(1, 100);
      if (rand <= 99)
        return MATERIAL_LEATHER;
      else
        return MATERIAL_DRAGONHIDE;
    } else {

      rand = dice(1, 100);
      if (rand <= 99)
        return MATERIAL_WOOD;
      else
        return MATERIAL_DARKWOOD;
    }
  }

  /* default steel */
  return MATERIAL_STEEL;
}

/* this is called in db.c on boot-up
   harvesting nodes are placed by this function randomly(?)
   throughout the world
 */
void reset_harvesting_rooms(void) {
  int cnt = 0;
  int num_rooms = 0;
  int nodes_allowed = 0;
  struct obj_data *obj = NULL;

  for (cnt = 0; cnt <= top_of_world; cnt++) {
    if (world[cnt].sector_type == SECT_CITY)
      continue;
    num_rooms++;
  }

  nodes_allowed = num_rooms / NODE_CAP_FACTOR;

  if (mining_nodes >= (nodes_allowed * 2) && foresting_nodes >= nodes_allowed &&
          farming_nodes >= nodes_allowed && hunting_nodes >= nodes_allowed)
    return;

  for (cnt = 0; cnt <= top_of_world; cnt++) {
    if (ROOM_FLAGGED(cnt, ROOM_HOUSE))
      continue;
    if (world[cnt].sector_type == SECT_CITY)
      continue;
    if (dice(1, 33) == 1) {
      obj = read_object(HARVESTING_NODE, VIRTUAL);
      if (!obj)
        continue;
      GET_OBJ_MATERIAL(obj) = random_node_material(nodes_allowed);
      switch (GET_OBJ_MATERIAL(obj)) {
        case MATERIAL_STEEL:
        case MATERIAL_COLD_IRON:
        case MATERIAL_MITHRIL:
        case MATERIAL_ADAMANTINE:
        case MATERIAL_SILVER:
        case MATERIAL_GOLD:
          if (mining_nodes >= nodes_allowed) {
            obj_to_room(obj, cnt);
            extract_obj(obj);
            continue;
          } else
            mining_nodes++;
          break;
        case MATERIAL_WOOD:
        case MATERIAL_DARKWOOD:
        case MATERIAL_LEATHER:
        case MATERIAL_DRAGONHIDE:
          if (foresting_nodes >= nodes_allowed) {
            obj_to_room(obj, cnt);
            extract_obj(obj);
            continue;
          } else
            foresting_nodes++;
        case MATERIAL_HEMP:
        case MATERIAL_COTTON:
        case MATERIAL_WOOL:
        case MATERIAL_VELVET:
        case MATERIAL_SATIN:
        case MATERIAL_SILK:
          if (farming_nodes >= nodes_allowed) {
            obj_to_room(obj, cnt);
            extract_obj(obj);
            continue;
          } else
            farming_nodes++;
          break;
        default:
          obj_to_room(obj, cnt);
          extract_obj(obj);
          continue;
          break;
      }
      GET_OBJ_VAL(obj, 0) = dice(2, 3);

      /* strdup()ed in node_foo() functions */
      obj->name = node_keywords(GET_OBJ_MATERIAL(obj));
      obj->short_description = node_sdesc(GET_OBJ_MATERIAL(obj));
      obj->description = node_desc(GET_OBJ_MATERIAL(obj));
      obj_to_room(obj, cnt);
    }
  }
}




/*************************/
/* start primary engines */
/*************************/



// combine crystals to make them stronger

int augment(struct obj_data *kit, struct char_data *ch) {
  struct obj_data *obj = NULL, *crystal_one = NULL, *crystal_two = NULL;
  int num_objs = 0, cost = 0, bonus = 0, bonus2 = 0;
  int skill_type = SKILL_CHEMISTRY; // change this to change the skill used
  char buf[MAX_INPUT_LENGTH];

  // Cycle through contents and categorize
  for (obj = kit->contains; obj != NULL; obj = obj->next_content) {
    if (obj) {
      num_objs++;
      if (num_objs > 2) {
        send_to_char(ch, "Make sure only two items are in the kit.\r\n");
        return 1;
      }
      if (GET_OBJ_TYPE(obj) == ITEM_CRYSTAL && !crystal_one) {
        crystal_one = obj;
      } else if (GET_OBJ_TYPE(obj) == ITEM_CRYSTAL && !crystal_two) {
        crystal_two = obj;
      }
    }
  }

  if (num_objs > 2) {
    send_to_char(ch, "Make sure only two items are in the kit.\r\n");
    return 1;
  }
  if (!crystal_one || !crystal_two) {
    send_to_char(ch, "You need two crystals to augment.\r\n");
    return 1;
  }
  if (apply_types[crystal_one->affected[0].location] !=
          apply_types[crystal_two->affected[0].location]) {
    send_to_char(ch, "The crystal 'apply type' needs to be the same to"
            " augment.\r\n");
    return 1;
  }
  // if the crystals aren't at least 2nd level, you are going to create junk
  if (GET_OBJ_LEVEL(crystal_one) < 2 || GET_OBJ_LEVEL(crystal_two) < 2) {
    send_to_char(ch, "These crystals are too weak to augment together.\r\n");
    return 1;
  }

  // new level of crystal, with cap
  /* first determine the higher/lower bonus */
  if (GET_OBJ_LEVEL(crystal_one) >= GET_OBJ_LEVEL(crystal_two)) {
    bonus = GET_OBJ_LEVEL(crystal_one);
    bonus2 = GET_OBJ_LEVEL(crystal_two);
  } else {
    bonus = GET_OBJ_LEVEL(crystal_two);
    bonus2 = GET_OBJ_LEVEL(crystal_one);
  }
  /* new level is half of lower level crystal + higher level crystal */
  bonus = bonus + (bonus2 / 2);

  if (bonus > (LVL_IMMORT - 1)) { // cap
    send_to_char(ch, "This augmentation process would create a crystal that is unstable!\r\n");
    return 1;
  }

  if (bonus > (GET_SKILL(ch, skill_type) / 3)) { // high enough skill?
    send_to_char(ch, "The crystal level is %d but your %s skill is "
            "only capable of creating level %d crystals.\r\n",
            bonus, spell_info[skill_type].name,
            (GET_SKILL(ch, skill_type) / 3));
    return 1;
  }

  cost = bonus * bonus * 1000 / 3; // expense for augmenting
  if (GET_GOLD(ch) < cost) {
    send_to_char(ch, "You need %d coins on hand for supplies to augment this "
            "crystal.\r\n", cost);
    return 1;
  }

  // crystal_one is converted to the new crystal
  GET_OBJ_LEVEL(crystal_one) = bonus;
  GET_OBJ_COST(crystal_one) = GET_OBJ_COST(crystal_one) +
          GET_OBJ_COST(crystal_two);

  // exp bonus for crafting ticks
  GET_CRAFTING_BONUS(ch) = 10 + MIN(60, GET_OBJ_LEVEL(crystal_one));

  // new name
  sprintf(buf, "\twa crystal of\ty %s\tw max level\ty %d\tn",
          apply_types[crystal_one->affected[0].location],
          GET_OBJ_LEVEL(crystal_one));
  crystal_one->name = strdup(buf);
  crystal_one->short_description = strdup(buf);
  sprintf(buf, "\twA crystal of\ty %s\tw max level\ty %d\tw lies here.\tn",
          apply_types[crystal_one->affected[0].location],
          GET_OBJ_LEVEL(crystal_one));
  crystal_one->description = strdup(buf);

  send_to_char(ch, "It cost you %d coins in supplies to "
          "augment this crytsal.\r\n", cost);
  GET_GOLD(ch) -= cost;

  GET_CRAFTING_TYPE(ch) = SCMD_AUGMENT;
  GET_CRAFTING_TICKS(ch) = 5; // add code here to modify speed of crafting
  GET_CRAFTING_TICKS(ch) -= MIN(4, (GET_SKILL(ch, SKILL_FAST_CRAFTER) / 25));
  GET_CRAFTING_OBJ(ch) = crystal_one;
  send_to_char(ch, "You begin to augment %s.\r\n",
          crystal_one->short_description);
  act("$n begins to augment $p.", FALSE, ch, crystal_one, 0, TO_ROOM);

  // get rid of the items in the kit
  obj_from_obj(crystal_one);
  extract_obj(crystal_two);

  obj_to_char(crystal_one, ch);
  NEW_EVENT(eCRAFTING, ch, NULL, 1 * PASSES_PER_SEC);

  return 1;
}


// convert one material into another
// requires multiples of exactly 10 of same mat to do the converstion

/*  !! still under construction - zusuk !! */
int convert(struct obj_data *kit, struct char_data *ch) {
  int cost = 500; /* flat cost */
  int num_mats = 0, material = -1, obj_vnum = 0;
  struct obj_data *new_mat = NULL, *obj = NULL;

  /* Cycle through contents and categorize */
  for (obj = kit->contains; obj != NULL; obj = obj->next_content) {
    if (obj) {
      if (GET_OBJ_TYPE(obj) != ITEM_MATERIAL) {
        send_to_char(ch, "Only materials should be inside the kit in"
                " order to convert.\r\n");
        return 1;
      } else if (GET_OBJ_TYPE(obj) == ITEM_MATERIAL) {
        if (GET_OBJ_VAL(obj, 0) >= 2) {
          send_to_char(ch, "%s is a bundled item, which must first be "
                  "unbundled before you can use it to craft.\r\n",
                  obj->short_description);
          return 1;
        }
        if (material == -1) { /* first item */
          new_mat = obj;
          material = GET_OBJ_MATERIAL(obj);
        } else if (GET_OBJ_MATERIAL(obj) != material) {
          send_to_char(ch, "You have mixed materials inside the kit, "
                  "put only the exact same materials for "
                  "conversion.\r\n");
          return 1;
        }
        num_mats++; /* we found matching material */
        obj_vnum = GET_OBJ_VNUM(obj);
      }
    }
  }

  if (num_mats) {
    if (num_mats % 10) {
      send_to_char(ch, "You must convert materials in multiple "
              "of 10 units exactly.\r\n");
      return 1;
    }
  } else {
    send_to_char(ch, "There is no material in the kit.\r\n");
    return 1;
  }

  if ((num_mats = convert_material(material)))
    send_to_char(ch, "You are converting the material to:  %s\r\n",
          material_name[num_mats]);
  else {
    send_to_char(ch, "You do not have a valid material in the crafting "
            "kit.\r\n");
    return 1;
  }

  if (GET_GOLD(ch) < cost) {
    send_to_char(ch, "You need %d gold on hand for supplies to covert these "
            "materials.\r\n", cost);
    return 1;
  }
  send_to_char(ch, "It cost you %d gold in supplies to convert this "
          "item.\r\n", cost);
  GET_GOLD(ch) -= cost;
  // new name
  char buf[MAX_INPUT_LENGTH];
  sprintf(buf, "\tca portion of %s material\tn",
          material_name[num_mats]);
  new_mat->name = strdup(buf);
  new_mat->short_description = strdup(buf);
  sprintf(buf, "\tcA portion of %s material lies here.\tn",
          material_name[num_mats]);
  new_mat->description = strdup(buf);
  act("$n begins a conversion of materials into $p.", FALSE, ch,
          new_mat, 0, TO_ROOM);

  GET_CRAFTING_BONUS(ch) = 10 + MIN(60, GET_OBJ_LEVEL(new_mat));
  GET_CRAFTING_TYPE(ch) = SCMD_CONVERT;
  GET_CRAFTING_TICKS(ch) = 11; // adding time-takes here
  GET_CRAFTING_TICKS(ch) -= MIN(10, (GET_SKILL(ch, SKILL_FAST_CRAFTER) / 10));
  GET_CRAFTING_OBJ(ch) = new_mat;
  GET_CRAFTING_REPEAT(ch) = MAX(0, (num_mats / 10) + 1);

  obj_from_obj(new_mat);

  obj_vnum = GET_OBJ_VNUM(kit);
  obj_from_char(kit);
  extract_obj(kit);
  kit = read_object(obj_vnum, VIRTUAL);

  obj_to_char(kit, ch);

  obj_to_char(new_mat, ch);
  NEW_EVENT(eCRAFTING, ch, NULL, 1 * PASSES_PER_SEC);

  return 1;
}

/* rename an object */
int restring(char *argument, struct obj_data *kit, struct char_data *ch) {
  int num_objs = 0, cost;
  struct obj_data *obj = NULL;
  char buf[MAX_INPUT_LENGTH];

  /* Cycle through contents */
  /* restring requires just one item be inside the kit */
  for (obj = kit->contains; obj != NULL; obj = obj->next_content) {
    num_objs++;
    break;
  }

  if (num_objs > 1) {
    send_to_char(ch, "Only one item should be inside the kit.\r\n");
    return 1;
  }

  if (obj->ex_description) {
    send_to_char(ch, "You cannot restring items with extra descriptions.\r\n");
    return 1;
    send_to_char(ch, "You cannot restring items with extra descriptions.\r\n");
    return 1;
  }

  if (GET_OBJ_TYPE(obj) == ITEM_CONTAINER) {
    if (obj->contains) {
      send_to_char(ch, "You cannot restring bags that have items in them.\r\n");
      return 1;
    }
  }

  if (GET_OBJ_MATERIAL(obj)) {
    if (!strstr(argument, material_name[GET_OBJ_MATERIAL(obj)])) {
      send_to_char(ch, "You must include the material name, '%s', in the object "
              "description somewhere.\r\n",
              material_name[GET_OBJ_MATERIAL(obj)]);
      return 1;
    }
  }

  cost = GET_OBJ_COST(obj) + GET_OBJ_LEVEL(obj);
  if (GET_GOLD(ch) < cost) {
    send_to_char(ch, "You need %d gold on hand for supplies to restring"
            " this item.\r\n", cost);
    return 1;
  }

  /* you need to parse the @ sign */
  parse_at(argument);

  /* success!! */
  obj->name = strdup(argument);
  obj->short_description = strdup(argument);
  sprintf(buf, "%s lies here.", CAP(argument));
  obj->description = strdup(buf);
  GET_CRAFTING_TYPE(ch) = SCMD_RESTRING;
  GET_CRAFTING_TICKS(ch) = 5; // here you'd add tick calculator
  GET_CRAFTING_TICKS(ch) -= MIN(4, (GET_SKILL(ch, SKILL_FAST_CRAFTER) / 25));
  GET_CRAFTING_OBJ(ch) = obj;

  send_to_char(ch, "It cost you %d gold in supplies to create this item.\r\n",
          cost);
  GET_GOLD(ch) -= cost;
  send_to_char(ch, "You put the item into the crafting kit and wait for it "
          "to transform into %s.\r\n", obj->short_description);

  obj_from_obj(obj);

  obj_to_char(obj, ch);
  NEW_EVENT(eCRAFTING, ch, NULL, 1 * PASSES_PER_SEC);

  return 1;
}

/* autocraft - crafting quest command */
int autocraft(struct obj_data *kit, struct char_data *ch) {
  int material, obj_vnum, num_mats = 0, material_level = 1;
  struct obj_data *obj = NULL;

  if (!GET_AUTOCQUEST_MATERIAL(ch)) {
    send_to_char(ch, "You do not have a supply order active right now. "
            "(supplyorder new)\r\n");
    return 1;
  }
  if (!GET_AUTOCQUEST_MAKENUM(ch)) {
    send_to_char(ch, "You have completed your supply order, "
            "go turn it in (type 'supplyorder complete' in a supplyorder office).\r\n");
    return 1;
  }

  material = GET_AUTOCQUEST_MATERIAL(ch);

  /* Cycle through contents and categorize */
  for (obj = kit->contains; obj != NULL; obj = obj->next_content) {
    if (obj) {
      if (GET_OBJ_TYPE(obj) != ITEM_MATERIAL) {
        send_to_char(ch, "Only materials should be inside the kit in"
                " order to complete a supplyorder.\r\n");
        return 1;
      } else if (GET_OBJ_TYPE(obj) == ITEM_MATERIAL) {
        if (GET_OBJ_VAL(obj, 0) >= 2) {
          send_to_char(ch, "%s is a bundled item, which must first be "
                  "unbundled before you can use it to craft.\r\n",
                  obj->short_description);
          return 1;
        }
        if (GET_OBJ_MATERIAL(obj) != material) {
          send_to_char(ch, "You need %s to complete this supplyorder.\r\n",
                  material_name[GET_AUTOCQUEST_MATERIAL(ch)]);
          return 1;
        }
        material_level = GET_OBJ_LEVEL(obj); // material level affects gold
        obj_vnum = GET_OBJ_VNUM(obj);
        num_mats++; /* we found matching material */
        if (num_mats > SUPPLYORDER_MATS) {
          send_to_char(ch, "You have too much materials in the kit, put "
                  "exactly %d for the supplyorder.\r\n",
                  SUPPLYORDER_MATS);
          return 1;
        }
      } else { /* must be an essence */
        send_to_char(ch, "Essence items will not work for supplyorders!\r\n");
        return 1;
      }
    }
  }

  if (num_mats < SUPPLYORDER_MATS) {
    send_to_char(ch, "You have %d material units in the kit, you will need "
            "%d more units to complete the supplyorder.\r\n",
            num_mats, SUPPLYORDER_MATS - num_mats);
    return 1;
  }

  GET_CRAFTING_TYPE(ch) = SCMD_SUPPLYORDER;
  GET_CRAFTING_TICKS(ch) = 5;
  GET_CRAFTING_TICKS(ch) -= MAX(4, (GET_SKILL(ch, SKILL_FAST_CRAFTER) / 25));
  GET_AUTOCQUEST_GOLD(ch) += GET_LEVEL(ch) * GET_LEVEL(ch);
  send_to_char(ch, "You begin a supply order for %s.\r\n",
          GET_AUTOCQUEST_DESC(ch));
  act("$n begins a supply order.", FALSE, ch, NULL, 0, TO_ROOM);

  obj_vnum = GET_OBJ_VNUM(kit);
  obj_from_char(kit);
  extract_obj(kit);
  kit = read_object(obj_vnum, VIRTUAL);
  obj_to_char(kit, ch);

  NEW_EVENT(eCRAFTING, ch, NULL, 1 * PASSES_PER_SEC);

  return 1;
}

/* resize an object, also will change weapon damage */
int resize(char *argument, struct obj_data *kit, struct char_data *ch) {
  int num_objs = 0, newsize, cost;
  struct obj_data *obj = NULL;
  int num_dice = -1;
  int size_dice = -1;

  /* Cycle through contents */
  /* resize requires just one item be inside the kit */
  for (obj = kit->contains; obj != NULL; obj = obj->next_content) {
    num_objs++;
    break;
  }

  if (num_objs > 1) {
    send_to_char(ch, "Only one item should be inside the kit.\r\n");
    return 1;
  }

  if (is_abbrev(argument, "fine"))
    newsize = SIZE_FINE;
  else if (is_abbrev(argument, "diminutive"))
    newsize = SIZE_DIMINUTIVE;
  else if (is_abbrev(argument, "tiny"))
    newsize = SIZE_TINY;
  else if (is_abbrev(argument, "small"))
    newsize = SIZE_SMALL;
  else if (is_abbrev(argument, "medium"))
    newsize = SIZE_MEDIUM;
  else if (is_abbrev(argument, "large"))
    newsize = SIZE_LARGE;
  else if (is_abbrev(argument, "huge"))
    newsize = SIZE_HUGE;
  else if (is_abbrev(argument, "gargantuan"))
    newsize = SIZE_GARGANTUAN;
  else if (is_abbrev(argument, "colossal"))
    newsize = SIZE_COLOSSAL;
  else {
    send_to_char(ch, "That is not a valid size: (fine|diminutive|tiny|small|"
            "medium|large|huge|gargantuan|colossal)\r\n");
    return 1;
  }

  if (newsize == GET_OBJ_SIZE(obj)) {
    send_to_char(ch, "The object is already the size you desire.\r\n");
    return 1;
  }

  /* weapon damage adjustment */
  if (GET_OBJ_TYPE(obj) == ITEM_WEAPON) {
    num_dice = GET_OBJ_VAL(obj, 1);
    size_dice = GET_OBJ_VAL(obj, 2);

    if (scale_damage(obj, newsize - GET_OBJ_SIZE(obj))) {
      /* success, weapon upgraded or downgraded in damage
         corresponding to size change */
      send_to_char(ch, "Weapon change:  %dd%d to %dd%d\r\n",
              num_dice, size_dice, GET_OBJ_VAL(obj, 1), GET_OBJ_VAL(obj, 2));
    } else {
      send_to_char(ch, "This weapon can not be resized.\r\n");
      return 1;
    }
  }

  /* "cost" of resizing */
  cost = GET_OBJ_COST(obj) / 2;
  if (GET_GOLD(ch) < cost) {
    send_to_char(ch, "You need %d coins on hand for supplies to make this "
            "item.\r\n", cost);
    return 1;
  }
  send_to_char(ch, "It cost you %d coins to resize this item.\r\n",
          cost);
  GET_GOLD(ch) -= cost;

  send_to_char(ch, "You begin to resize %s from %s to %s.\r\n",
          obj->short_description, size_names[GET_OBJ_SIZE(obj)],
          size_names[newsize]);
  act("$n begins resizing $p.", FALSE, ch, obj, 0, TO_ROOM);
  obj_from_obj(obj);

  /* resize object after taking out of kit, otherwise issues */
  /* weight adjustment of object */
  GET_OBJ_SIZE(obj) = newsize;
  GET_OBJ_WEIGHT(obj) += (newsize - GET_OBJ_SIZE(obj)) * GET_OBJ_WEIGHT(obj);
  if (GET_OBJ_WEIGHT(obj) <= 0)
    GET_OBJ_WEIGHT(obj) = 1;

  GET_CRAFTING_OBJ(ch) = obj;
  GET_CRAFTING_TYPE(ch) = SCMD_RESIZE;
  GET_CRAFTING_TICKS(ch) = 5;
  GET_CRAFTING_TICKS(ch) -= MAX(4, (GET_SKILL(ch, SKILL_FAST_CRAFTER) / 25));

  obj_to_char(obj, ch);
  NEW_EVENT(eCRAFTING, ch, NULL, 1 * PASSES_PER_SEC);

  return 1;
}


/* unfinished -zusuk */
int disenchant(struct obj_data *kit, struct char_data *ch) {
  struct obj_data *obj = NULL;
  int num_objs = 0;
  
  /* Cycle through contents */
  /* disenchant requires just one item be inside the kit */
  for (obj = kit->contains; obj != NULL; obj = obj->next_content) {
    num_objs++;
    break;
  }

  if (num_objs > 1) {
    send_to_char(ch, "Only one item should be inside the kit.\r\n");
    return 1;
  }  

  if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)) {
    send_to_char(ch, "You must drop something before you can disenchant anything.\r\n");
    return 1;
  }

  if (!IS_SET_AR(GET_OBJ_EXTRA(obj), ITEM_MAGIC)) {
    send_to_char(ch, "Only magical items can be disenchanted.\r\n");
    return 1;
  }

  if (obj && GET_OBJ_LEVEL(obj) > (GET_SKILL(ch, SKILL_CHEMISTRY)/3)) {
    send_to_char(ch, "Your chemistry skill isn't high enough to disenchant that item.\r\n");
    return 1;
  }
  
  /* award crystal for item */
  award_random_crystal(ch, GET_OBJ_LEVEL(obj));
  
  increase_skill(ch, SKILL_CHEMISTRY);

  GET_CRAFTING_TYPE(ch) = SCMD_DISENCHANT;
  GET_CRAFTING_TICKS(ch) = 5;
  GET_CRAFTING_TICKS(ch) -= MAX(4, (GET_SKILL(ch, SKILL_FAST_CRAFTER) / 25));

  send_to_char(ch, "You begin to disenchant %s.\r\n", obj->short_description);
  act("$n begins to disenchant $p.", FALSE, ch, obj, 0, TO_ROOM);

  /* clear item that got disenchanted */
  obj_from_obj(obj);
  extract_obj(obj);  
  
  NEW_EVENT(eCRAFTING, ch, NULL, 1 * PASSES_PER_SEC);
  return 1;
}



/* our create command and craftcheck, mode determines which we're using */
/* mode = 1; create     */
/* mode = 2; craftcheck */

/* As an extra layer of protection, only ITEM_MOLD should be used for
 * crafting.  It should be hard-coded un-wearable, BUT have the exact WEAR_
 * flags you want it to create. Also it should have the raw stats of the
 * item you want it to turn into.  Otherwise you could run into some issues
 * with stacking stats, etc.
 */

/*
 * create is for wearable gear at this stage
 */
int create(char *argument, struct obj_data *kit,
        struct char_data *ch, int mode) {
  char buf[MAX_INPUT_LENGTH] = { '\0' };  
  struct obj_data *obj = NULL, *mold = NULL, *crystal = NULL,
          *material = NULL, *essence = NULL;
  int num_mats = 0, obj_level = 1, skill = SKILL_WEAPON_SMITHING,
          crystal_value = -1, mats_needed = 12345, found = 0, i = 0, bonus = 0;

  /* sort through our kit and check if we got everything we need */
  for (obj = kit->contains; obj != NULL; obj = obj->next_content) {
    if (obj) {
      /* find a mold? */
      if (OBJ_FLAGGED(obj, ITEM_MOLD)) {
        if (!mold) {
          mold = obj;
          found++;
        } else {
          send_to_char(ch, "You have more than one mold inside the kit, "
                  "please only put one inside.\r\n");
          return 1;
        }
      }

      if (found) { // we didn't have a mold and found one, iterate main loop
        found = 0;
        continue;
      }

      /* find a crystal? */
      if (GET_OBJ_TYPE(obj) == ITEM_CRYSTAL) {
        if (!crystal) {
          crystal = obj;
        } else {
          send_to_char(ch, "You have more than one crystal inside the kit, "
                  "please only put one inside.\r\n");
          return 1;
        }

        /* find a material? */
      } else if (GET_OBJ_TYPE(obj) == ITEM_MATERIAL) {
        if (GET_OBJ_VAL(obj, 0) >= 2) {
          send_to_char(ch, "%s is a bundled item, which must first be"
                  " unbundled before you can use it to craft.\r\n",
                  obj->short_description);
          return 1;
        }
        if (!material) {
          material = obj;
          num_mats++;
        } else if (GET_OBJ_MATERIAL(obj) != GET_OBJ_MATERIAL(material)) {
          send_to_char(ch, "You have mixed materials in the kit, please "
                  "make sure to use only the required materials.\r\n");
          return 1;
        } else { /* this should be good */
          num_mats++;
        }

        /* find an essence? */
      } else if (GET_OBJ_TYPE(obj) == ITEM_ESSENCE) {
        if (!essence) {
          essence = obj;
        } else {
          send_to_char(ch, "You have more than one essence inside the kit, "
                  "please only put one inside.\r\n");
          return 1;
        }

      } else { /* didn't find anything we need */
        send_to_char(ch, "There is an unnecessary item in the kit, please "
                "remove it.\r\n");
        return 1;
      }
    }
  } /* end our sorting loop */

  /** check we have all the ingredients we need **/
  if (!mold) {
    send_to_char(ch, "The creation process requires a mold to continue.\r\n");
    return 1;
  }
  
  /* set base level, crystal should be ultimate determinant */
  obj_level = GET_OBJ_LEVEL(mold);
  
  if (!material) {
    send_to_char(ch, "You need to put materials into the kit.\r\n");
    return 1;
  }

  /* right material? */
  if (GET_SKILL(ch, SKILL_BONE_ARMOR) &&
          GET_OBJ_MATERIAL(material) == MATERIAL_BONE) {
    send_to_char(ch, "You use your mastery in bone-crafting to substitutue "
            "bone for the normal material needed...\r\n");
  } else if (IS_CLOTH(GET_OBJ_MATERIAL(mold)) &&
          !IS_CLOTH(GET_OBJ_MATERIAL(material))) {
    send_to_char(ch, "You need cloth for this mold pattern.\r\n");
    return 1;
  } else if (IS_LEATHER(GET_OBJ_MATERIAL(mold)) &&
          !IS_LEATHER(GET_OBJ_MATERIAL(material))) {
    send_to_char(ch, "You need leather for this mold pattern.\r\n");
    return 1;
  } else if (IS_WOOD(GET_OBJ_MATERIAL(mold)) &&
          !IS_WOOD(GET_OBJ_MATERIAL(material))) {
    send_to_char(ch, "You need wood for this mold pattern.\r\n");
    return 1;
  } else if (IS_HARD_METAL(GET_OBJ_MATERIAL(mold)) &&
          !IS_HARD_METAL(GET_OBJ_MATERIAL(material))) {
    send_to_char(ch, "You need hard metal for this mold pattern.\r\n");
    return 1;
  } else if (IS_PRECIOUS_METAL(GET_OBJ_MATERIAL(mold)) &&
          !IS_PRECIOUS_METAL(GET_OBJ_MATERIAL(material))) {
    send_to_char(ch, "You need precious metal for this mold pattern.\r\n");
    return 1;
  }
  /* we should be OK at this point with material validity, */
  /* although more error checking might be good */
  /* valid_misc_item_material_type(mold, material)) */
  /* expansion here or above to other miscellaneous materials, etc */

  /* determine how much material is needed 
   * [mold weight divided by weight_factor]
   */
  mats_needed = MAX(MIN_MATS, (GET_OBJ_WEIGHT(mold) / WEIGHT_FACTOR));

  /* elvent crafting reduces material needed */
  if (GET_SKILL(ch, SKILL_ELVEN_CRAFTING))
    mats_needed = MAX(MIN_ELF_MATS, mats_needed / 2);
  
  if (num_mats < mats_needed) {
    send_to_char(ch, "You do not have enough materials to make that item.  "
            "You need %d more units of the same type.\r\n",
            mats_needed - num_mats);
    return 1;
  } else if (num_mats > mats_needed) {
    send_to_char(ch, "You put too much material in the kit, please "
            "take out %d units.\r\n", num_mats - mats_needed);
    return 1;
  }

  /** check for other disqualifiers */
  /* valid name */
  if (mode == 1 && !strstr(argument,
          material_name[GET_OBJ_MATERIAL(material)])) {
    send_to_char(ch, "You must include the material name, '%s', in the object "
            "description somewhere.\r\n",
            material_name[GET_OBJ_MATERIAL(material)]);

    return 1;
  }

  /*** valid crystal usage ***/
  if (crystal) {
    crystal_value = crystal->affected[0].location;

    if (crystal_value == APPLY_HITROLL &&
            !CAN_WEAR(mold, ITEM_WEAR_HANDS)) {
      send_to_char(ch, "You can only imbue gauntlets or gloves with a "
              "hitroll enhancement.\r\n");
      return 1;
    }

    if ((crystal_value == APPLY_HITROLL ||
            crystal_value == APPLY_DAMROLL) &&
            !CAN_WEAR(mold, ITEM_WEAR_WIELD)) {
      send_to_char(ch, "You cannot imbue a non-weapon with weapon bonuses.\r\n");
      return 1;
    }

    /* for skill restriction and object level */
    obj_level += GET_OBJ_LEVEL(crystal);

    /* determine crystal bonus, etc */
    int mod = 0;
    for (i = 0; i <= MAX_CRAFT_CRIT; i++) {
      if (dice(1, 100) < 1)
        mod++;
      if (mod)
        send_to_char(ch, "@l@WYou have received a critical success on your "
              "craft! (+%d)@n\r\n", mod);
    }
    
    if (GET_SKILL(ch, SKILL_MASTERWORK_CRAFTING)) {
      send_to_char(ch, "Your masterwork-crafting skill increases the quality of "
              "the item.\r\n");
      mod++;
    }
    
    if (GET_SKILL(ch, SKILL_DWARVEN_CRAFTING)) {
      send_to_char(ch, "Your dwarven-crafting skill increases the quality of "
              "the item.\r\n");
      mod++;
    }
    
    if (GET_SKILL(ch, SKILL_DRACONIC_CRAFTING)) {
      send_to_char(ch, "Your draconic-crafting skill increases the quality of "
              "the item.\r\n");
      mod++;
    }
    bonus = crystal_bonus(crystal, mod);
  }
  /*** end valid crystal usage ***/

  /* which skill is used for this crafting session? */
  /* we determine crafting skill by wear-flag */

  /* jewel making (finger, */
  if (CAN_WEAR(mold, ITEM_WEAR_FINGER) ||
          CAN_WEAR(mold, ITEM_WEAR_NECK) ||
          CAN_WEAR(mold, ITEM_WEAR_HOLD)
          ) {
    skill = SKILL_JEWELRY_MAKING;
  }
    /* body armor pieces: either armor-smith/leather-worker/or knitting */
  else if (CAN_WEAR(mold, ITEM_WEAR_BODY) ||
          CAN_WEAR(mold, ITEM_WEAR_ARMS) ||
          CAN_WEAR(mold, ITEM_WEAR_LEGS) ||
          CAN_WEAR(mold, ITEM_WEAR_HEAD) ||
          CAN_WEAR(mold, ITEM_WEAR_FEET) ||
          CAN_WEAR(mold, ITEM_WEAR_HANDS) ||
          CAN_WEAR(mold, ITEM_WEAR_WRIST) ||
          CAN_WEAR(mold, ITEM_WEAR_WAIST)
          ) {
    if (IS_HARD_METAL(GET_OBJ_MATERIAL(mold)))
      skill = SKILL_ARMOR_SMITHING;
    else if (IS_LEATHER(GET_OBJ_MATERIAL(mold)))
      skill = SKILL_LEATHER_WORKING;
    else
      skill = SKILL_KNITTING;
  }
    /* about body */
  else if (CAN_WEAR(mold, ITEM_WEAR_ABOUT)) {
    skill = SKILL_KNITTING;
  }
    /* weapon-smithing:  weapons and shields */
  else if (CAN_WEAR(mold, ITEM_WEAR_WIELD) ||
          CAN_WEAR(mold, ITEM_WEAR_SHIELD)
          ) {
    skill = SKILL_WEAPON_SMITHING;
  }

  /* skill restriction */
  if ((GET_SKILL(ch, skill) / 3) < obj_level) {
    send_to_char(ch, "Your skill in %s is too low to create that item.\r\n",
            spell_info[skill].name);
    return 1;
  }

  int cost = obj_level * obj_level * 100 / 3;

  /** passed all the tests, time to check or create the item **/
  if (mode == 2) { /* checkcraft */
    send_to_char(ch, "This crafting session will create the following "
            "item:\r\n\r\n");
    call_magic(ch, ch, mold, SPELL_IDENTIFY, LVL_IMMORT, CAST_SPELL);
    if (crystal) {
      send_to_char(ch, "You will be enhancing this value: %s.\r\n",
              apply_types[crystal_value]);
      send_to_char(ch, "The total bonus will be: %d.\r\n", bonus);
    }
    send_to_char(ch, "The item will be level: %d.\r\n", obj_level);
    send_to_char(ch, "It will make use of your %s skill, which has a value "
            "of %d.\r\n",
            spell_info[skill].name, GET_SKILL(ch, skill));
    send_to_char(ch, "This crafting session will take 60 seconds.\r\n");
    send_to_char(ch, "You need %d gold on hand to make this item.\r\n", cost);
    return 1;
  } else if (GET_GOLD(ch) < cost) {
    send_to_char(ch, "You need %d coins on hand for supplies to make"
            "this item.\r\n", cost);
    return 1;
  } else { /* create */
    REMOVE_BIT_AR(GET_OBJ_EXTRA(mold), ITEM_MOLD);
    if (essence)
      SET_BIT_AR(GET_OBJ_EXTRA(mold), ITEM_MAGIC);
    GET_OBJ_LEVEL(mold) = obj_level;
    GET_OBJ_MATERIAL(mold) = GET_OBJ_MATERIAL(material);
    if (crystal) {
      mold->affected[0].location = crystal_value;
      mold->affected[0].modifier = bonus;
    }
    GET_OBJ_COST(mold) = 100 + GET_OBJ_LEVEL(mold) * 50 *
            MAX(1, GET_OBJ_LEVEL(mold) - 1) +
            GET_OBJ_COST(mold);
    GET_CRAFTING_BONUS(ch) = 10 + MIN(60, GET_OBJ_LEVEL(mold));

    send_to_char(ch, "It cost you %d gold in supplies to create this item.\r\n",
            cost);
    GET_GOLD(ch) -= cost;

    /* gotta convert @ sign */
    parse_at(argument);

    /* restringing aspect */
    mold->name = strdup(argument);
    mold->short_description = strdup(argument);
    sprintf(buf, "%s lies here.", CAP(argument));
    mold->description = strdup(buf);

    send_to_char(ch, "You begin to craft %s.\r\n", mold->short_description);
    act("$n begins to craft $p.", FALSE, ch, mold, 0, TO_ROOM);

    GET_CRAFTING_OBJ(ch) = mold;
    obj_from_obj(mold); /* extracting this causes issues, solution? */
    GET_CRAFTING_TYPE(ch) = SCMD_CRAFT;
    GET_CRAFTING_TICKS(ch) = 11;
    GET_CRAFTING_TICKS(ch) -= MAX(4, (GET_SKILL(ch, SKILL_FAST_CRAFTER) / 25));
    int kit_obj_vnum = GET_OBJ_VNUM(kit);
    obj_from_room(kit);
    extract_obj(kit);
    kit = read_object(kit_obj_vnum, VIRTUAL);

    obj_to_char(kit, ch);

    obj_to_char(mold, ch);
    increase_skill(ch, skill);
    NEW_EVENT(eCRAFTING, ch, NULL, 1 * PASSES_PER_SEC);
  }
  return 1;
}

SPECIAL(crafting_kit) {
  if (!CMD_IS("resize") && !CMD_IS("create") && !CMD_IS("checkcraft") &&
          !CMD_IS("restring") && !CMD_IS("augment") && !CMD_IS("convert") &&
          !CMD_IS("autocraft") && !CMD_IS("disenchant"))
    return 0;

  if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)) {
    send_to_char(ch, "You cannot craft anything until you've made some "
            "room in your inventory.\r\n");
    return 1;
  }

  if (GET_CRAFTING_OBJ(ch) || char_has_mud_event(ch, eCRAFTING)) {
    send_to_char(ch, "You are already doing something.  Please wait until "
            "your current task ends.\r\n");
    return 1;
  }

  struct obj_data *kit = (struct obj_data *) me;
  skip_spaces(&argument);

  /* Some of the commands require argument */
  if (!*argument && !CMD_IS("checkcraft") && !CMD_IS("augment") &&
          !CMD_IS("autocraft") && !CMD_IS("convert")&& !CMD_IS("disenchant")) {
    if (CMD_IS("create") || CMD_IS("restring"))
      send_to_char(ch, "Please provide an item description containing the "
            "material and item name in the string.\r\n");
    else if (CMD_IS("resize"))
      send_to_char(ch, "What would you like the new size to be?"
            " (fine|diminutive|tiny|small|"
            "medium|large|huge|gargantuan|colossal)\r\n");
    return 1;
  }

  if (!kit->contains) {
    if (CMD_IS("augment"))
      send_to_char(ch, "You must place at least two crystals of the same "
            "type into the kit in order to augment.\r\n");
    else if (CMD_IS("autocraft")) {
      if (GET_AUTOCQUEST_MATERIAL(ch))
        send_to_char(ch, "You must place %d units of %s or a similar type of "
              "material (all the same type) into the kit to continue.\r\n",
              SUPPLYORDER_MATS,
              material_name[GET_AUTOCQUEST_MATERIAL(ch)]);
      else
        send_to_char(ch, "You do not have a supply order active "
              "right now.\r\n");
    } else if (CMD_IS("create"))
      send_to_char(ch, "You must place an item to use as the mold pattern, "
            "a crystal and your crafting resource materials in the "
            "kit and then type 'create <optional item "
            "description>'\r\n");
    else if (CMD_IS("restring"))
      send_to_char(ch, "You must place the item to restring and in the "
            "crafting kit.\r\n");
    else if (CMD_IS("resize"))
      send_to_char(ch, "You must place the item in the kit to resize it.\r\n");
    else if (CMD_IS("checkcraft"))
      send_to_char(ch, "You must place an item to use as the mold pattern, a "
            "crystal and your crafting resource materials in the kit and "
            "then type 'checkcraft'\r\n");
    else if (CMD_IS("convert"))
      send_to_char(ch, "You must place exact multiples of 10, of a material "
            "to being the conversion process.\r\n");
    else if (CMD_IS("disenchant"))
      send_to_char(ch, "You must place the item you want to disenchant "
              "in the kit.\r\n");
    else
      send_to_char(ch, "Unrecognized crafting-kit command!\r\n");
    return 1;
  }  
  
  if (CMD_IS("resize"))
    return resize(argument, kit, ch);
  else if (CMD_IS("restring"))
    return restring(argument, kit, ch);
  else if (CMD_IS("augment"))
    return augment(kit, ch);
  else if (CMD_IS("convert"))
    return convert(kit, ch);
  else if (CMD_IS("autocraft"))
    return autocraft(kit, ch);
  else if (CMD_IS("create"))
    return create(argument, kit, ch, 1);
  else if (CMD_IS("checkcraft"))
    return create(NULL, kit, ch, 2);
  else if (CMD_IS("disenchant"))
    return disenchant(kit, ch);
  else {
    send_to_char(ch, "Invalid command.\r\n");
    return 0;
  }
  return 0;
}

/* here is our room-spec for crafting quest */
SPECIAL(crafting_quest) {
  char desc[MAX_INPUT_LENGTH];
  char arg[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];
  int roll = 0;

  if (!CMD_IS("supplyorder")) {
    return 0;
  }

  two_arguments(argument, arg, arg2);

  if (!*arg)
    cquest_report(ch);
  else if (!strcmp(arg, "new")) {
    if (GET_AUTOCQUEST_VNUM(ch) && GET_AUTOCQUEST_MAKENUM(ch) <= 0) {
      send_to_char(ch, "You can't take a new supply order until you've "
              "handed in the one you've completed (supplyorder complete).\r\n");
      return 1;
    }

    /* initialize values */
    reset_acraft(ch);
    GET_AUTOCQUEST_VNUM(ch) = AUTOCQUEST_VNUM;

    switch (dice(1, 5)) {
      case 1:
        sprintf(desc, "a shield");
        GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_WOOD;
        break;
      case 2:
        sprintf(desc, "a sword");
        GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_STEEL;
        break;
      case 3:
        if ((roll = dice(1, 7)) == 1) {
          sprintf(desc, "a necklace");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_COPPER;
        } else if (roll == 2) {
          sprintf(desc, "a bracer");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_COPPER;
        } else if (roll == 3) {
          sprintf(desc, "a cloak");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_WOOL;
        } else if (roll == 4) {
          sprintf(desc, "a cape");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_HEMP;
        } else if (roll == 5) {
          sprintf(desc, "a belt");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_BURLAP;
        } else if (roll == 6) {
          sprintf(desc, "a pair of gloves");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_COTTON;
        } else {
          sprintf(desc, "a pair of boots");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_LEATHER;
        }
        break;
      case 4:
        if ((roll = dice(1, 2)) == 1) {
          sprintf(desc, "a suit of ringmail");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_IRON;
        } else {
          sprintf(desc, "a cloth robe");
          GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_SATIN;
        }
        break;
      default:
        sprintf(desc, "some war supplies");
        GET_AUTOCQUEST_MATERIAL(ch) = MATERIAL_BRONZE;
        break;
    }

    GET_AUTOCQUEST_DESC(ch) = strdup(desc);
    GET_AUTOCQUEST_MAKENUM(ch) = AUTOCQUEST_MAKENUM;
    GET_AUTOCQUEST_QP(ch) = 1;
    GET_AUTOCQUEST_EXP(ch) = (GET_LEVEL(ch) * GET_LEVEL(ch)) * 10;
    GET_AUTOCQUEST_GOLD(ch) = GET_LEVEL(ch) * 100;

    send_to_char(ch, "You have been commissioned for a supply order to "
            "make %s.  We expect you to make %d before you can collect your "
            "reward.  Good luck!  Once completed you will receive the "
            "following:  You will receive %d quest points."
            "  %d gold will be given to you.  You will receive %d "
            "experience points.\r\n",
            desc, GET_AUTOCQUEST_MAKENUM(ch), GET_AUTOCQUEST_QP(ch),
            GET_AUTOCQUEST_GOLD(ch), GET_AUTOCQUEST_EXP(ch));
  } else if (!strcmp(arg, "complete")) {
    if (GET_AUTOCQUEST_VNUM(ch) && GET_AUTOCQUEST_MAKENUM(ch) <= 0) {
      send_to_char(ch, "You have completed your supply order contract"
              " for %s.\r\n"
              "You receive %d reputation points.\r\n"
              "%d gold has been given to you.\r\n"
              "You receive %d experience points.\r\n",
              GET_AUTOCQUEST_DESC(ch), GET_AUTOCQUEST_QP(ch),
              GET_AUTOCQUEST_GOLD(ch), GET_AUTOCQUEST_EXP(ch));
      GET_QUESTPOINTS(ch) += GET_AUTOCQUEST_QP(ch);
      GET_GOLD(ch) += GET_AUTOCQUEST_GOLD(ch);
      GET_EXP(ch) += GET_AUTOCQUEST_EXP(ch);

      reset_acraft(ch);
    } else
      cquest_report(ch);
  } else if (!strcmp(arg, "quit")) {
    send_to_char(ch, "You abandon your supply order to make %d %s.\r\n",
            GET_AUTOCQUEST_MAKENUM(ch), GET_AUTOCQUEST_DESC(ch));
    reset_acraft(ch);
  } else
    cquest_report(ch);

  return 1;
}

/* the event driver for crafting */
EVENTFUNC(event_crafting) {
  struct char_data *ch;
  struct mud_event_data *pMudEvent;
  struct obj_data *obj2 = NULL;
  char buf[MAX_INPUT_LENGTH], buf2[MAX_INPUT_LENGTH];
  int exp = 0, skill = -1, roll = -1;

  //initialize everything and dummy checks
  if (event_obj == NULL) return 0;
  pMudEvent = (struct mud_event_data *) event_obj;
  ch = (struct char_data *) pMudEvent->pStruct;
  if (!IS_NPC(ch) && !IS_PLAYING(ch->desc))
    return 0;

  // something is off, so ensure reset
  if (!GET_AUTOCQUEST_VNUM(ch) && GET_CRAFTING_OBJ(ch) == NULL) {
    log("SYSERR: crafting - null object");
    return 0;
  }
  if (GET_CRAFTING_TYPE(ch) == 0) {
    log("SYSERR: crafting - invalid type");
    return 0;
  }

  if (GET_CRAFTING_TICKS(ch)) {
    // the crafting tick is still going!
    if (GET_CRAFTING_OBJ(ch)) {
      send_to_char(ch, "You continue to %s %s.\r\n",
              craft_type[GET_CRAFTING_TYPE(ch)],
              GET_CRAFTING_OBJ(ch)->short_description);
      exp = GET_OBJ_LEVEL(GET_CRAFTING_OBJ(ch)) * GET_LEVEL(ch) + GET_LEVEL(ch);
    } else {
      send_to_char(ch, "You continue your supply order for %s.\r\n",
              GET_AUTOCQUEST_DESC(ch));
      exp = GET_LEVEL(ch) * 2;
    }
    gain_exp(ch, exp);
    send_to_char(ch, "You gained %d exp for crafting...\r\n", exp);

    send_to_char(ch, "You have approximately %d seconds "
            "left to go.\r\n", GET_CRAFTING_TICKS(ch) * 6);

    GET_CRAFTING_TICKS(ch)--;
    
    /* skill notch */
    increase_skill(ch, SKILL_FAST_CRAFTER);
    
    if (GET_LEVEL(ch) >= LVL_IMMORT)
      return 1;
    else
      return (6 * PASSES_PER_SEC); // come back in x time to the event

  } else { /* should be completed */

    switch (GET_CRAFTING_TYPE(ch)) {
      case SCMD_RESIZE:
        // no skill association
        sprintf(buf, "You resize $p.  Success!!!");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n resizes $p.");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        break;
      case SCMD_DIVIDE:
        // no skill association
        sprintf(buf, "You create $p (x%d).  Success!!!",
                GET_CRAFTING_REPEAT(ch));
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n creates $p (x%d).", GET_CRAFTING_REPEAT(ch));
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        int i = 0;
        for (i = 1; i < GET_CRAFTING_REPEAT(ch); i++) {
          obj2 = read_object(GET_OBJ_VNUM(GET_CRAFTING_OBJ(ch)), VIRTUAL);
          obj_to_char(obj2, ch);
        }
        break;
      case SCMD_MINE:
        skill = SKILL_MINING;
        sprintf(buf, "You mine $p.  Success!!!");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n mines $p.");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        break;
      case SCMD_HUNT:
        skill = SKILL_FORESTING;
        sprintf(buf, "You find $p from your hunting.  Success!!!");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n finds $p from $s hunting.");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        break;
      case SCMD_KNIT:
        skill = SKILL_KNITTING;
        sprintf(buf, "You knit $p.  Success!!!");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n knits $p.");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        break;
      case SCMD_FOREST:
        skill = SKILL_FORESTING;
        sprintf(buf, "You forest $p.  Success!!!");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n forests $p.");
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        break;
      case SCMD_DISENCHANT:
        skill = SKILL_CHEMISTRY;
        sprintf(buf, "You complete the disenchantment process.  Success!!!");
        act(buf, false, ch, 0, 0, TO_CHAR);
        sprintf(buf, "$n finishes the disenchanting process.");
        act(buf, false, ch, 0, 0, TO_ROOM);
        break;
      case SCMD_SYNTHESIZE:
        // synthesizing here
        break;
      case SCMD_CRAFT:
        // skill notch is done in create command
        if (GET_CRAFTING_REPEAT(ch)) {
          sprintf(buf2, " (x%d)", GET_CRAFTING_REPEAT(ch) + 1);
          for (i = 0; i < MAX(0, GET_CRAFTING_REPEAT(ch)); i++) {
            obj2 = GET_CRAFTING_OBJ(ch);
            obj_to_char(obj2, ch);
          }
          GET_CRAFTING_REPEAT(ch) = 0;
        } else
          sprintf(buf2, "\tn");
        sprintf(buf, "You create $p%s.  Success!!!", buf2);
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n creates $p%s.", buf2);
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        if (GET_GOLD(ch) < (GET_OBJ_COST(GET_CRAFTING_OBJ(ch)) / 4)) {
          GET_BANK_GOLD(ch) -= GET_OBJ_COST(GET_CRAFTING_OBJ(ch)) / 4;
        } else {
          GET_GOLD(ch) -= GET_OBJ_COST(GET_CRAFTING_OBJ(ch)) / 4;
        }
        break;
      case SCMD_AUGMENT:
        // use to be part of crafting
        skill = SKILL_CHEMISTRY;
        if (GET_CRAFTING_REPEAT(ch)) {
          sprintf(buf2, " (x%d)", GET_CRAFTING_REPEAT(ch) + 1);
          for (i = 0; i < MAX(0, GET_CRAFTING_REPEAT(ch)); i++) {
            obj2 = GET_CRAFTING_OBJ(ch);
            obj_to_char(obj2, ch);
          }
          GET_CRAFTING_REPEAT(ch) = 0;
        } else
          sprintf(buf2, "\tn");
        sprintf(buf, "You augment $p%s.  Success!!!", buf2);
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n augments $p%s.", buf2);
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        break;
      case SCMD_CONVERT:
        // use to be part of crafting
        skill = SKILL_CHEMISTRY;
        if (GET_CRAFTING_REPEAT(ch)) {
          sprintf(buf2, " (x%d)", GET_CRAFTING_REPEAT(ch) + 1);
          for (i = 0; i < MAX(0, GET_CRAFTING_REPEAT(ch)); i++) {
            obj2 = GET_CRAFTING_OBJ(ch);
            obj_to_char(obj2, ch);
          }
          GET_CRAFTING_REPEAT(ch) = 0;
        } else
          sprintf(buf2, "\tn");
        sprintf(buf, "You convert $p%s.  Success!!!", buf2);
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n converts $p%s.", buf2);
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        break;
      case SCMD_RESTRING:
        // no skill association
        // use to be part of crafting
        sprintf(buf2, "\tn");
        sprintf(buf, "You rename $p%s.  Success!!!", buf2);
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_CHAR);
        sprintf(buf, "$n renames $p%s.", buf2);
        act(buf, false, ch, GET_CRAFTING_OBJ(ch), 0, TO_ROOM);
        break;
      case SCMD_SUPPLYORDER:
        /* picking a random trade to notch */
        roll = dice(1, 4);
        switch (dice(1,4)) {
          case 1:
            skill = SKILL_ARMOR_SMITHING;
            break;
          case 2:
            skill = SKILL_WEAPON_SMITHING;
            break;
          case 3:
            skill = SKILL_JEWELRY_MAKING;
            break;
          default:
            skill = SKILL_LEATHER_WORKING;
            break;
        }
        
        GET_AUTOCQUEST_MAKENUM(ch)--;
        if (GET_AUTOCQUEST_MAKENUM(ch) <= 0) {
          sprintf(buf, "$n completes an item for a supply order.");
          act(buf, false, ch, NULL, 0, TO_ROOM);
          send_to_char(ch, "You have completed your supply order! Go turn"
                  " it in for more exp, quest points and "
                  "gold!\r\n");
        } else {
          sprintf(buf, "$n completes a supply order.");
          act(buf, false, ch, NULL, 0, TO_ROOM);
          send_to_char(ch, "You have completed another item in your supply "
                  "order and have %d more to make.\r\n",
                  GET_AUTOCQUEST_MAKENUM(ch));
        }
        break;
      default:
        log("SYSERR: crafting - unsupported SCMD_");
        return 0;
    }

    /* notch skills */
    if (skill != -1)
      increase_skill(ch, skill);
    reset_craft(ch);
    return 0; //done with the event
  }
  log("SYSERR: crafting, crafting_event end");
  return 0;
}

/* the 'harvest' command */
ACMD(do_harvest) {
  struct obj_data *obj = NULL, *node = NULL;
  int roll = 0, skillnum = SKILL_MINING, material = -1, minskill = 0;
  char arg[MAX_INPUT_LENGTH] = { '\0' };
  char buf[MEDIUM_STRING] = { '\0' };
  int sub_command = SCMD_CRAFT_UNDF;
  
  if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)) {
    send_to_char(ch, "You must drop something before you can harvest anything else.\r\n");
    return;
  }

  one_argument(argument, arg);

  if (!*arg) {
    send_to_char(ch, "You need to specify what you want to harvest.\r\n");
    return;
  }

  if (!(node = get_obj_in_list_vis(ch, arg, NULL, world[IN_ROOM(ch)].contents))) {
    send_to_char(ch, "That doesn't seem to be present in this room.\r\n");
    return;
  }

  if (GET_OBJ_VNUM(node) != HARVESTING_NODE) {
    send_to_char(ch, "That is not a harvesting node.\r\n");
    return;
  }

  material = GET_OBJ_MATERIAL(node);

  if (IS_WOOD(material)) {
    skillnum = SKILL_FORESTING;
    sub_command = SCMD_FOREST;
  } else if (IS_LEATHER(material)) {
    skillnum = SKILL_HUNTING;
    sub_command = SCMD_HUNT;
  } else if (IS_CLOTH(material)) {
    skillnum = SKILL_KNITTING;
    sub_command = SCMD_KNIT;
  } else {
    skillnum = SKILL_MINING;
    sub_command = SCMD_MINE;
  }

  switch (material) {

    case MATERIAL_STEEL:
      roll = dice(1, 100);
      if (roll <= 40)
        obj = read_object(BRONZE_MATERIAL, VIRTUAL); // bronze
      else if (roll <= 75)
        obj = read_object(IRON_MATERIAL, VIRTUAL); // iron
      else if (roll <= 96)
        obj = read_object(STEEL_MATERIAL, VIRTUAL); // steel
      else if (roll <= 98)
        obj = read_object(ONYX_MATERIAL, VIRTUAL); // onyx
      else
        obj = read_object(OBSIDIAN_MATERIAL, VIRTUAL); // obsidian
      minskill = 1;
      break;

    case MATERIAL_COLD_IRON:
      roll = dice(1, 100);
      if (roll <= 48)
        obj = read_object(COLD_IRON_MATERIAL, VIRTUAL); // cold iron
      else if (roll <= 52)
        obj = read_object(ONYX_MATERIAL, VIRTUAL); // onyx
      else
        obj = read_object(IRON_MATERIAL, VIRTUAL); // iron
      minskill = 35;
      break;

    case MATERIAL_MITHRIL:
      roll = dice(1, 100);
      if (roll <= 48)
        obj = read_object(MITHRIL_MATERIAL, VIRTUAL); // mithril
      else if (roll <= 96)
        obj = read_object(MITHRIL_MATERIAL, VIRTUAL); // mithril
      else if (roll <= 98)
        obj = read_object(RUBY_MATERIAL, VIRTUAL); // ruby
      else
        obj = read_object(SAPPHIRE_MATERIAL, VIRTUAL); // sapphire
      minskill = 48;
      break;

    case MATERIAL_ADAMANTINE:
      roll = dice(1, 100);
      if (roll <= 4)
        obj = read_object(ADAMANTINE_MATERIAL, VIRTUAL); // adamantine
      else if (roll <= 96)
        obj = read_object(PLATINUM_MATERIAL, VIRTUAL); // platinum
      else {
        if (dice(1, 2) % 2 == 0)
          obj = read_object(DIAMOND_MATERIAL, VIRTUAL); // diamond
        else
          obj = read_object(EMERALD_MATERIAL, VIRTUAL); // emerald
      }
      minskill = 61;
      break;

    case MATERIAL_SILVER:
      roll = dice(1, 10);
      if (roll <= (8)) {
        roll = dice(1, 100);
        if (roll <= 48)
          obj = read_object(COPPER_MATERIAL, VIRTUAL); // copper
        else if (roll <= 96)
          obj = read_object(ALCHEMAL_SILVER_MATERIAL, VIRTUAL); // alchemal silver
        else if (roll <= 98)
          obj = read_object(ONYX_MATERIAL, VIRTUAL); // onyx
        else
          obj = read_object(OBSIDIAN_MATERIAL, VIRTUAL); // obsidian
      } else {
        roll = dice(1, 100);
        if (roll <= 48)
          obj = read_object(SILVER_MATERIAL, VIRTUAL); // silver
        else if (roll <= 52)
          obj = read_object(ONYX_MATERIAL, VIRTUAL); // onyx
        else
          obj = read_object(SILVER_MATERIAL, VIRTUAL); // silver
      }
      minskill = 1;
      break;

    case MATERIAL_GOLD:
      roll = dice(1, 10);
      if (roll <= (8)) {
        roll = dice(1, 100);
        if (roll <= 48)
          obj = read_object(GOLD_MATERIAL, VIRTUAL); // gold
        else if (roll <= 96)
          obj = read_object(GOLD_MATERIAL, VIRTUAL); // gold
        else if (roll <= 98)
          obj = read_object(RUBY_MATERIAL, VIRTUAL); // ruby
        else
          obj = read_object(SAPPHIRE_MATERIAL, VIRTUAL); // sapphire
      } else {
        roll = dice(1, 100);
        if (roll <= 4)
          obj = read_object(PLATINUM_MATERIAL, VIRTUAL); // platinum
        else if (roll <= 96)
          obj = read_object(PLATINUM_MATERIAL, VIRTUAL); // platinum
        else {
          if (dice(1, 2) % 2 == 0)
            obj = read_object(DIAMOND_MATERIAL, VIRTUAL); // diamond
          else
            obj = read_object(EMERALD_MATERIAL, VIRTUAL); // emerald
        }
      }
      minskill = 30;
      break;

    case MATERIAL_WOOD:
      roll = dice(1, 100);
      if (roll <= (80)) {
        if (dice(1, 100) <= 96)
          obj = read_object(ALDERWOOD_MATERIAL, VIRTUAL); // alderwood
        else
          obj = read_object(FOS_BIRD_MATERIAL, VIRTUAL); // fossilized bird egg
      } else if (roll <= (94)) {
        if (dice(1, 100) <= 96)
          obj = read_object(YEW_MATERIAL, VIRTUAL); // yew
        else
          obj = read_object(FOS_LIZARD_MATERIAL, VIRTUAL); // fossilized giant lizard egg
      } else {
        if (dice(1, 100) <= 96)
          obj = read_object(OAK_MATERIAL, VIRTUAL); // oak
        else
          obj = read_object(FOS_WYVERN_MATERIAL, VIRTUAL); // fossilized wyvern egg
      }
      minskill = 1;
      break;

    case MATERIAL_DARKWOOD:
      if (dice(1, 100) <= 96)
        obj = read_object(DARKWOOD_MATERIAL, VIRTUAL); // darkwood
      else
        obj = read_object(FOS_DRAGON_MATERIAL, VIRTUAL); // fossilized dragon egg
      minskill = 38;
      break;

    case MATERIAL_LEATHER:
      roll = dice(1, 100);
      if (roll <= (82)) {
        if (dice(1, 100) <= 96) {
          obj = read_object(LEATHER_LQ_MATERIAL, VIRTUAL); // low quality hide
        } else
          obj = read_object(FOS_BIRD_MATERIAL, VIRTUAL); // fossilized bird egg
      } else if (roll <= (94)) {
        if (dice(1, 10) <= 96) {
          obj = read_object(LEATHER_MQ_MATERIAL, VIRTUAL); // medium quality hide
        } else
          obj = read_object(FOS_LIZARD_MATERIAL, VIRTUAL); // fossilized giant lizard egg
      } else {
        if (dice(1, 100) <= 96) {
          obj = read_object(LEATHER_HQ_MATERIAL, VIRTUAL); // high quality hide
        } else
          obj = read_object(FOS_WYVERN_MATERIAL, VIRTUAL); // fossilized wyvern egg
      }
      minskill = 1;
      break;

    case MATERIAL_DRAGONHIDE:
      if (dice(1, 100) <= 70)
        obj = read_object(LEATHER_HQ_MATERIAL, VIRTUAL); // high quality leather
      else
        obj = read_object(DRAGONHIDE_MATERIAL, VIRTUAL); // dragon hide
      minskill = 58;
      break;

    case MATERIAL_HEMP:
      if (dice(1, 100) <= 96)
        obj = read_object(HEMP_MATERIAL, VIRTUAL); // hemp
      else
        obj = read_object(FOS_BIRD_MATERIAL, VIRTUAL); // fossilized bird egg
      minskill = 1;
      break;

    case MATERIAL_COTTON:
      if (dice(1, 100) <= 96) {
        obj = read_object(COTTON_MATERIAL, VIRTUAL); // cotton
      } else
        obj = read_object(FOS_LIZARD_MATERIAL, VIRTUAL); // fossilized giant lizard egg
      minskill = 5;
      break;

    case MATERIAL_WOOL:
      if (dice(1, 100) <= 96) {
        obj = read_object(WOOL_MATERIAL, VIRTUAL); // wool
      } else
        obj = read_object(FOS_LIZARD_MATERIAL, VIRTUAL); // fossilized giant lizard egg
      minskill = 10;
      break;

    case MATERIAL_VELVET:
      if (dice(1, 100) <= 96) {
        obj = read_object(VELVET_MATERIAL, VIRTUAL); // velvet
      } else
        obj = read_object(FOS_WYVERN_MATERIAL, VIRTUAL); // fossilized wyvern egg
      minskill = 25;
      break;

    case MATERIAL_SATIN:
      if (dice(1, 100) <= 96) {
        obj = read_object(SATIN_MATERIAL, VIRTUAL); // satin
      } else
        obj = read_object(FOS_WYVERN_MATERIAL, VIRTUAL); // fossilized wyvern egg
      minskill = 31;
      break;

    case MATERIAL_SILK:
      if (dice(1, 100) <= 96) {
        if (dice(1, 100) <= 25)
          obj = read_object(VELVET_MATERIAL, VIRTUAL); // velvet
        else if (dice(1, 100) <= 25)
          obj = read_object(SATIN_MATERIAL, VIRTUAL); // satin
        else
          obj = read_object(SILK_MATERIAL, VIRTUAL); // silk
      } else
        obj = read_object(FOS_DRAGON_MATERIAL, VIRTUAL); // fossilized dragon egg
      minskill = 38;
      break;

    default:
      send_to_char(ch, "That is not a valid node type, please report this to a staff member [1].\r\n");
      return;
  }

  if (!obj) {
    send_to_char(ch, "That is not a valid node type, please report this to a staff member [2].\r\n");
    return;
  }

  if (GET_SKILL(ch, skillnum) < minskill) {
    send_to_char(ch, "You need a minimum %s skill of %d, while yours is only %d.\r\n",
            spell_info[skillnum].name, minskill, GET_SKILL(ch, skillnum));
    return;
  }

  GET_CRAFTING_TYPE(ch) = sub_command;
  GET_CRAFTING_TICKS(ch) = 5;
  GET_CRAFTING_OBJ(ch) = obj;

  // Tell the character they made something. 
  sprintf(buf, "You begin to %s.", CMD_NAME);
  act(buf, FALSE, ch, 0, NULL, TO_CHAR);

  // Tell the room the character made something. 
  sprintf(buf, "$n begins to %s.", CMD_NAME);
  act(buf, FALSE, ch, 0, NULL, TO_ROOM);

  if (node)
    GET_OBJ_VAL(node, 0)--;

  if (node && GET_OBJ_VAL(node, 0) <= 0) {
    switch (skillnum) {
      case SKILL_MINING:
        mining_nodes--;
        break;
      case SKILL_KNITTING:
        farming_nodes--;
        break;
      default:
        foresting_nodes--;
        break;
    }
    obj_from_room(node);
    extract_obj(node);
  }
  
  obj_to_char(obj, ch);
  NEW_EVENT(eCRAFTING, ch, NULL, 1 * PASSES_PER_SEC);

  return;
}




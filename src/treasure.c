/* *************************************************************************
 *   File: treasure.c                                 Part of LuminariMUD *
 *  Usage: functions for random treasure objects                           *
 *  Author: d20mud, ported to tba/luminari by Zusuk                        *
 ************************************************************************* */

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "interpreter.h"
#include "handler.h"
#include "db.h"
#include "spells.h"
#include "constants.h"
#include "dg_scripts.h"
#include "treasure.h"
#include "craft.h"

/***************************************************************/
/***  utility functions ***/

/* some spells are not appropriate for expendable items, this simple
 function returns TRUE if the spell is OK, FALSE if not */
bool valid_item_spell(int spellnum) {

  /* just list exceptions */
  switch (spellnum) {
    case SPELL_VENTRILOQUATE:
    case SPELL_MUMMY_DUST:
    case SPELL_DRAGON_KNIGHT:
    case SPELL_GREATER_RUIN:
    case SPELL_HELLBALL:
    case SPELL_EPIC_MAGE_ARMOR:
    case SPELL_EPIC_WARDING:
    case SPELL_FIRE_BREATHE:
    case SPELL_STENCH:
    case SPELL_ACID_SPLASH:
    case SPELL_RAY_OF_FROST:
    case SPELL_FSHIELD_DAM:
    case SPELL_CSHIELD_DAM:
    case SPELL_ASHIELD_DAM:
    case SPELL_DEATHCLOUD:
    case SPELL_ACID:
    case SPELL_INCENDIARY:
    case SPELL_UNUSED271:
    case SPELL_UNUSED275:
    case SPELL_UNUSED285:
    case SPELL_BLADES:
    case SPELL_I_DARKNESS:
      return FALSE;
  }
  return TRUE;
}

/* simple function to give a random metal type */
/* (currently unused) */
int choose_metal_material(void) {  
  switch (dice(1, 12)) {
    case 1:
    case 2:
    case 3:
      return MATERIAL_BRONZE;
    case 4:
    case 5:
      return MATERIAL_STEEL;
    case 6:
      return MATERIAL_ALCHEMAL_SILVER;
    case 7:
      return MATERIAL_COLD_IRON;
    case 8:
      return MATERIAL_ADAMANTINE;
    case 9:
      return MATERIAL_MITHRIL;
    default:
      return MATERIAL_IRON;    
  }
}

/* simple function to give a random precious metal type */
/* (currently unused) */
int choose_precious_metal_material(void) {
  switch (dice(1, 9)) {
    case 1:
    case 2:
      return MATERIAL_BRASS;
    case 3:
    case 4:
      return MATERIAL_SILVER;
    case 5:
      return MATERIAL_GOLD;
    case 6:
      return MATERIAL_PLATINUM;
    default:
      return MATERIAL_COPPER;    
  }
}

/* simple function to give a random cloth type */
/* (currently unused) */
int choose_cloth_material(void) {
  switch (dice(1, 12)) {
    case 1:
    case 2:
    case 3:
      return MATERIAL_COTTON;
    case 4:
    case 5:
      return MATERIAL_WOOL;
    case 6:
      return MATERIAL_BURLAP;
    case 7:
      return MATERIAL_VELVET;
    case 8:
      return MATERIAL_SATIN;
    case 9:
      return MATERIAL_SILK;
    default:
      return MATERIAL_HEMP;    
  }
}

/* returns random apply value 
   from a list of values staff approved */
int random_apply_value(void) {
  int val = APPLY_NONE;

  switch (dice(1, 14)) {
    case 1:
      val = APPLY_AC;
      break;
    case 2:
      val = APPLY_HITROLL;
      break;
    case 3:
      val = APPLY_DAMROLL;
      break;
    case 4:
      val = APPLY_STR;
      break;
    case 5:
      val = APPLY_CON;
      break;
    case 6:
      val = APPLY_DEX;
      break;
    case 7:
      val = APPLY_INT;
      break;
    case 8:
      val = APPLY_WIS;
      break;
    case 9:
      val = APPLY_CHA;
      break;
    case 10:
      val = APPLY_MOVE;
      break;
    case 11:
      val = APPLY_SAVING_FORT;
      break;
    case 12:
      val = APPLY_SAVING_REFL;
      break;
    case 13:
      val = APPLY_SAVING_WILL;
      break;
    case 14:
      val = APPLY_HIT;
      break;
  }

  return val;
}

/* added this because the apply_X bonus is capped, stop it before
   it causes problems */
#define RANDOM_BONUS_CAP  127
/* function that returns bonus value based on apply-value and level */
/* TODO:  probably can merge this with crystal_bonus fuction in craft.c */
int random_bonus_value(int apply_value, int level) {
  int bonus = level / BONUS_FACTOR;

  switch (apply_value) {
    case APPLY_HIT:
      bonus *= 12;
      break;
    case APPLY_AC:
      bonus *= -5;
      break;
    case APPLY_HITROLL:
    case APPLY_DAMROLL:
      bonus += 2;
      break;
    case APPLY_MOVE:
      bonus *= 12;
      break;
      /* no modifications */
    case APPLY_STR:
    case APPLY_CON:
    case APPLY_DEX:
    case APPLY_INT:
    case APPLY_WIS:
    case APPLY_CHA:
    case APPLY_SAVING_FORT:
    case APPLY_SAVING_REFL:
    case APPLY_SAVING_WILL:
      break;
  }

  return MIN(RANDOM_BONUS_CAP, bonus);
}

/* when groupped, determine random recipient from group */
struct char_data *find_treasure_recipient(struct char_data *ch) {
  struct group_data *group = NULL;

  /* assign group data */
  if ((group = ch->group) == NULL)
    return ch;

  return ((struct char_data *) (random_from_list(group->members)));
}


/***************************************************************/
/***  primary functions ***/

/* this function determines whether the character will get treasure or not
 *   for example, called before make_corpse() when killing a mobile */
void determine_treasure(struct char_data *ch, struct char_data *mob) {
  int roll = dice(1, 100);
  int gold = 0;
  int level = 0;
  char buf[MEDIUM_STRING] = {'\0'};
  int grade = GRADE_MUNDANE;

  if (IS_NPC(ch))
    return;
  if (!IS_NPC(mob))
    return;

  gold = dice(1, GET_LEVEL(mob)) * 10;
  level = GET_LEVEL(mob);

  if (level >= 20) {
    grade = GRADE_MAJOR;
  } else if (level >= 16) {
    if (roll >= 61)
      grade = GRADE_MAJOR;
    else
      grade = GRADE_MEDIUM;
  } else if (level >= 12) {
    if (roll >= 81)
      grade = GRADE_MAJOR;
    else if (roll >= 11)
      grade = GRADE_MEDIUM;
    else
      grade = GRADE_MINOR;
  } else if (level >= 8) {
    if (roll >= 96)
      grade = GRADE_MAJOR;
    else if (roll >= 31)
      grade = GRADE_MEDIUM;
    else
      grade = GRADE_MINOR;
  } else if (level >= 4) {
    if (roll >= 76)
      grade = GRADE_MEDIUM;
    else if (roll >= 16)
      grade = GRADE_MINOR;
    else
      grade = GRADE_MUNDANE;
  } else {
    if (roll >= 96)
      grade = GRADE_MEDIUM;
    else if (roll >= 41)
      grade = GRADE_MINOR;
    else
      grade = GRADE_MUNDANE;
  }

  if (dice(1, 100) <= TREASURE_PERCENT) {
    award_magic_item(1, ch, level, grade);
    sprintf(buf, "\tYYou have found %d coins on $N's corpse!\tn", gold);
    act(buf, FALSE, ch, 0, mob, TO_CHAR);
    sprintf(buf, "$n \tYhas has found %d coins on $N's corpse!\tn", gold);
    act(buf, FALSE, ch, 0, mob, TO_NOTVICT);
    GET_GOLD(ch) += gold;
    /* does not split this gold, maybe change later */
  }

}

/* character should get treasure, roll dice for what items to give out */
void award_magic_item(int number, struct char_data *ch, int level, int grade) {
  int i = 0;

  for (i = 0; i < number; i++) {
    if (dice(1, 100) <= 40)
      award_expendable_item(ch, grade, TYPE_SCROLL);
    if (dice(1, 100) <= 30)
      award_expendable_item(ch, grade, TYPE_POTION);
    if (dice(1, 100) <= 20)
      award_expendable_item(ch, grade, TYPE_WAND);
    if (dice(1, 100) <= 10)
      award_expendable_item(ch, grade, TYPE_STAFF);
    if (dice(1, 100) <= 20)
      award_misc_magic_item(ch, grade, level);
    if (dice(1, 100) <= 20)
      award_magic_armor(ch, grade, level);
    if (dice(1, 100) <= 10)
      award_magic_weapon(ch, grade, level);
    if (dice(1, 100) <= 5)
      award_random_crystal(ch, level);
  }
}

/* function to create a random crystal */
void award_random_crystal(struct char_data *ch, int level) {
  int color1 = -1, color2 = -1, desc = -1, roll = 0;
  struct obj_data *obj = NULL;
  char buf[MEDIUM_STRING] = {'\0'};

  if ((obj = read_object(CRYSTAL_PROTOTYPE, VIRTUAL)) == NULL) {
    log("SYSERR:  get_random_crystal read_object returned NULL");
    return;
  }

  /* this is just to make sure the item is set correctly */
  GET_OBJ_TYPE(obj) = ITEM_CRYSTAL;
  GET_OBJ_COST(obj) = level * 100;
  GET_OBJ_LEVEL(obj) = MIN(LVL_IMMORT - 1, dice(1, level));
  GET_OBJ_MATERIAL(obj) = MATERIAL_CRYSTAL;

  /* set a random apply value */
  obj->affected[0].location = random_apply_value();
  /* determine bonus */
  /* this is deprecated, level determines modifier now (in craft.c) */
  obj->affected[0].modifier =
          random_bonus_value(obj->affected[0].location, GET_OBJ_LEVEL(obj));

  /* random color(s) and description */
  color1 = rand_number(0, NUM_A_COLORS);
  color2 = rand_number(0, NUM_A_COLORS);
  while (color2 == color1)
    color2 = rand_number(0, NUM_A_COLORS);
  desc = rand_number(0, NUM_A_CRYSTAL_DESCS);

  roll = dice(1, 100);

  // two colors and descriptor
  if (roll >= 91) {
    sprintf(buf, "crystal %s %s %s", colors[color1], colors[color2],
            crystal_descs[desc]);
    obj->name = strdup(buf);
    sprintf(buf, "a  %s, %s and %s crystal", crystal_descs[desc],
            colors[color1], colors[color2]);
    obj->short_description = strdup(buf);
    sprintf(buf, "A %s, %s and %s crystal lies here.", crystal_descs[desc],
            colors[color1], colors[color2]);
    obj->description = strdup(buf);

    // one color and descriptor
  } else if (roll >= 66) {
    sprintf(buf, "crystal %s %s", colors[color1], crystal_descs[desc]);
    obj->name = strdup(buf);
    sprintf(buf, "a %s %s crystal", crystal_descs[desc], colors[color1]);
    obj->short_description = strdup(buf);
    sprintf(buf, "A %s %s crystal lies here.", crystal_descs[desc],
            colors[color1]);
    obj->description = strdup(buf);

    // two colors no descriptor
  } else if (roll >= 41) {
    sprintf(buf, "crystal %s %s", colors[color1], colors[color2]);
    obj->name = strdup(buf);
    sprintf(buf, "a %s and %s crystal", colors[color1], colors[color2]);
    obj->short_description = strdup(buf);
    sprintf(buf, "A %s and %s crystal lies here.", colors[color1], colors[color2]);
    obj->description = strdup(buf);
  } else if (roll >= 21) {// one color no descriptor
    sprintf(buf, "crystal %s", colors[color1]);
    obj->name = strdup(buf);
    sprintf(buf, "a %s crystal", colors[color1]);
    obj->short_description = strdup(buf);
    sprintf(buf, "A %s crystal lies here.", colors[color1]);
    obj->description = strdup(buf);

    // descriptor only  
  } else {
    sprintf(buf, "crystal %s", crystal_descs[desc]);
    obj->name = strdup(buf);
    sprintf(buf, "a %s crystal", crystal_descs[desc]);
    obj->short_description = strdup(buf);
    sprintf(buf, "A %s crystal lies here.", crystal_descs[desc]);
    obj->description = strdup(buf);
  }

  obj_to_char(obj, ch);
}

/* awards potions or scroll or wand or staff */

/* reminder, stock:
   obj value 0:  spell level
   obj value 1:  potion/scroll - spell #1; staff/wand - max charges
   obj value 2:  potion/scroll - spell #2; staff/wand - charges remaining
   obj value 3:  potion/scroll - spell #3; staff/wand - spell #1
 */
void award_expendable_item(struct char_data *ch, int grade, int type) {
  int class = CLASS_UNDEFINED, spell_level = 1, spell_num = SPELL_RESERVED_DBC;
  int color1 = 0, color2 = 0, desc = 0, roll = dice(1, 100), i = 0;
  struct obj_data *obj = NULL;
  char keywords[100] = {'\0'}, buf[MAX_STRING_LENGTH] = {'\0'};

  /* first determine which class the scroll will be,
   then determine what level spell the scroll will be */
  switch (rand_number(0, NUM_CLASSES - 1)) {
    case CLASS_CLERIC:
      class = CLASS_CLERIC;
      break;
    case CLASS_DRUID:
      class = CLASS_DRUID;
      break;
    case CLASS_SORCERER:
      class = CLASS_SORCERER;
      break;
    case CLASS_PALADIN:
      class = CLASS_PALADIN;
      break;
    case CLASS_RANGER:
      class = CLASS_RANGER;
      break;
    case CLASS_BARD:
      class = CLASS_BARD;
      break;
    default:
      class = CLASS_WIZARD;
      break;
  }

  /* determine level */
  /* -note- that this is caster level, not spell-circle */
  switch (grade) {
    case GRADE_MUNDANE:
      spell_level = rand_number(1, 5);
      break;
    case GRADE_MINOR:
      spell_level = rand_number(6, 10);
      break;
    case GRADE_MEDIUM:
      spell_level = rand_number(11, 14);
      break;
    default:
      spell_level = rand_number(15, 20);
      break;
  }

  /* Loop through spell list randomly! conditions of exit:
     - invalid spell (sub-function)
     - does not match class
     - does not match level
   */
  do {
    spell_num = rand_number(1, NUM_SPELLS - 1);
  } while (spell_level < spell_info[spell_num].min_level[class] ||
          !valid_item_spell(spell_num));

  /* first assign two random colors for usage */
  color1 = rand_number(0, NUM_A_COLORS);
  color2 = rand_number(0, NUM_A_COLORS);
  /* make sure they are not the same colors */
  while (color2 == color1)
    color2 = rand_number(0, NUM_A_COLORS);

  /* load prototype */
  
  if ((obj = read_object(ITEM_PROTOTYPE, VIRTUAL)) == NULL) {
    log("SYSERR:  award_expendable_item returned NULL");
    return;
  }

  switch (type) {
    case TYPE_POTION:
      /* assign a description (potions only) */
      desc = rand_number(0, NUM_A_POTION_DESCS);

      // two colors and descriptor
      if (roll >= 91) {
        sprintf(keywords, "potion-%s", spell_info[spell_num].name);
        for (i = 0; i < strlen(keywords); i++)
          if (keywords[i] == ' ')
            keywords[i] = '-';
        sprintf(buf, "vial potion %s %s %s %s %s", colors[color1], colors[color2],
                potion_descs[desc], spell_info[spell_num].name, keywords);
        obj->name = strdup(buf);
        sprintf(buf, "a glass vial filled with a %s, %s and %s liquid",
                potion_descs[desc], colors[color1], colors[color2]);
        obj->short_description = strdup(buf);
        sprintf(buf, "A glass vial filled with a %s, %s and %s liquid lies here.",
                potion_descs[desc], colors[color1], colors[color2]);
        obj->description = strdup(buf);

        // one color and descriptor        
      } else if (roll >= 66) {
        sprintf(keywords, "potion-%s", spell_info[spell_num].name);
        for (i = 0; i < strlen(keywords); i++)
          if (keywords[i] == ' ')
            keywords[i] = '-';
        sprintf(buf, "vial potion %s %s %s %s", colors[color1],
                potion_descs[desc], spell_info[spell_num].name, keywords);
        obj->name = strdup(buf);
        sprintf(buf, "a glass vial filled with a %s %s liquid",
                potion_descs[desc], colors[color1]);
        obj->short_description = strdup(buf);
        sprintf(buf, "A glass vial filled with a %s and %s liquid lies here.",
                potion_descs[desc], colors[color1]);
        obj->description = strdup(buf);

        // two colors no descriptor
      } else if (roll >= 41) {
        sprintf(keywords, "potion-%s", spell_info[spell_num].name);
        for (i = 0; i < strlen(keywords); i++)
          if (keywords[i] == ' ')
            keywords[i] = '-';
        sprintf(buf, "vial potion %s %s %s %s", colors[color1], colors[color2],
                spell_info[spell_num].name, keywords);
        obj->name = strdup(buf);
        sprintf(buf, "a glass vial filled with a %s and %s liquid", colors[color1],
                colors[color2]);
        obj->short_description = strdup(buf);
        sprintf(buf, "A glass vial filled with a %s and %s liquid lies here.",
                colors[color1], colors[color2]);
        obj->description = strdup(buf);

        // one color no descriptor
      } else {
        sprintf(keywords, "potion-%s", spell_info[spell_num].name);
        for (i = 0; i < strlen(keywords); i++)
          if (keywords[i] == ' ')
            keywords[i] = '-';
        sprintf(buf, "vial potion %s %s %s", colors[color1],
                spell_info[spell_num].name, keywords);
        obj->name = strdup(buf);
        sprintf(buf, "a glass vial filled with a %s liquid",
                colors[color1]);
        obj->short_description = strdup(buf);
        sprintf(buf, "A glass vial filled with a %s liquid lies here.",
                colors[color1]);
        obj->description = strdup(buf);
      }

      GET_OBJ_VAL(obj, 0) = spell_level;
      GET_OBJ_VAL(obj, 1) = spell_num;

      GET_OBJ_MATERIAL(obj) = MATERIAL_GLASS;
      GET_OBJ_COST(obj) = MIN(1000, 5 * spell_level);
      GET_OBJ_LEVEL(obj) = dice(1, spell_level);
      break;

    case TYPE_WAND:
      sprintf(keywords, "wand-%s", spell_info[spell_num].name);
      for (i = 0; i < strlen(keywords); i++)
        if (keywords[i] == ' ')
          keywords[i] = '-';

      sprintf(buf, "wand wooden runes %s %s %s", colors[color1],
              spell_info[spell_num].name, keywords);
      obj->name = strdup(buf);
      sprintf(buf, "a wooden wand covered in %s runes", colors[color1]);
      obj->short_description = strdup(buf);
      sprintf(buf, "A wooden wand covered in %s runes lies here.", colors[color1]);
      obj->description = strdup(buf);

      GET_OBJ_VAL(obj, 0) = spell_level;
      GET_OBJ_VAL(obj, 1) = dice(1, 10);
      GET_OBJ_VAL(obj, 2) = 10;
      GET_OBJ_VAL(obj, 3) = spell_num;

      GET_OBJ_COST(obj) = MIN(1000, 50 * spell_level);
      GET_OBJ_MATERIAL(obj) = MATERIAL_WOOD;
      GET_OBJ_TYPE(obj) = ITEM_WAND;
      SET_BIT_AR(GET_OBJ_WEAR(obj), ITEM_WEAR_HOLD);
      GET_OBJ_LEVEL(obj) = dice(1, spell_level);
      break;

    case TYPE_STAFF:
      sprintf(keywords, "staff-%s", spell_info[spell_num].name);
      for (i = 0; i < strlen(keywords); i++)
        if (keywords[i] == ' ')
          keywords[i] = '-';

      sprintf(buf, "staff wooden runes %s %s %s", colors[color1],
              spell_info[spell_num].name, keywords);
      obj->name = strdup(buf);
      sprintf(buf, "a wooden staff covered in %s runes", colors[color1]);
      obj->short_description = strdup(buf);
      sprintf(buf, "A wooden staff covered in %s runes lies here.",
              colors[color1]);
      obj->description = strdup(buf);

      GET_OBJ_VAL(obj, 0) = spell_level;
      GET_OBJ_VAL(obj, 1) = dice(1, 8);
      GET_OBJ_VAL(obj, 2) = 8;
      GET_OBJ_VAL(obj, 3) = spell_num;

      GET_OBJ_COST(obj) = MIN(1000, 110 * spell_level);
      GET_OBJ_MATERIAL(obj) = MATERIAL_WOOD;
      GET_OBJ_TYPE(obj) = ITEM_STAFF;
      SET_BIT_AR(GET_OBJ_WEAR(obj), ITEM_WEAR_HOLD);
      GET_OBJ_LEVEL(obj) = dice(1, spell_level);
      break;

    default: // Type-Scrolls
      sprintf(keywords, "scroll-%s", spell_info[spell_num].name);
      for (i = 0; i < strlen(keywords); i++)
        if (keywords[i] == ' ')
          keywords[i] = '-';

      sprintf(buf, "scroll ink %s %s %s", colors[color1],
              spell_info[spell_num].name, keywords);
      obj->name = strdup(buf);
      sprintf(buf, "a scroll written in %s ink", colors[color1]);
      obj->short_description = strdup(buf);
      sprintf(buf, "A scroll written in %s ink lies here.", colors[color1]);
      obj->description = strdup(buf);

      GET_OBJ_VAL(obj, 0) = spell_level;
      GET_OBJ_VAL(obj, 1) = spell_num;

      GET_OBJ_COST(obj) = MIN(1000, 10 * spell_level);
      GET_OBJ_MATERIAL(obj) = MATERIAL_PAPER;
      GET_OBJ_TYPE(obj) = ITEM_SCROLL;
      GET_OBJ_LEVEL(obj) = dice(1, spell_level);
      break;
  }

  REMOVE_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MOLD);
  SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MAGIC);

  obj_to_char(obj, ch);

  send_to_char(ch, "\tYYou have found %s in a nearby lair!\tn\r\n", obj->short_description);
  sprintf(buf, "$n \tYhas found %s in a nearby lair!\tn", obj->short_description);
  act(buf, FALSE, ch, 0, ch, TO_NOTVICT);
}

/* give away random magic armor */

/*
 method:
 * 1)  determine armor
 * 2)  determine material
 * 3)  assign description
 * 4)  determine modifier (if applicable)
 * 5)  determine amount (if applicable)
 */
void award_magic_armor(struct char_data *ch, int grade, int moblevel) {
  struct obj_data *obj = NULL;
  int vnum = -1, material = MATERIAL_BRONZE, roll = 0, crest_num = 0;
  int rare_grade = 0, color1 = 0, color2 = 0, level = 0;
  char desc[MEDIUM_STRING] = {'\0'}, armor_name[MEDIUM_STRING] = {'\0'};
  char buf[MAX_STRING_LENGTH] = {'\0'}, keywords[MEDIUM_STRING] = {'\0'};


  /* determine if rare or not */
  roll = dice(1, 100);
  if (roll == 1) {
    rare_grade = 3;
    sprintf(desc, "\tM[Mythical]\tn ");
  } else if (roll <= 6) {
    rare_grade = 2;
    sprintf(desc, "\tY[Legendary]\tn ");
  } else if (roll <= 16) {
    rare_grade = 1;
    sprintf(desc, "\tG[Rare]\tn ");
  }

  /* find a random piece of armor
   * assign base material
   * and last but not least, give appropriate start of description
   *  */
  switch (dice(1, NUM_ARMOR_MOLDS)) {

      /* body pieces */
    case 1:
      vnum = PLATE_BODY;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa suit of", desc);
      sprintf(armor_name, "plate mail armor");
      break;
    case 2:
      vnum = HALFPLATE_BODY;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa suit of", desc);
      sprintf(armor_name, "half plate armor");
      break;
    case 3:
      vnum = SPLINT_BODY;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa suit of", desc);
      sprintf(armor_name, "splint mail armor");
      break;
    case 4:
      vnum = BREASTPLATE_BODY;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa suit of", desc);
      sprintf(armor_name, "breastplate armor");
      break;
    case 5:
      vnum = CHAIN_BODY;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa suit of", desc);
      sprintf(armor_name, "chain mail armor");
      break;
    case 6:
      vnum = STUD_LEATHER_BODY;
      material = MATERIAL_LEATHER;
      sprintf(desc, "%sa suit of", desc);
      sprintf(armor_name, "studded leather armor");
      break;
    case 7:
      vnum = LEATHER_BODY;
      material = MATERIAL_LEATHER;
      sprintf(desc, "%sa suit of", desc);
      sprintf(armor_name, "leather armor");
      break;
    case 8:
      vnum = PADDED_BODY;
      material = MATERIAL_COTTON;
      sprintf(desc, "%sa suit of", desc);
      sprintf(armor_name, "padded armor");
      break;
    case 9:
      vnum = CLOTH_BODY;
      material = MATERIAL_COTTON;
      sprintf(desc, "%ssome", desc);
      sprintf(armor_name, "robes");
      break;

      /* head pieces */
    case 10:
      vnum = PLATE_HELM;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "plate mail helm");
      break;
    case 11:
      vnum = HALFPLATE_HELM;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "half plate helm");
      break;
    case 12:
      vnum = SPLINT_HELM;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "splint mail helm");
      break;
    case 13:
      vnum = PIECEPLATE_HELM;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "piece plate helm");
      break;
    case 14:
      vnum = CHAIN_HELM;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "chain mail helm");
      break;
    case 15:
      vnum = STUD_LEATHER_HELM;
      material = MATERIAL_LEATHER;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "studded leather helm");
      break;
    case 16:
      vnum = LEATHER_HELM;
      material = MATERIAL_LEATHER;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "leather helm");
      break;
    case 17:
      vnum = PADDED_HELM;
      material = MATERIAL_COTTON;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "padded armor helm");
      break;
    case 18:
      vnum = CLOTH_HELM;
      material = MATERIAL_COTTON;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "cloth hood");
      break;

      /* arm pieces */
    case 19:
      vnum = PLATE_ARMS;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "plate mail vambraces");
      break;
    case 20:
      vnum = HALFPLATE_ARMS;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "half plate vambraces");
      break;
    case 21:
      vnum = SPLINT_ARMS;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "splint mail vambraces");
      break;
    case 22:
      vnum = CHAIN_ARMS;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "chain mail sleeves");
      break;
    case 23:
      vnum = STUD_LEATHER_ARMS;
      material = MATERIAL_LEATHER;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "studded leather sleeves");
      break;
    case 24:
      vnum = LEATHER_ARMS;
      material = MATERIAL_LEATHER;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "leather sleeves");
      break;
    case 25:
      vnum = PADDED_ARMS;
      material = MATERIAL_COTTON;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "padded armor sleeves");
      break;
    case 26:
      vnum = CLOTH_ARMS;
      material = MATERIAL_COTTON;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "cloth sleeves");
      break;

      /* leg pieces */
    case 27:
      vnum = PLATE_LEGS;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "plate mail greaves");
      break;
    case 28:
      vnum = HALFPLATE_LEGS;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "half plate greaves");
      break;
    case 29:
      vnum = SPLINT_LEGS;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "splint mail greaves");
      break;
    case 30:
      vnum = CHAIN_LEGS;
      material = MATERIAL_BRONZE;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "chain mail leggings");
      break;
    case 31:
      vnum = STUD_LEATHER_LEGS;
      material = MATERIAL_LEATHER;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "studded leather leggings");
      break;
    case 32:
      vnum = LEATHER_LEGS;
      material = MATERIAL_LEATHER;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "leather leggings");
      break;
    case 33:
      vnum = PADDED_LEGS;
      material = MATERIAL_COTTON;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "padded armor leggings");
      break;
    case 34:
      vnum = CLOTH_LEGS;
      material = MATERIAL_COTTON;
      sprintf(desc, "%sa set of", desc);
      sprintf(armor_name, "cloth pants");
      break;

      /* shields */
    case 35:
      vnum = SHIELD_MEDIUM;
      material = MATERIAL_WOOD;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "medium shield");
      break;
    case 36:
      vnum = SHIELD_LARGE;
      material = MATERIAL_WOOD;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "large shield");
      break;
    case 37:
      vnum = SHIELD_TOWER;
      material = MATERIAL_WOOD;
      sprintf(desc, "%sa", desc);
      sprintf(armor_name, "tower shield");
      break;
  }

  /* we already determined 'base' material, now
   determine whether an upgrade was achieved by item-grade */
  roll = dice(1, 100);
  switch (material) {
    case MATERIAL_BRONZE:
      switch (grade) {
        case GRADE_MUNDANE:
          if (roll <= 75)
            material = MATERIAL_BRONZE;
          else
            material = MATERIAL_IRON;
          break;
        case GRADE_MINOR:
          if (roll <= 75)
            material = MATERIAL_IRON;
          else
            material = MATERIAL_STEEL;
          break;
        case GRADE_MEDIUM:
          if (roll <= 50)
            material = MATERIAL_IRON;
          else if (roll <= 80)
            material = MATERIAL_STEEL;
          else if (roll <= 95)
            material = MATERIAL_COLD_IRON;
          else
            material = MATERIAL_ALCHEMAL_SILVER;
          break;
        default: // major grade
          if (roll <= 50)
            material = MATERIAL_COLD_IRON;
          else if (roll <= 80)
            material = MATERIAL_ALCHEMAL_SILVER;
          else if (roll <= 95)
            material = MATERIAL_MITHRIL;
          else
            material = MATERIAL_ADAMANTINE;
          break;
      }
      break;
    case MATERIAL_LEATHER:
      switch (grade) {
        case GRADE_MUNDANE:
          material = MATERIAL_LEATHER;
          break;
        case GRADE_MINOR:
          material = MATERIAL_LEATHER;
          break;
        case GRADE_MEDIUM:
          material = MATERIAL_LEATHER;
          break;
        default: // major grade
          if (roll <= 90)
            material = MATERIAL_LEATHER;
          else
            material = MATERIAL_DRAGONHIDE;
          break;
      }
      break;
    case MATERIAL_COTTON:
      switch (grade) {
        case GRADE_MUNDANE:
          if (roll <= 75)
            material = MATERIAL_HEMP;
          else
            material = MATERIAL_COTTON;
          break;
        case GRADE_MINOR:
          if (roll <= 50)
            material = MATERIAL_HEMP;
          else if (roll <= 80)
            material = MATERIAL_COTTON;
          else
            material = MATERIAL_WOOL;
          break;
        case GRADE_MEDIUM:
          if (roll <= 50)
            material = MATERIAL_COTTON;
          else if (roll <= 80)
            material = MATERIAL_WOOL;
          else if (roll <= 95)
            material = MATERIAL_VELVET;
          else
            material = MATERIAL_SATIN;
          break;
        default: // major grade
          if (roll <= 50)
            material = MATERIAL_WOOL;
          else if (roll <= 80)
            material = MATERIAL_VELVET;
          else if (roll <= 95)
            material = MATERIAL_SATIN;
          else
            material = MATERIAL_SILK;
          break;
      }
      break;
    case MATERIAL_WOOD:
      switch (grade) {
        case GRADE_MUNDANE:
        case GRADE_MINOR:
        case GRADE_MEDIUM:
          material = MATERIAL_WOOD;
          break;
        default: // major grade
          if (roll <= 80)
            material = MATERIAL_WOOD;
          else
            material = MATERIAL_DARKWOOD;
          break;
      }
      break;
  }

  /* determine level */
  switch (grade) {
    case GRADE_MUNDANE:
      level = rand_number(1, 8);
      break;
    case GRADE_MINOR:
      level = rand_number(9, 16);
      break;
    case GRADE_MEDIUM:
      level = rand_number(17, 24);
      break;
    default: // major grade
      level = rand_number(25, 30);
      break;
  }

  /* ok load object, set material */
  if ((obj = read_object(vnum, VIRTUAL)) == NULL) {
    log("SYSERR: award_magic_armor created NULL object");
    return;
  }
  GET_OBJ_MATERIAL(obj) = material;

  /* first assign two random colors for usage */
  color1 = rand_number(0, NUM_A_COLORS);
  color2 = rand_number(0, NUM_A_COLORS);
  /* make sure they are not the same colors */
  while (color2 == color1)
    color2 = rand_number(0, NUM_A_COLORS);
  crest_num = rand_number(0, NUM_A_ARMOR_CRESTS);

  /* start with keyword string */
  sprintf(keywords, "%s %s", keywords, armor_name);
  sprintf(keywords, "%s %s", keywords, material_name[material]);
  
  roll = dice(1, 3);  
  if (roll == 3) {  // armor spec adjective in desc?
    sprintf(desc, "%s %s", desc,
          armor_special_descs[rand_number(0, NUM_A_ARMOR_SPECIAL_DESCS)]);
    sprintf(keywords, "%s %s", keywords,
          armor_special_descs[rand_number(0, NUM_A_ARMOR_SPECIAL_DESCS)]);
  }

  roll = dice(1, 5);
  if (roll >= 4) {  // color describe #1?
    sprintf(desc, "%s %s", desc, colors[color1]);
    sprintf(keywords, "%s %s", keywords, colors[color1]);
  } else if (roll == 3) {  // two colors
    sprintf(desc, "%s %s and %s", desc, colors[color1], colors[color2]);
    sprintf(keywords, "%s %s and %s", keywords, colors[color1], colors[color2]);
  }

  // Insert the material type, then armor type
  sprintf(desc, "%s %s", desc, material_name[material]);
  sprintf(desc, "%s %s", desc, armor_name);

  roll = dice(1, 8);
  if (roll >= 7) {  // crest?
    sprintf(desc, "%s with %s %s crest", desc,
          AN(armor_crests[crest_num]),
          armor_crests[crest_num]);
    sprintf(keywords, "%s with %s %s crest", keywords,
          AN(armor_crests[crest_num]),
          armor_crests[crest_num]);
  } else if (roll >= 5) {  // or symbol?
    sprintf(desc, "%s covered in symbols of %s %s", desc,
          AN(armor_crests[crest_num]),
          armor_crests[crest_num]);
    sprintf(keywords, "%s covered in symbols of %s %s", keywords,
          AN(armor_crests[crest_num]),
          armor_crests[crest_num]);
  }

  // keywords
  obj->name = strdup(keywords);
  // Set descriptions  
  obj->short_description = strdup(desc);
  desc[0] = toupper(desc[0]);
  sprintf(desc, "%s is lying here.", desc);
  obj->description = strdup(desc);

  /* level, bonus and cost */
  GET_OBJ_LEVEL(obj) = level;
  obj->affected[0].location = random_apply_value();
  obj->affected[0].modifier =
          random_bonus_value(obj->affected[0].location, level + (rare_grade * BONUS_FACTOR));
  GET_OBJ_COST(obj) = GET_OBJ_LEVEL(obj) * 100;

  REMOVE_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MOLD);
  if (grade > GRADE_MUNDANE)
    SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MAGIC);

  obj_to_char(obj, ch);

  send_to_char(ch, "\tYYou have found %s in a nearby lair!\tn\r\n", obj->short_description);
  sprintf(buf, "$n \tYhas found %s in a nearby lair!\tn", obj->short_description);
  act(buf, FALSE, ch, 0, ch, TO_NOTVICT);
}

/* give away random magic weapon */

/*
 method:
 * 1)  determine weapon
 * 2)  determine material
 * 3)  assign description
 * 4)  determine modifier (if applicable)
 * 5)  determine amount (if applicable)
 */
#define SHORT_STRING 80

void award_magic_weapon(struct char_data *ch, int grade, int moblevel) {
  struct obj_data *obj = NULL;
  int vnum = -1, material = MATERIAL_BRONZE, roll = 0, size = SIZE_MEDIUM;
  int rare_grade = 0, color1 = 0, color2 = 0, level = 0, roll2 = 0, roll3 = 0;
  char desc[MEDIUM_STRING] = {'\0'}, weapon_name[SHORT_STRING] = {'\0'};
  char hilt_color[SHORT_STRING] = {'\0'}, head_color[SHORT_STRING] = {'\0'};
  char special[SHORT_STRING] = {'\0'};
  char buf[MAX_STRING_LENGTH] = {'\0'};


  /* determine if rare or not */
  roll = dice(1, 100);
  if (roll == 1) {
    rare_grade = 3;
    sprintf(desc, "\tM[Mythical] \tn");
  } else if (roll <= 6) {
    rare_grade = 2;
    sprintf(desc, "\tY[Legendary] \tn");
  } else if (roll <= 16) {
    rare_grade = 1;
    sprintf(desc, "\tG[Rare] \tn");
  }

  /* find a random weapon
   * assign base material
   * assign size
   * and last but not least, give appropriate start of description
   *  */
  switch (dice(1, NUM_WEAPON_MOLDS)) {

      /* simple */
      /* light */
    case 1:
      vnum = DAGGER;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "dagger");
      size = SIZE_SMALL;
      break;
    case 2:
      vnum = MACE;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "mace");
      size = SIZE_SMALL;
      break;
    case 3:
      vnum = SICKLE;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "sickle");
      size = SIZE_SMALL;
      break;
      /* one handed */
    case 4:
      vnum = CLUB;
      material = MATERIAL_WOOD;
      sprintf(weapon_name, "club");
      size = SIZE_MEDIUM;
      break;
    case 5:
      vnum = MORNINGSTAR;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "morning star");
      size = SIZE_MEDIUM;
      break;
      /* two handed */
    case 6:
      vnum = SPEAR;
      material = MATERIAL_WOOD;
      sprintf(weapon_name, "spear");
      size = SIZE_LARGE;
      break;
    case 7:
      vnum = QUARTERSTAFF;
      material = MATERIAL_WOOD;
      sprintf(weapon_name, "quarterstaff");
      size = SIZE_LARGE;
      break;
      /* martial */
      /* light */
    case 8:
      vnum = HANDAXE;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "hand axe");
      size = SIZE_SMALL;
      break;
    case 9:
      vnum = KUKRI;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "kukri");
      size = SIZE_SMALL;
      break;
    case 10:
      vnum = SHORTSWORD;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "short sword");
      size = SIZE_SMALL;
      break;
      /* one handed */
    case 11:
      vnum = BATTLEAXE;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "battle axe");
      size = SIZE_MEDIUM;
      break;
    case 12:
      vnum = FLAIL;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "flail");
      size = SIZE_MEDIUM;
      break;
    case 13:
      vnum = LONGSWORD;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "long sword");
      size = SIZE_MEDIUM;
      break;
    case 14:
      vnum = RAPIER;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "rapier");
      size = SIZE_MEDIUM;
      break;
    case 15:
      vnum = SCIMITAR;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "scimitar");
      size = SIZE_MEDIUM;
      break;
    case 16:
      vnum = TRIDENT;
      material = MATERIAL_WOOD;
      sprintf(weapon_name, "trident");
      size = SIZE_MEDIUM;
      break;
    case 17:
      vnum = WARHAMMER;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "war hammer");
      size = SIZE_MEDIUM;
      break;
      /* two handed */
    case 18:
      vnum = FALCHION;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "falchion");
      size = SIZE_LARGE;
      break;
    case 19:
      vnum = GLAIVE;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "glaive");
      size = SIZE_LARGE;
      break;
    case 20:
      vnum = GREATAXE;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "great axe");
      size = SIZE_LARGE;
      break;
    case 21:
      vnum = GREATCLUB;
      material = MATERIAL_WOOD;
      sprintf(weapon_name, "great club");
      size = SIZE_LARGE;
      break;
    case 22:
      vnum = GREATSWORD;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "great sword");
      size = SIZE_LARGE;
      break;
    case 23:
      vnum = HALBERD;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "halberd");
      size = SIZE_LARGE;
      break;
    case 24:
      vnum = LANCE;
      material = MATERIAL_WOOD;
      sprintf(weapon_name, "lance");
      size = SIZE_LARGE;
      break;
    case 25:
      vnum = SCYTHE;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "scythe");
      size = SIZE_LARGE;
      break;
      /* exotic */
      /* light */
    case 26:
      vnum = KAMA;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "kama");
      size = SIZE_SMALL;
      break;
      /* one handed */
    case 27:
      vnum = BASTARDSWORD;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "bastard sword");
      size = SIZE_MEDIUM;
      break;
    case 28:
      vnum = DWARVENWARAXE;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "dwarven war axe");
      size = SIZE_MEDIUM;
      break;
      /* two handed */
    case 29:
      vnum = DIREFLAIL;
      material = MATERIAL_BRONZE;
      sprintf(weapon_name, "dire flail");
      size = SIZE_LARGE;
      break;
  }

  /* we already determined 'base' material, now
   determine whether an upgrade was achieved by item-grade */
  roll = dice(1, 100);
  switch (material) {
    case MATERIAL_BRONZE:
      switch (grade) {
        case GRADE_MUNDANE:
          if (roll <= 75)
            material = MATERIAL_BRONZE;
          else
            material = MATERIAL_IRON;
          break;
        case GRADE_MINOR:
          if (roll <= 75)
            material = MATERIAL_IRON;
          else
            material = MATERIAL_STEEL;
          break;
        case GRADE_MEDIUM:
          if (roll <= 50)
            material = MATERIAL_IRON;
          else if (roll <= 80)
            material = MATERIAL_STEEL;
          else if (roll <= 95)
            material = MATERIAL_COLD_IRON;
          else
            material = MATERIAL_ALCHEMAL_SILVER;
          break;
        default: // major grade
          if (roll <= 50)
            material = MATERIAL_COLD_IRON;
          else if (roll <= 80)
            material = MATERIAL_ALCHEMAL_SILVER;
          else if (roll <= 95)
            material = MATERIAL_MITHRIL;
          else
            material = MATERIAL_ADAMANTINE;
          break;
      }
      break;
    case MATERIAL_WOOD:
      switch (grade) {
        case GRADE_MUNDANE:
        case GRADE_MINOR:
        case GRADE_MEDIUM:
          material = MATERIAL_WOOD;
          break;
        default: // major grade
          if (roll <= 80)
            material = MATERIAL_WOOD;
          else
            material = MATERIAL_DARKWOOD;
          break;
      }
      break;
  }

  /* determine level */
  switch (grade) {
    case GRADE_MUNDANE:
      level = rand_number(1, 8);
      break;
    case GRADE_MINOR:
      level = rand_number(9, 16);
      break;
    case GRADE_MEDIUM:
      level = rand_number(17, 24);
      break;
    default: // major grade
      level = rand_number(25, 30);
      break;
  }

  /* ok load object, set material */
  if ((obj = read_object(vnum, VIRTUAL)) == NULL) {
    log("SYSERR: award_magic_armor created NULL object");
    return;
  }
  GET_OBJ_MATERIAL(obj) = material;

  // pick a pair of random colors for usage
  /* first assign two random colors for usage */
  color1 = rand_number(0, NUM_A_COLORS);
  color2 = rand_number(0, NUM_A_COLORS);
  /* make sure they are not the same colors */
  while (color2 == color1)
    color2 = rand_number(0, NUM_A_COLORS);

  sprintf(head_color, "%s", colors[color1]);
  sprintf(hilt_color, "%s", colors[color2]);
  if (IS_BLADE(obj))
    sprintf(special, "%s%s", desc, blade_descs[rand_number(0, NUM_A_BLADE_DESCS)]);
  else if (IS_PIERCE(obj))
    sprintf(special, "%s%s", desc, piercing_descs[rand_number(0, NUM_A_PIERCING_DESCS)]);
  else //blunt
    sprintf(special, "%s%s", desc, blunt_descs[rand_number(0, NUM_A_BLUNT_DESCS)]);
  
  roll = dice(1, 100);
  roll2 = rand_number(0, NUM_A_HEAD_TYPES);
  roll3 = rand_number(0, NUM_A_HANDLE_TYPES);

  // special, head color, hilt color
  if (roll >= 91) {
    sprintf(buf, "%s %s-%s %s %s %s %s", weapon_name,
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], special,
            hilt_color,
            handle_types[roll3]);
    obj->name = strdup(buf);
    sprintf(buf, "%s %s, %s-%s %s %s with %s %s %s", a_or_an(special), special,
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name,
            a_or_an(hilt_color), hilt_color,
            handle_types[roll3]);
    obj->short_description = strdup(buf);
    sprintf(buf, "%s %s, %s-%s %s %s with %s %s %s lies here.", a_or_an(special),
            special, head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name,
            a_or_an(hilt_color), hilt_color,
            handle_types[roll3]);
    *buf = UPPER(*buf);
    obj->description = strdup(buf);

  // special, head color
  } else if (roll >= 81) {
    sprintf(buf, "%s %s-%s %s %s", weapon_name,
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], special);
    obj->name = strdup(buf);
    sprintf(buf, "%s %s, %s-%s %s %s", a_or_an(special), special,
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name);
    obj->short_description = strdup(buf);
    sprintf(buf, "%s %s, %s-%s %s %s lies here.", a_or_an(special),
            special, head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name);
    *buf = UPPER(*buf);
    obj->description = strdup(buf);

  // special, hilt color
  } else if (roll >= 71) {
    sprintf(buf, "%s %s %s %s %s", weapon_name,
            material_name[GET_OBJ_MATERIAL(obj)], special, hilt_color,
            handle_types[roll3]);
    obj->name = strdup(buf);
    sprintf(buf, "%s %s %s %s with %s %s %s", a_or_an(special), special,
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name,
            a_or_an(hilt_color), hilt_color,
            handle_types[roll3]);
    obj->short_description = strdup(buf);
    sprintf(buf, "%s %s %s %s with %s %s %s lies here.", a_or_an(special),
            special, material_name[GET_OBJ_MATERIAL(obj)], weapon_name,
            a_or_an(hilt_color), hilt_color,
            handle_types[roll3]);
    *buf = UPPER(*buf);
    obj->description = strdup(buf);

  // head color, hilt color
  } else if (roll >= 41) {
    sprintf(buf, "%s %s-%s %s %s %s",
            weapon_name,head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)],
            hilt_color,
            handle_types[roll3]);
    obj->name = strdup(buf);
    sprintf(buf, "%s %s-%s %s %s with %s %s %s", a_or_an(head_color),
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name,
            a_or_an(hilt_color), hilt_color,
            handle_types[roll3]);
    obj->short_description = strdup(buf);
    sprintf(buf, "%s %s-%s %s %s with %s %s %s lies here.", a_or_an(head_color),
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name,
            a_or_an(hilt_color), hilt_color,
            handle_types[roll3]);
    *buf = UPPER(*buf);
    obj->description = strdup(buf);

  // head color
  } else if (roll >= 31) {
    sprintf(buf, "%s %s-%s %s", weapon_name,
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)]);
    obj->name = strdup(buf);
    sprintf(buf, "%s %s-%s %s %s", a_or_an(head_color),
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name);
    obj->short_description = strdup(buf);
    sprintf(buf, "%s %s-%s %s %s lies here.", a_or_an(head_color),
            head_color, head_types[roll2],
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name);
    *buf = UPPER(*buf);
    obj->description = strdup(buf);

  // hilt color
  } else if (roll >= 21) {
    sprintf(buf, "%s %s %s %s",
            weapon_name, material_name[GET_OBJ_MATERIAL(obj)],
            hilt_color,
            handle_types[roll3]);
    obj->name = strdup(buf);
    sprintf(buf, "%s %s %s with %s %s %s", a_or_an((char *) material_name[GET_OBJ_MATERIAL(obj)]),
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name,
            a_or_an(hilt_color), hilt_color,
            handle_types[roll3]);
    obj->short_description = strdup(buf);
    sprintf(buf, "%s %s %s with %s %s %s lies here.",
            a_or_an((char *) material_name[GET_OBJ_MATERIAL(obj)]),
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name,
            a_or_an(hilt_color), hilt_color,
            handle_types[roll3]);
    *buf = UPPER(*buf);
    obj->description = strdup(buf);

  // special
  } else if (roll >= 11) {
    sprintf(buf, "%s %s %s", weapon_name,
            material_name[GET_OBJ_MATERIAL(obj)], special);
    obj->name = strdup(buf);
    sprintf(buf, "%s %s %s %s", a_or_an(special), special,
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name);
    obj->short_description = strdup(buf);
    sprintf(buf, "%s %s %s %s lies here.", a_or_an(special), special,
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name);
    *buf = UPPER(*buf);
    obj->description = strdup(buf);
    
  // none
  } else {
    sprintf(buf, "%s %s",
            weapon_name, material_name[GET_OBJ_MATERIAL(obj)]);
    obj->name = strdup(buf);
    sprintf(buf, "%s %s %s", a_or_an((char *) material_name[GET_OBJ_MATERIAL(obj)]),
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name);
    obj->short_description = strdup(buf);
    sprintf(buf, "%s %s %s lies here.",
            a_or_an((char *) material_name[GET_OBJ_MATERIAL(obj)]),
            material_name[GET_OBJ_MATERIAL(obj)], weapon_name);
    *buf = UPPER(*buf);
    obj->description = strdup(buf);
  }
  
  /* object is fully described 
   base object is taken care of including material, now set random stats, etc */
  
  obj->affected[0].location = random_apply_value();
  obj->affected[0].modifier =
          random_bonus_value(obj->affected[0].location, level + (rare_grade * BONUS_FACTOR));
  GET_OBJ_LEVEL(obj) = level;
  GET_OBJ_COST(obj) = GET_OBJ_LEVEL(obj) * 100;

  REMOVE_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MOLD);
  if (grade > GRADE_MUNDANE)
    SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MAGIC);

  obj_to_char(obj, ch);

  send_to_char(ch, "\tYYou have found %s in a nearby lair!\tn\r\n", obj->short_description);
  sprintf(buf, "$n \tYhas found %s in a nearby lair!\tn", obj->short_description);
  act(buf, FALSE, ch, 0, ch, TO_NOTVICT);
}
#undef SHORT_STRING


/* give away random magic armor (not:  body/head/legs/arms)
 * includes:  neck, about, waist, wrist, hands, rings, feet */

/*
 method:
 * 1)  determine item
 * 2)  determine material
 * 3)  assign description
 * 4)  determine modifier (if applicable)
 * 5)  determine amount (if applicable)
 */
#define SHORT_STRING    80
void award_misc_magic_item(struct char_data *ch, int grade, int moblevel) {
  struct obj_data *obj = NULL;
  int vnum = -1, material = MATERIAL_BRONZE, roll = 0;
  int rare_grade = 0, level = 0;
  char desc[MEDIUM_STRING] = {'\0'}, armor_name[MEDIUM_STRING] = {'\0'};
  char buf[MAX_STRING_LENGTH] = {'\0'}, keywords[MEDIUM_STRING] = {'\0'};
  char desc2[SHORT_STRING] = {'\0'}, desc3[SHORT_STRING] = {'\0'};
  
  /* determine if rare or not */
  roll = dice(1, 100);
  if (roll == 1) {
    rare_grade = 3;
    sprintf(desc, "\tM[Mythical] \tn");
  } else if (roll <= 6) {
    rare_grade = 2;
    sprintf(desc, "\tY[Legendary] \tn");
  } else if (roll <= 16) {
    rare_grade = 1;
    sprintf(desc, "\tG[Rare] \tn");
  }

  /* find a random piece of armor
   * assign base material
   * and last but not least, give appropriate start of description
   *  */
  switch (dice(1, NUM_MISC_MOLDS)) {
    case 1:
      vnum = RING_MOLD;
      material = MATERIAL_COPPER;
      sprintf(armor_name, ring_descs[rand_number(0, NUM_A_RING_DESCS)]);
      sprintf(desc2, gemstones[rand_number(0, NUM_A_GEMSTONES)]);
      break;
    case 2:
      vnum = NECKLACE_MOLD;
      material = MATERIAL_COPPER;
      sprintf(armor_name, neck_descs[rand_number(0, NUM_A_NECK_DESCS)]);
      sprintf(desc2, gemstones[rand_number(0, NUM_A_GEMSTONES)]);
      break;
    case 3:
      vnum = BOOTS_MOLD;
      material = MATERIAL_LEATHER;
      sprintf(armor_name, boot_descs[rand_number(0, NUM_A_BOOT_DESCS)]);
      sprintf(desc2, armor_special_descs[rand_number(0, NUM_A_ARMOR_SPECIAL_DESCS)]);
      sprintf(desc3, colors[rand_number(0, NUM_A_COLORS)]);
      break;
    case 4:
      vnum = GLOVES_MOLD;
      material = MATERIAL_LEATHER;
      sprintf(armor_name, hands_descs[rand_number(0, NUM_A_HAND_DESCS)]);
      sprintf(desc2, armor_special_descs[rand_number(0, NUM_A_ARMOR_SPECIAL_DESCS)]);
      sprintf(desc3, colors[rand_number(0, NUM_A_COLORS)]);
      break;
    case 5:
      vnum = CLOAK_MOLD;
      material = MATERIAL_COTTON;
      sprintf(armor_name, cloak_descs[rand_number(0, NUM_A_CLOAK_DESCS)]);
      sprintf(desc2, armor_crests[rand_number(0, NUM_A_ARMOR_CRESTS)]);
      sprintf(desc3, colors[rand_number(0, NUM_A_COLORS)]);
      break;
    case 6:
      vnum = BELT_MOLD;
      material = MATERIAL_LEATHER;
      sprintf(armor_name, waist_descs[rand_number(0, NUM_A_WAIST_DESCS)]);
      sprintf(desc2, armor_special_descs[rand_number(0, NUM_A_ARMOR_SPECIAL_DESCS)]);
      sprintf(desc3, colors[rand_number(0, NUM_A_COLORS)]);
      break;
    case 7:
      vnum = WRIST_MOLD;
      material = MATERIAL_COPPER;
      sprintf(armor_name, wrist_descs[rand_number(0, NUM_A_WRIST_DESCS)]);
      sprintf(desc2, gemstones[rand_number(0, NUM_A_GEMSTONES)]);
      break;
    case 8:
      vnum = HELD_MOLD;
      material = MATERIAL_ONYX;
      sprintf(armor_name, crystal_descs[rand_number(0, NUM_A_CRYSTAL_DESCS)]);
      sprintf(desc2, colors[rand_number(0, NUM_A_COLORS)]);
      break;
  }

  /* we already determined 'base' material, now
   determine whether an upgrade was achieved by item-grade */
  roll = dice(1, 100);
  switch (material) {
    case MATERIAL_COPPER:
      switch (grade) {
        case GRADE_MUNDANE:
          if (roll <= 75)
            material = MATERIAL_COPPER;
          else
            material = MATERIAL_BRASS;
          break;
        case GRADE_MINOR:
          if (roll <= 75)
            material = MATERIAL_BRASS;
          else
            material = MATERIAL_SILVER;
          break;
        case GRADE_MEDIUM:
          if (roll <= 50)
            material = MATERIAL_BRASS;
          else if (roll <= 80)
            material = MATERIAL_SILVER;
          else
            material = MATERIAL_GOLD;
          break;
        default: // major grade
          if (roll <= 50)
            material = MATERIAL_SILVER;
          else if (roll <= 80)
            material = MATERIAL_GOLD;
          else
            material = MATERIAL_PLATINUM;
          break;
      }
      break;
    case MATERIAL_LEATHER:
      switch (grade) {
        case GRADE_MUNDANE:
          material = MATERIAL_LEATHER;
          break;
        case GRADE_MINOR:
          material = MATERIAL_LEATHER;
          break;
        case GRADE_MEDIUM:
          material = MATERIAL_LEATHER;
          break;
        default: // major grade
          if (roll <= 90)
            material = MATERIAL_LEATHER;
          else
            material = MATERIAL_DRAGONHIDE;
          break;
      }
      break;
    case MATERIAL_COTTON:
      switch (grade) {
        case GRADE_MUNDANE:
          if (roll <= 75)
            material = MATERIAL_HEMP;
          else
            material = MATERIAL_COTTON;
          break;
        case GRADE_MINOR:
          if (roll <= 50)
            material = MATERIAL_HEMP;
          else if (roll <= 80)
            material = MATERIAL_COTTON;
          else
            material = MATERIAL_WOOL;
          break;
        case GRADE_MEDIUM:
          if (roll <= 50)
            material = MATERIAL_COTTON;
          else if (roll <= 80)
            material = MATERIAL_WOOL;
          else if (roll <= 95)
            material = MATERIAL_VELVET;
          else
            material = MATERIAL_SATIN;
          break;
        default: // major grade
          if (roll <= 50)
            material = MATERIAL_WOOL;
          else if (roll <= 80)
            material = MATERIAL_VELVET;
          else if (roll <= 95)
            material = MATERIAL_SATIN;
          else
            material = MATERIAL_SILK;
          break;
      }
      break;
    /* options:  crystal, obsidian, onyx, ivory, pewter*/
    case MATERIAL_ONYX:
      switch (dice(1, 5)) {
        case 1:
          material = MATERIAL_CRYSTAL;
          break;
        case 2:
          material = MATERIAL_OBSIDIAN;
          break;
        case 3:
          material = MATERIAL_IVORY;
          break;
        case 4:
          material = MATERIAL_PEWTER;
          break;
        default:
          material = MATERIAL_ONYX;
          break;
      }
      break;
  }

  /* determine level */
  switch (grade) {
    case GRADE_MUNDANE:
      level = rand_number(1, 8);
      break;
    case GRADE_MINOR:
      level = rand_number(9, 16);
      break;
    case GRADE_MEDIUM:
      level = rand_number(17, 24);
      break;
    default: // major grade
      level = rand_number(25, 30);
      break;
  }

  /* ok load object, set material */
  if ((obj = read_object(vnum, VIRTUAL)) == NULL) {
    log("SYSERR: award_magic_armor created NULL object");
    return;
  }
  GET_OBJ_MATERIAL(obj) = material;
  
  /* put together a descrip */  
  switch (vnum) {
    case RING_MOLD:
    case NECKLACE_MOLD:
    case WRIST_MOLD:
      sprintf(keywords, "%s %s set with %s gemstone",
              armor_name, material_name[material], desc2);
      obj->name = strdup(keywords);
      sprintf(desc, "%s%s %s %s set with %s %s gemstone", desc,
              AN(material_name[material]), material_name[material],
              armor_name, AN(desc2), desc2);
      obj->short_description = strdup(desc);
      sprintf(desc, "%s %s %s set with %s %s gemstone lies here.", 
              AN(material_name[material]), material_name[material],
              armor_name, AN(desc2), desc2);
      obj->description = strdup(CAP(desc));
      break;
    case BOOTS_MOLD:
    case GLOVES_MOLD:
      sprintf(keywords, "%s pair %s %s leather", armor_name, desc2, desc3);
      obj->name = strdup(keywords);
      sprintf(desc, "%sa pair of %s %s leather %s", desc, desc2, desc3,
              armor_name);
      obj->short_description = strdup(desc);
      sprintf(desc, "A pair of %s %s leather %s lie here.", desc2, desc3,
              armor_name);
      obj->description = strdup(desc);
      break;
    case CLOAK_MOLD:
      sprintf(keywords, "%s %s %s %s bearing crest", armor_name, desc2,
              material_name[material], desc3);
      obj->name = strdup(keywords);
      sprintf(desc, "%s%s %s %s %s bearing the crest of %s %s", desc, AN(desc3), desc3,
              material_name[material], armor_name, AN(desc2),
              desc2);
      obj->short_description = strdup(desc);
      sprintf(desc, "%s %s %s %s bearing the crest of %s %s is lying here.", AN(desc3), desc3,
              material_name[material], armor_name, AN(desc2),
              desc2);
      obj->description = strdup(CAP(desc));
      break;
    case BELT_MOLD:
      sprintf(keywords, "%s %s leather %s", armor_name, desc2, desc3);
      obj->name = strdup(keywords);
      sprintf(desc, "%s%s %s %s leather %s", desc, AN(desc2), desc2, desc3,
              armor_name);
      obj->short_description = strdup(desc);
      sprintf(desc, "%s %s %s leather %s lie here.", AN(desc2), desc2, desc3,
              armor_name);
      obj->description = strdup(desc);
      break;
    case HELD_MOLD:
      sprintf(keywords, "%s %s orb", armor_name, desc2);
      obj->name = strdup(keywords);
      sprintf(desc, "%sa %s %s orb", desc, desc2, armor_name);
      obj->short_description = strdup(desc);
      sprintf(desc, "A %s %s orb is lying here.", desc2, armor_name);
      obj->description = strdup(desc);
      break;
  }
  
  /* level, bonus and cost */
  GET_OBJ_LEVEL(obj) = level;
  obj->affected[0].location = random_apply_value();
  obj->affected[0].modifier =
          random_bonus_value(obj->affected[0].location, level + (rare_grade * BONUS_FACTOR));
  GET_OBJ_COST(obj) = GET_OBJ_LEVEL(obj) * 100;

  REMOVE_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MOLD);
  if (grade > GRADE_MUNDANE)
    SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_MAGIC);

  obj_to_char(obj, ch);

  send_to_char(ch, "\tYYou have found %s in a nearby lair!\tn\r\n", obj->short_description);
  sprintf(buf, "$n \tYhas found %s in a nearby lair!\tn", obj->short_description);
  act(buf, FALSE, ch, 0, ch, TO_NOTVICT);
}
#undef SHORT_STRING


/* staff tool to load random items */
ACMD(do_loadmagic) {
  char arg1[MAX_STRING_LENGTH];
  char arg2[MAX_STRING_LENGTH];
  int number = 1;
  int grade = 0;

  two_arguments(argument, arg1, arg2);

  if (!*arg1) {
    send_to_char(ch, "Syntax: loadmagic [mundane | minor | medium | major] [# of items]\r\n");
    return;
  }

  if (*arg2 && !isdigit(arg2[0])) {
    send_to_char(ch, "The second number must be an integer.\r\n");
    return;
  }

  if (is_abbrev(arg1, "mundane"))
    grade = GRADE_MUNDANE;
  else if (is_abbrev(arg1, "minor"))
    grade = GRADE_MINOR;
  else if (is_abbrev(arg1, "medium"))
    grade = GRADE_MEDIUM;
  else if (is_abbrev(arg1, "major"))
    grade = GRADE_MAJOR;
  else {
    send_to_char(ch, "Syntax: loadmagic [mundane | minor | medium | major] [# of items]\r\n");
    return;
  }

  if (*arg2)
    number = atoi(arg2);

  award_magic_item(number, ch, GET_LEVEL(ch), grade);
}


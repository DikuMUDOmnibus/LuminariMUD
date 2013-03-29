/**************************************************************************
 *  File: magic.c                                           Part of tbaMUD *
 *  Usage: Low-level functions for magic; spell template code.             *
 *                                                                         *
 *  All rights reserved.  See license for complete information.            *
 *                                                                         *
 *  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "comm.h"
#include "spells.h"
#include "handler.h"
#include "db.h"
#include "interpreter.h"
#include "constants.h"
#include "dg_scripts.h"
#include "class.h"
#include "fight.h"
#include "utils.h"
#include "mud_event.h"
#include "act.h"  //perform_wildshapes

//external
extern struct raff_node *raff_list;

/* local file scope function prototypes */
static int mag_materials(struct char_data *ch, IDXTYPE item0, IDXTYPE item1,
        IDXTYPE item2, int extract, int verbose);
static void perform_mag_groups(int level, struct char_data *ch,
        struct char_data *tch, struct obj_data *obj, int spellnum,
        int savetype);




// Magic Resistance, ch is challenger, vict is resistor, modifier applys to vict

int compute_spell_res(struct char_data *ch, struct char_data *vict, int modifier) {
  int resist = GET_SPELL_RES(vict);

  //adjustments passed to mag_resistance
  resist += modifier;
  //additional adjustmenets
  if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_SPELL_RESIST_1))
    resist += 2;
  if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_SPELL_RESIST_2))
    resist += 2;
  if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_SPELL_RESIST_3))
    resist += 2;
  if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_SPELL_RESIST_4))
    resist += 2;
  if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_SPELL_RESIST_5))
    resist += 2;
  if (affected_by_spell(vict, SPELL_PROTECT_FROM_SPELLS))
    resist += 10;
  if (IS_AFFECTED(vict, AFF_SPELL_RESISTANT))
    resist += 12 + GET_LEVEL(vict);

  return MIN(99, MAX(0, resist));
}

// TRUE = reisted
// FALSE = Failed to resist

int mag_resistance(struct char_data *ch, struct char_data *vict, int modifier) {
  int challenge = dice(1, 20),
          resist = compute_spell_res(ch, vict, modifier);

  // should be modified - zusuk
  challenge += CASTER_LEVEL(ch);

  //insert challenge bonuses here (ch)
  if (!IS_NPC(ch) && GET_SKILL(ch, SKILL_SPELLPENETRATE))
    challenge += 2;
  if (!IS_NPC(ch) && GET_SKILL(ch, SKILL_SPELLPENETRATE_2))
    challenge += 2;
  if (!IS_NPC(ch) && GET_SKILL(ch, SKILL_SPELLPENETRATE_3))
    challenge += 4;

  //success?
  if (resist > challenge) {
    send_to_char(vict, "\tW*(%d>%d)you resist*\tn", resist, challenge);
    if (ch)
      send_to_char(ch, "\tR*(%d<%d)resisted*\tn", challenge, resist);
    return TRUE;
  }
  //failed to resist the spell
  return FALSE;
}

// Saving Throws, ch is challenger, vict is resistor, modifier applys to vict

int compute_mag_saves(struct char_data *vict,
        int type, int modifier) {

  int saves = 0;

  /* specific saves and related bonuses/penalties */
  switch (type) {
    case SAVING_FORT:
      saves += GET_CON_BONUS(vict);
      if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_GREAT_FORTITUDE))
        saves += 2;
      if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_EPIC_FORTITUDE))
        saves += 3;
      break;
    case SAVING_REFL:
      saves += GET_DEX_BONUS(vict);
      if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_LIGHTNING_REFLEXES))
        saves += 2;
      if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_EPIC_REFLEXES))
        saves += 3;
      break;
    case SAVING_WILL:
      saves += GET_WIS_BONUS(vict);
      if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_IRON_WILL))
        saves += 2;
      if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_EPIC_WILL))
        saves += 3;
      break;
  }

  /* universal bonuses/penalties */
  if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_LUCK_OF_HEROES))
    saves++;
  if (!IS_NPC(vict) && GET_RACE(vict) == RACE_HALFLING)
    saves++;
  if (!IS_NPC(vict) && GET_SKILL(vict, SKILL_GRACE)) {
    increase_skill(vict, SKILL_GRACE);
    /* i decided to cap this, a little too powerful otherwise */
    saves += MIN(CLASS_LEVEL(vict, CLASS_PALADIN) + 2,
            GET_CHA_BONUS(vict));
  }

  /* determine base, add/minus bonus/penalty and return */
  if (IS_NPC(vict))
    saves += (GET_LEVEL(vict) / 3) + 1;
  else
    saves += saving_throws(vict, type);
  saves += GET_SAVE(vict, type);
  saves += modifier;

  return MIN(50, MAX(saves, 0));
}


const char *save_names[] = {"Fort", "Refl", "Will", "", ""};
// TRUE = resisted
// FALSE = Failed to resist
// modifier applies to victim, higher the better (for the victim)

int mag_savingthrow(struct char_data *ch, struct char_data *vict,
        int type, int modifier) {
  int challenge = 10, // 10 is base DC
          diceroll = dice(1, 20),
          savethrow = compute_mag_saves(vict, type, modifier) + diceroll;

  //can add challenge bonus/penalties here (ch)
  challenge += (DIVINE_LEVEL(ch) + MAGIC_LEVEL(ch)) / 2;
  if (DIVINE_LEVEL(ch) > MAGIC_LEVEL(ch))
    challenge += GET_WIS_BONUS(ch);
  else
    challenge += GET_INT_BONUS(ch);

  if (AFF_FLAGGED(vict, AFF_PROTECT_GOOD) && IS_GOOD(ch))
    savethrow += 2;
  if (AFF_FLAGGED(vict, AFF_PROTECT_EVIL) && IS_EVIL(ch))
    savethrow += 2;

  if (diceroll != 1 && (savethrow > challenge || diceroll == 20)) {
    send_to_char(vict, "\tW*(%s:%d<%d)saved*\tn ", save_names[type],
            savethrow, challenge);
    if (ch && vict && vict != ch)
      send_to_char(ch, "\tR*(%s:%d>%d)opp saved*\tn ", save_names[type],
            challenge, savethrow);
    return (TRUE);
  }

  send_to_char(vict, "\tR*(%s:%d>%d)failed save*\tn ", save_names[type],
          savethrow, challenge);
  if (ch && vict && vict != ch)
    send_to_char(ch, "\tW*(%s:%d<%d)opp failed saved*\tn ", save_names[type],
          challenge, savethrow);
  return (FALSE);
}

/* added this function to add special wear off handling -zusuk */
void spec_wear_off(struct char_data *ch, int skillnum) {
  if (skillnum >= MAX_SKILLS)
    return;
  if (skillnum <= 0)
    return;
  
  switch (skillnum) {
    case SPELL_ANIMAL_SHAPES:
      send_to_char(ch, "As the spell wears off you feel yourself "
              "transform back to your normal form...\r\n");
      IS_MORPHED(ch) = 0;      
      SUBRACE(ch) = 0;      
      break;
    default:
      break;
  }
  
}

/* added this function to add wear off messages for skills -zusuk */
void alt_wear_off_msg(struct char_data *ch, int skillnum) {
  if (skillnum < (MAX_SPELLS + 1))
    return;
  if (skillnum >= MAX_SKILLS)
    return;

  switch (skillnum) {
    case SKILL_RAGE:
      send_to_char(ch, "Your rage has calmed...\r\n");
      break;
    case SKILL_PERFORM:
      send_to_char(ch, "Your bard-song morale has faded...\r\n");
      SONG_AFF_VAL(ch) = 0;
      break;
    case SKILL_CRIP_STRIKE:
      send_to_char(ch, "You have recovered from the crippling strike...\r\n");
      break;
    case SKILL_WILDSHAPE:
      send_to_char(ch, "You are unable to maintain your wildshape and "
              "transform back to your normal form...\r\n");
      IS_MORPHED(ch) = 0;      
      SUBRACE(ch) = 0;      
      break;
    default:
      break;
  }

}

void rem_room_aff(struct raff_node *raff) {
  struct raff_node *temp;

  /* this room affection has expired */
  send_to_room(raff->room, spell_info[raff->spell].wear_off_msg);
  send_to_room(raff->room, "\r\n");

  /* remove the affection */
  REMOVE_BIT(world[(int) raff->room].room_affections, raff->affection);
  REMOVE_FROM_LIST(raff, raff_list, next)
  free(raff);
}

/* affect_update: called from comm.c (causes spells to wear off) */
void affect_update(void) {
  struct affected_type *af, *next;
  struct char_data *i;
  struct raff_node *raff, *next_raff;
  int has_message = 0;

  for (i = character_list; i; i = i->next) {
    for (af = i->affected; af; af = next) {
      next = af->next;
      if (af->duration >= 1)
        af->duration--;
      else if (af->duration == -1) /* No action */
        ;
      else {
        if ((af->spell > 0) && (af->spell <= MAX_SPELLS)) {
          if (!af->next || (af->next->spell != af->spell) ||
                  (af->next->duration > 0)) {
            if (spell_info[af->spell].wear_off_msg) {
              send_to_char(i, "%s\r\n", spell_info[af->spell].wear_off_msg);
              spec_wear_off(i, af->spell);
              has_message = 1;
            }
          }
        }
        if (!has_message)
          alt_wear_off_msg(i, af->spell);
        affect_remove(i, af);
      }
    }
  }

  /* update the room affections */
  for (raff = raff_list; raff; raff = next_raff) {
    next_raff = raff->next;
    raff->timer--;

    if (raff->timer <= 0)
      rem_room_aff(raff);
  }
}

/* Checks for up to 3 vnums (spell reagents) in the player's inventory. If
 * multiple vnums are passed in, the function ANDs the items together as
 * requirements (ie. if one or more are missing, the spell will not fail).
 * @param ch The caster of the spell.
 * @param item0 The first required item of the spell, NOTHING if not required.
 * @param item1 The second required item of the spell, NOTHING if not required.
 * @param item2 The third required item of the spell, NOTHING if not required.
 * @param extract TRUE if mag_materials should consume (destroy) the items in
 * the players inventory, FALSE if not. Items will only be removed on a
 * successful cast.
 * @param verbose TRUE to provide some generic failure or success messages,
 * FALSE to send no in game messages from this function.
 * @retval int TRUE if ch has all materials to cast the spell, FALSE if not.
 */
static int mag_materials(struct char_data *ch, IDXTYPE item0,
        IDXTYPE item1, IDXTYPE item2, int extract, int verbose) {
  /* Begin Local variable definitions. */
  /*------------------------------------------------------------------------*/
  /* Used for object searches. */
  struct obj_data *tobj = NULL;
  /* Points to found reagents. */
  struct obj_data *obj0 = NULL, *obj1 = NULL, *obj2 = NULL;
  /*------------------------------------------------------------------------*/
  /* End Local variable definitions. */

  /* Begin success checks. Checks must pass to signal a success. */
  /*------------------------------------------------------------------------*/
  /* Check for the objects in the players inventory. */
  for (tobj = ch->carrying; tobj; tobj = tobj->next_content) {
    if ((item0 != NOTHING) && (GET_OBJ_VNUM(tobj) == item0)) {
      obj0 = tobj;
      item0 = NOTHING;
    } else if ((item1 != NOTHING) && (GET_OBJ_VNUM(tobj) == item1)) {
      obj1 = tobj;
      item1 = NOTHING;
    } else if ((item2 != NOTHING) && (GET_OBJ_VNUM(tobj) == item2)) {
      obj2 = tobj;
      item2 = NOTHING;
    }
  }

  /* If we needed items, but didn't find all of them, then the spell is a
   * failure. */
  if ((item0 != NOTHING) || (item1 != NOTHING) || (item2 != NOTHING)) {
    /* Generic spell failure messages. */
    if (verbose) {
      switch (rand_number(0, 2)) {
        case 0:
          send_to_char(ch, "A wart sprouts on your nose.\r\n");
          break;
        case 1:
          send_to_char(ch, "Your hair falls out in clumps.\r\n");
          break;
        case 2:
          send_to_char(ch, "A huge corn develops on your big toe.\r\n");
          break;
      }
    }
    /* Return fales, the material check has failed. */
    return (FALSE);
  }
  /*------------------------------------------------------------------------*/
  /* End success checks. */

  /* From here on, ch has all required materials in their inventory and the
   * material check will return a success. */

  /* Begin Material Processing. */
  /*------------------------------------------------------------------------*/
  /* Extract (destroy) the materials, if so called for. */
  if (extract) {
    if (obj0 != NULL)
      extract_obj(obj0);
    if (obj1 != NULL)
      extract_obj(obj1);
    if (obj2 != NULL)
      extract_obj(obj2);
    /* Generic success messages that signals extracted objects. */
    if (verbose) {
      send_to_char(ch, "A puff of smoke rises from your pack.\r\n");
      act("A puff of smoke rises from $n's pack.", TRUE, ch, NULL, NULL, TO_ROOM);
    }
  }

  /* Don't extract the objects, but signal materials successfully found. */
  if (!extract && verbose) {
    send_to_char(ch, "Your pack rumbles.\r\n");
    act("Something rumbles in $n's pack.", TRUE, ch, NULL, NULL, TO_ROOM);
  }
  /*------------------------------------------------------------------------*/
  /* End Material Processing. */

  /* Signal to calling function that the materials were successfully found
   * and processed. */
  return (TRUE);
}


// save = -1  ->  you get no save
// default    ->  magic resistance
// returns damage, -1 if dead

int mag_damage(int level, struct char_data *ch, struct char_data *victim,
        struct obj_data *wpn, int spellnum, int savetype) {
  int dam = 0, element = 0, num_dice = 0, save = savetype, size_dice = 0,
          bonus = 0, magic_level = 0, divine_level = 0, mag_resist = TRUE;

  if (victim == NULL || ch == NULL)
    return (0);

  magic_level = MAGIC_LEVEL(ch);
  divine_level = DIVINE_LEVEL(ch);
  if (wpn)
    if (HAS_SPELLS(wpn))
      magic_level = divine_level = level;

  /* need to include:
   * 1)  save = SAVING_x   -1 means no saving throw
   * 2)  mag_resist = TRUE/FALSE (TRUE - default, resistable or FALSE - not)
   * 3)  element = DAM_x
   */

  switch (spellnum) {
    
      /*******************************************\
      || ------------- MAGIC SPELLS ------------ ||
      \*******************************************/
    
    case SPELL_ACID_ARROW: //conjuration
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_ACID;
      num_dice = 4;
      size_dice = 6;
      bonus = 0;
      break;

    case SPELL_ACID_SPLASH:
      save = SAVING_REFL;
      num_dice = 2;
      size_dice = 3;
      element = DAM_ACID;
      break;

    case SPELL_BALL_OF_LIGHTNING: //evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_ELECTRIC;
      num_dice = MIN(22, magic_level);
      size_dice = 10;
      bonus = 0;
      break;

    case SPELL_BLIGHT: // evocation
      if (!IS_PLANT(victim)) {
        send_to_char(ch, "Your blight spell will only effect plant life.\r\n");
        return (0);
      }
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_EARTH;
      num_dice = MIN(divine_level, 15); // maximum 15d6
      size_dice = 6;
      bonus = MIN(divine_level, 15);
      break;
      
    case SPELL_BURNING_HANDS: //evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = MIN(8, magic_level);
      size_dice = 6;
      bonus = 0;
      break;

    case SPELL_CHILL_TOUCH: //necromancy
      // *note chill touch also has an effect, only save on effect
      save = -1;
      mag_resist = TRUE;
      element = DAM_COLD;
      num_dice = 1;
      size_dice = 10;
      bonus = 0;
      break;

    case SPELL_CLENCHED_FIST: //evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FORCE;
      num_dice = MIN(28, magic_level);
      size_dice = 11;
      bonus = magic_level + 5;

      // 33% chance of causing a wait-state to victim
      if (!rand_number(0, 2))
        attach_mud_event(new_mud_event(eFISTED, ch, NULL), 1000);

      break;

    case SPELL_COLOR_SPRAY: //illusion
      //  has effect too
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_ILLUSION;
      num_dice = 1;
      size_dice = 4;
      bonus = 0;
      break;

    case SPELL_CONE_OF_COLD: //abjuration
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_COLD;
      num_dice = MIN(22, magic_level);
      size_dice = 6;
      bonus = 0;
      break;

    case SPELL_ENERGY_SPHERE: //abjuration
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_ENERGY;
      num_dice = MIN(10, magic_level);
      size_dice = 8;
      bonus = 0;
      break;

    case SPELL_FINGER_OF_DEATH: // necromancy
      // saving throw is handled special below
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_UNHOLY;
      num_dice = 3;
      size_dice = 6;
      bonus = GET_HIT(victim) + 10; // MIN(divine_level, 25);      
      break;
      
    case SPELL_FIREBALL: //evocation
      // Nashak: make this dissipate obscuring mist when finished
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = MIN(15, magic_level);
      size_dice = 10;
      bonus = 0;
      break;

    case SPELL_FIREBRAND: //transmutation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = MIN(22, magic_level);
      size_dice = 6;
      bonus = 0;
      break;
      
    case SPELL_FLAME_BLADE: // evocation
      if (SECT(ch->in_room) == SECT_UNDERWATER) {
        send_to_char(ch, "Your flame blade immediately burns out underwater.");
        return (0);
      }
      save = -1;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = 1;
      size_dice = 8;
      bonus = MIN(magic_level / 2, 10);
      break;
      
    case SPELL_FREEZING_SPHERE: //evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_COLD;
      num_dice = MIN(24, magic_level);
      size_dice = 10;
      bonus = magic_level / 2;
      break;

    case SPELL_GRASPING_HAND: //evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FORCE;
      num_dice = MIN(26, magic_level);
      size_dice = 6;
      bonus = magic_level;
      break;

    case SPELL_GREATER_RUIN: //epic spell
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_PUNCTURE;
      num_dice = magic_level + 6;
      size_dice = 12;
      bonus = magic_level + 35;
      break;
      

    case SPELL_HORIZIKAULS_BOOM: //evocation
      // *note also has an effect
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_SOUND;
      num_dice = MIN(8, magic_level);
      size_dice = 4;
      bonus = num_dice;
      break;

    case SPELL_ICE_DAGGER: //conjurations
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_COLD;
      num_dice = MIN(7, magic_level);
      size_dice = 8;
      bonus = 0;
      break;

    case SPELL_LIGHTNING_BOLT: //evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_ELECTRIC;
      num_dice = MIN(15, magic_level);
      size_dice = 10;
      bonus = 0;
      break;

    case SPELL_MAGIC_MISSILE: //evocation
      if (affected_by_spell(victim, SPELL_SHIELD)) {
        send_to_char(ch, "Your target is shielded by magic!\r\n");
      } else {
        mag_resist = TRUE;
        save = -1;
        num_dice = MIN(8, (MAX(1, magic_level / 2)));
        size_dice = 6;
        bonus = num_dice;
      }
      element = DAM_FORCE;
      break;

    case SPELL_MISSILE_STORM: //evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FORCE;
      num_dice = MIN(26, magic_level);
      size_dice = 10;
      bonus = magic_level;
      break;

    case SPELL_NEGATIVE_ENERGY_RAY: //necromancy
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_NEGATIVE;
      num_dice = 2;
      size_dice = 6;
      bonus = 3;
      break;

    case SPELL_NIGHTMARE: //illusion
      // *note nightmare also has an effect, only save on effect
      save = -1;
      mag_resist = TRUE;
      element = DAM_ILLUSION;
      num_dice = magic_level;
      size_dice = 4;
      bonus = 10;
      break;

    case SPELL_POWER_WORD_KILL: //divination
      save = -1;
      mag_resist = TRUE;
      element = DAM_MENTAL;
      num_dice = 10;
      size_dice = 2;
      if (GET_HIT(victim) <= 121)
        bonus = GET_HIT(victim) + 10;
      else
        bonus = 0;
      break;

    case SPELL_PRODUCE_FLAME: // evocation
      if (SECT(ch->in_room) == SECT_UNDERWATER) {
        send_to_char(ch, "You are unable to produce a flame while underwater.");
        return (0);
      }
      save = -1;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = 1;
      size_dice = 6;
      bonus = MIN(divine_level, 5);
      break;

    case SPELL_RAY_OF_FROST:
      save = SAVING_REFL;
      num_dice = 2;
      size_dice = 3;
      element = DAM_COLD;
      break;

    case SPELL_SCORCHING_RAY: //evocation
      save = -1;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = MIN(22, magic_level * 2);
      size_dice = 6;
      bonus = 0;
      break;

    case SPELL_SHOCKING_GRASP: //evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_ELECTRIC;
      num_dice = MIN(10, magic_level);
      size_dice = 8;
      bonus = 0;
      break;

    case SPELL_TELEKINESIS: //transmutation
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_FORCE;
      num_dice = MIN(20, magic_level);
      size_dice = 4;
      bonus = 0;
      //60% chance of knockdown, target can't be more than 2 size classes bigger
      if (dice(1, 100) < 60 && (GET_SIZE(ch) + 2) >= GET_SIZE(victim)) {
        act("Your telekinetic wave knocks $N over!",
                FALSE, ch, 0, victim, TO_CHAR);
        act("The force of the telekinetic slam from $n knocks you over!\r\n",
                FALSE, ch, 0, victim, TO_VICT | TO_SLEEP);
        act("A wave of telekinetic energy originating from $n knocks $N to "
                "the ground!", FALSE, ch, 0, victim, TO_NOTVICT);
        GET_POS(victim) = POS_SITTING;
        SET_WAIT(victim, PULSE_VIOLENCE);
      }
      break;

    case SPELL_VAMPIRIC_TOUCH: //necromancy
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_UNHOLY;
      num_dice = MIN(15, magic_level);
      size_dice = 5;
      bonus = 0;
      break;

    case SPELL_WEIRD: //enchantment
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_ILLUSION;
      num_dice = magic_level;
      size_dice = 12;
      bonus = magic_level + 10;
      break;

      /*******************************************\
      || ------------ DIVINE SPELLS ------------ ||
      \*******************************************/

    case SPELL_CALL_LIGHTNING:
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_ELECTRIC;
      num_dice = MIN(24, divine_level);
      size_dice = 8;
      bonus = num_dice + 10;
      break;
      
    case SPELL_CALL_LIGHTNING_STORM:
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_ELECTRIC;
      num_dice = MIN(24, divine_level);
      size_dice = 12;
      bonus = num_dice + 20;
      break;

    case SPELL_CAUSE_CRITICAL_WOUNDS:
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_HOLY;
      num_dice = 4;
      size_dice = 12;
      bonus = MIN(30, divine_level);
      break;

    case SPELL_CAUSE_LIGHT_WOUNDS:
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_HOLY;
      num_dice = 1;
      size_dice = 12;
      bonus = MIN(7, divine_level);
      break;

    case SPELL_CAUSE_MODERATE_WOUNDS:
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_HOLY;
      num_dice = 2;
      size_dice = 12;
      bonus = MIN(15, divine_level);
      break;

    case SPELL_CAUSE_SERIOUS_WOUNDS:
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_HOLY;
      num_dice = 3;
      size_dice = 12;
      bonus = MIN(22, divine_level);
      break;

    case SPELL_DESTRUCTION:
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_NEGATIVE;
      num_dice = MIN(26, divine_level);
      size_dice = 8;
      bonus = num_dice + 20;
      break;

    case SPELL_DISPEL_EVIL:
      if (IS_EVIL(ch)) {
        victim = ch;
      } else if (IS_GOOD(victim)) {
        act("The gods protect $N.", FALSE, ch, 0, victim, TO_CHAR);
        return (0);
      }
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_HOLY;
      num_dice = divine_level;
      size_dice = 9;
      bonus = divine_level;
      break;

    case SPELL_DISPEL_GOOD:
      if (IS_GOOD(ch)) {
        victim = ch;
      } else if (IS_EVIL(victim)) {
        act("The gods protect $N.", FALSE, ch, 0, victim, TO_CHAR);
        return (0);
      }
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_UNHOLY;
      num_dice = divine_level;
      size_dice = 9;
      bonus = divine_level;
      break;

    case SPELL_ENERGY_DRAIN:
      //** Magic AND Divine
      if (AFF_FLAGGED(victim, AFF_DEATH_WARD)) {
        act("$N is warded against death magic.", FALSE, ch, 0, victim, TO_CHAR);
        return (0);
      }
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_NEGATIVE;
      if (GET_LEVEL(victim) < CASTER_LEVEL(ch))
        num_dice = 2;
      else
        num_dice = 1;
      size_dice = 200;
      gain_exp(ch, (dice(2, 200)*10));
      gain_exp(victim, -(dice(2, 200)*10));      
      bonus = 0;
      break;

    case SPELL_FLAME_STRIKE:
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = MIN(20, divine_level);
      size_dice = 8;
      bonus = 0;
      break;

    case SPELL_HARM:
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_HOLY;
      num_dice = MIN(22, divine_level);
      size_dice = 8;
      bonus = num_dice;
      break;

      /* trying to keep the AOE together */
      /****************************************\
      || ------------ AoE SPELLS ------------ ||
      \****************************************/

    case SPELL_ACID: //acid fog (conjuration)
      //AoE
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_ACID;
      num_dice = magic_level;
      size_dice = 2;
      bonus = 10;
      break;

    case SPELL_BLADES: //blade barrier damage (divine spell)
      //AoE
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_SLICE;
      num_dice = divine_level;
      size_dice = 6;
      bonus = 2;
      break;

    case SPELL_CHAIN_LIGHTNING: //evocation
      //AoE
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_ELECTRIC;
      num_dice = MIN(28, CASTER_LEVEL(ch));
      size_dice = 9;
      bonus = CASTER_LEVEL(ch);
      break;

    case SPELL_DEATHCLOUD: //cloudkill (conjuration)
      //AoE
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_POISON;
      num_dice = magic_level;
      size_dice = 4;
      bonus = 0;
      break;
      
    case SPELL_DOOM: // creeping doom (conjuration)
      //AoE
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = magic_level;
      size_dice = 5;
      bonus = magic_level;
      break;

    case SPELL_FLAMING_SPHERE: // evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = 2;
      size_dice = 6;
      bonus = 0;
      break;
      
    case SPELL_HELLBALL:
      //AoE
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_ENERGY;
      num_dice = magic_level + 8;
      size_dice = 12;
      bonus = magic_level + 50;
      break;

    case SPELL_HORRID_WILTING: //horrid wilting, necromancy
      //AoE
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_NEGATIVE;
      num_dice = MIN(28, magic_level);
      size_dice = 8;
      bonus = magic_level / 2;
      break;

    case SPELL_ICE_STORM: //evocation
      //AoE
      save = -1;
      mag_resist = TRUE;
      element = DAM_COLD;
      num_dice = MIN(15, CASTER_LEVEL(ch));
      size_dice = 8;
      bonus = 0;
      break;

    case SPELL_INCENDIARY: //incendiary cloud (conjuration)
      //AoE
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = magic_level;
      size_dice = 5;
      bonus = magic_level;
      break;

    case SPELL_INSECT_PLAGUE: // conjuration
      mag_resist = FALSE;
      save = -1;
      element = DAM_NEGATIVE;
      num_dice = MIN(divine_level / 3, 6);
      size_dice = 6;
      bonus = divine_level;
      break;
      
    case SPELL_METEOR_SWARM:
      //AoE
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = magic_level + 4;
      size_dice = 12;
      bonus = magic_level + 8;
      break;

    case SPELL_PRISMATIC_SPRAY: //illusion
      //  has effect too
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_ILLUSION;
      num_dice = MIN(26, magic_level);
      size_dice = 4;
      bonus = 0;
      break;

    case SPELL_SUMMON_SWARM: // conjuration
      mag_resist = FALSE;
      save = -1;
      element = DAM_NEGATIVE;
      num_dice = 1;
      size_dice = 6;
      bonus = MAX(divine_level, 5);
      break;

    case SPELL_SUNBEAM: // evocation [light]
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_LIGHT;
      num_dice = 4;
      size_dice = 6;
      bonus = magic_level;
      break;
      
    case SPELL_SUNBURST: //divination
      //  has effect too
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = MIN(26, magic_level);
      size_dice = 5;
      bonus = magic_level;
      break;

    case SPELL_SYMBOL_OF_PAIN: //necromancy
      //AoE
      save = SAVING_WILL;
      mag_resist = TRUE;
      element = DAM_UNHOLY;
      num_dice = MIN(17, magic_level);
      size_dice = 6;
      bonus = 0;
      break;

    case SPELL_THUNDERCLAP: // abjuration
      //  has effect too
      // no save
      save = -1;
      // no resistance
      mag_resist = FALSE;
      element = DAM_SOUND;
      num_dice = 1;
      size_dice = 10;
      bonus = magic_level;
      break;

    case SPELL_WAIL_OF_THE_BANSHEE: //necromancy
      //  has effect too
      save = SAVING_FORT;
      mag_resist = TRUE;
      element = DAM_SOUND;
      num_dice = magic_level + 2;
      size_dice = 5;
      bonus = magic_level + 10;
      break;
      
    case SPELL_WHIRLWIND: // evocation
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_AIR;
      num_dice = 3 * dice(1, 3);
      size_dice = 6;
      bonus = divine_level;
      break;

      /***********************************************\
      || ------------ DIVINE AoE SPELLS ------------ ||
      \***********************************************/

    case SPELL_EARTHQUAKE:
      //AoE
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_EARTH;
      num_dice = divine_level;
      size_dice = 8;
      bonus = num_dice + 30;
      break;
    case SPELL_FIRE_STORM:
      // AoE
      save = SAVING_REFL;
      mag_resist = TRUE;
      element = DAM_FIRE;
      num_dice = MIN(divine_level, 20);
      size_dice = 6;
      bonus = 0;
      break;

  } /* switch(spellnum) */

  dam = dice(num_dice, size_dice) + bonus;

  //resistances to magic
  if (dam && mag_resist)
    if (mag_resistance(ch, victim, 0))
      return 0;

  //dwarven racial bonus to magic, gnomes to illusion
  int race_bonus = 0;
  if (GET_RACE(victim) == RACE_DWARF)
    race_bonus += 2;
  if (GET_RACE(victim) == RACE_GNOME && element == DAM_ILLUSION)
    race_bonus += 2;

  // figure saving throw for finger of death here, because it's not half damage
  if (spellnum == SPELL_FINGER_OF_DEATH) {
    if (mag_savingthrow(ch, victim, save, race_bonus)) {
      dam = dice(num_dice, size_dice) + MIN(25, divine_level);
    }
  }
  else if (dam && (save != -1)) {
    //saving throw for half damage if applies  
    if (mag_savingthrow(ch, victim, save, race_bonus)) {
      if ((!IS_NPC(victim)) && save == SAVING_REFL && // evasion
              (GET_SKILL(victim, SKILL_EVASION) ||
              GET_SKILL(victim, SKILL_IMP_EVASION)))
        dam /= 2;
      dam /= 2;
    } else if ((!IS_NPC(victim)) && save == SAVING_REFL && // evasion
            (GET_SKILL(victim, SKILL_IMP_EVASION)))
      dam /= 2;

    if (!IS_NPC(victim) && GET_SKILL(victim, SKILL_EVASION))
      increase_skill(victim, SKILL_EVASION);
    if (!IS_NPC(victim) && GET_SKILL(victim, SKILL_IMP_EVASION))
      increase_skill(victim, SKILL_IMP_EVASION);
  }

  if (!element) //want to make sure all spells have some sort of damage cat
    log("SYSERR: %d is lacking DAM_", spellnum);

  return (damage(ch, victim, dam, spellnum, element, FALSE));
}

/* make sure you don't stack armor spells for silly high AC */
int isMagicArmored(struct char_data *victim) {
  if (affected_by_spell(victim, SPELL_EPIC_MAGE_ARMOR) ||
          affected_by_spell(victim, SPELL_MAGE_ARMOR) ||
          affected_by_spell(victim, SPELL_SHIELD) ||
          affected_by_spell(victim, SPELL_SHADOW_SHIELD) ||
          affected_by_spell(victim, SPELL_ARMOR) ||
          affected_by_spell(victim, SPELL_BARKSKIN)) {

    send_to_char(victim, "Your target already has magical armoring!\r\n");
    return TRUE;
  }

  return FALSE;
}


// converted affects to rounds
// 20 rounds = 1 real minute
// 1200 rounds = 1 real hour
// old tick = 75 seconds, or 1.25 minutes or 25 rounds
#define MAX_SPELL_AFFECTS 6	/* change if more needed */

void mag_affects(int level, struct char_data *ch, struct char_data *victim,
        struct obj_data *wpn, int spellnum, int savetype) {
  struct affected_type af[MAX_SPELL_AFFECTS];
  bool accum_affect = FALSE, accum_duration = FALSE;
  const char *to_vict = NULL, *to_room = NULL;
  int i, j, magic_level = 0, divine_level = 0;
  int elf_bonus = 0, gnome_bonus = 0, success = 0;
  bool is_mind_affect = FALSE;

  if (victim == NULL || ch == NULL)
    return;

  for (i = 0; i < MAX_SPELL_AFFECTS; i++) { //init affect array
    new_affect(&(af[i]));
    af[i].spell = spellnum;
  }

  /* racial ch bonus/penalty */
  /* added IS_NPC check to prevent NPCs from getting incorrect bonuses,
   * since NPCRACE_HUMAN = RACE_ELF, etc. -Nashak */
  if (!IS_NPC(ch)) {
    switch (GET_RACE(ch)) {
      case RACE_GNOME: // illusions
        gnome_bonus -= 2;
        break;
      default:
        break;
    }
    /* racial victim resistance */
    switch (GET_RACE(victim)) {
      case RACE_H_ELF:
      case RACE_ELF: //enchantments
        elf_bonus += 2;
        break;
      case RACE_GNOME: // illusions
        gnome_bonus += 2;
        break;
      default:
        break;
    }
  }
  magic_level = MAGIC_LEVEL(ch);
  divine_level = DIVINE_LEVEL(ch);
  if (wpn)
    if (HAS_SPELLS(wpn))
      magic_level = divine_level = level;

  switch (spellnum) {

    case SPELL_ACID_SHEATH: //divination
      if (affected_by_spell(victim, SPELL_FIRE_SHIELD) ||
              affected_by_spell(victim, SPELL_COLD_SHIELD)) {
        send_to_char(ch, "You are already affected by an elemental shield!\r\n");
        return;
      }
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_ASHIELD);

      accum_duration = FALSE;
      to_vict = "A shield of acid surrounds you.";
      to_room = "$n is surrounded by shield of acid.";
      break;

    case SPELL_AID:
      if (affected_by_spell(victim, SPELL_BLESS) ||
              affected_by_spell(victim, SPELL_PRAYER)) {
        send_to_char(ch, "The target is already blessed!\r\n");
        return;
      }
      
      af[0].location = APPLY_HITROLL;
      af[0].modifier = 3;
      af[0].duration = 300;

      af[1].location = APPLY_DAMROLL;
      af[1].modifier = 3;
      af[1].duration = 300;

      af[2].location = APPLY_SAVING_WILL;
      af[2].modifier = 2;
      af[2].duration = 300;

      af[3].location = APPLY_SAVING_FORT;
      af[3].modifier = 2;
      af[3].duration = 300;

      af[4].location = APPLY_SAVING_REFL;
      af[4].modifier = 2;
      af[4].duration = 300;

      af[5].location = APPLY_HIT;
      af[5].modifier = dice(2, 6) + MAX(divine_level, 15);
      af[5].duration = 300;

      accum_duration = TRUE;
      to_room = "$n is now divinely aided!";
      to_vict = "You feel divinely aided.";
      break;

    case SPELL_ARMOR:
      if (isMagicArmored(victim))
        return;

      af[0].location = APPLY_AC_NEW;
      af[0].modifier = 2;
      af[0].duration = 600;
      accum_duration = TRUE;
      to_vict = "You feel someone protecting you.";
      to_room = "$n is surrounded by magical armor!";
      break;

    case SPELL_BARKSKIN: // transmutation
      if (isMagicArmored(victim))
        return;

      af[0].location = APPLY_AC_NEW;
      if (divine_level >= 12)
        af[0].modifier = 5;
      else if (divine_level >= 9)
        af[0].modifier = 4;
      else if (divine_level >= 6)
        af[0].modifier = 3;
      else
        af[0].modifier = 2;
      af[0].duration = (divine_level * 200); // divine level * 10, * 20 for minutes
      accum_affect = FALSE;
      accum_duration = FALSE;
      to_vict = "Your skin hardens to bark.";
      to_room = "$n skin hardens to bark!";
      break;
     
    case SPELL_BATTLETIDE: //divine
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_BATTLETIDE);

      af[1].duration = 50;
      SET_BIT_AR(af[1].bitvector, AFF_HASTE);

      af[2].location = APPLY_HITROLL;
      af[2].modifier = 3;
      af[2].duration = 50;

      af[3].location = APPLY_DAMROLL;
      af[3].modifier = 3;
      af[3].duration = 50;

      accum_duration = FALSE;
      to_vict = "You feel the tide of battle turn in your favor!";
      to_room = "The tide of battle turns in $n's favor!";
      break;

    case SPELL_BLESS:
      if (affected_by_spell(victim, SPELL_AID) ||
              affected_by_spell(victim, SPELL_PRAYER)) {
        send_to_char(ch, "The target is already blessed!\r\n");
        return;
      }
      
      af[0].location = APPLY_HITROLL;
      af[0].modifier = 2;
      af[0].duration = 300;

      af[1].location = APPLY_SAVING_WILL;
      af[1].modifier = 1;
      af[1].duration = 300;

      accum_duration = TRUE;
      to_room = "$n is now righteous!";
      to_vict = "You feel righteous.";
      break;

    case SPELL_BLINDNESS: //necromancy
      if (MOB_FLAGGED(victim, MOB_NOBLIND)) {
        send_to_char(ch, "Your opponent doesn't seem blindable.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_FORT, 0)) {
        send_to_char(ch, "You fail.\r\n");
        return;
      }

      af[0].location = APPLY_HITROLL;
      af[0].modifier = -4;
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_BLIND);

      af[1].location = APPLY_AC_NEW;
      af[1].modifier = -4;
      af[1].duration = 50;
      SET_BIT_AR(af[1].bitvector, AFF_BLIND);

      to_room = "$n seems to be blinded!";
      to_vict = "You have been blinded!";
      break;
      
    case SPELL_BLUR: //illusion
      af[0].location = APPLY_AC;
      af[0].modifier = -1;
      af[0].duration = 300;
      to_room = "$n's images becomes blurry!.";
      to_vict = "You observe as your image becomes blurry.";
      SET_BIT_AR(af[0].bitvector, AFF_BLUR);
      accum_duration = FALSE;
      break;

    case SPELL_BRAVERY:
      af[0].duration = 25 + divine_level;
      SET_BIT_AR(af[0].bitvector, AFF_BRAVERY);

      accum_duration = TRUE;
      to_vict = "You suddenly feel very brave.";
      to_room = "$n suddenly feels very brave.";
      break;

    case SPELL_CHARISMA: //transmutation
      if (affected_by_spell(victim, SPELL_MASS_CHARISMA)) {
        send_to_char(ch, "Your target already has a charisma spell in effect.\r\n");
        return;
      }
      af[0].location = APPLY_CHA;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 4;
      accum_duration = TRUE;
      to_vict = "You feel more charismatic!";
      to_room = "$n's charisma increases!";
      break;

    case SPELL_CHILL_TOUCH: //necromancy
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_FORT, 0))
        return;

      af[0].location = APPLY_STR;
      af[0].duration = 4 + magic_level;
      af[0].modifier = -2;
      accum_duration = TRUE;
      to_room = "$n's strength is withered!";
      to_vict = "You feel your strength wither!";
      break;

    case SPELL_COLD_SHIELD: //evocation
      if (affected_by_spell(victim, SPELL_ACID_SHEATH) ||
              affected_by_spell(victim, SPELL_FIRE_SHIELD)) {
        send_to_char(ch, "You are already affected by an elemental shield!\r\n");
        return;
      }
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_CSHIELD);

      accum_duration = FALSE;
      to_vict = "A shield of ice surrounds you.";
      to_room = "$n is surrounded by shield of ice.";
      break;

    case SPELL_COLOR_SPRAY: //enchantment
      if (GET_LEVEL(victim) > 5) {
        send_to_char(ch, "Your target is too powerful to be stunned by this illusion.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, gnome_bonus)) {
        return;
      }

      SET_BIT_AR(af[0].bitvector, AFF_STUN);
      af[0].duration = dice(1, 4);
      to_room = "$n is stunned by the colors!";
      to_vict = "You are stunned by the colors!";
      break;

    case SPELL_CONTAGION: // necromancy
      switch (rand_number(1, 7)) {
        case 1: // blinding sickness
          af[0].location = APPLY_STR;
          af[0].modifier = -1 * dice(1, 4);
          to_vict = "You are overcome with a blinding sickness.";
          to_room = "$n is overcome with a blinding sickness.";
          break;
        case 2: // cackle fever
          af[0].location = APPLY_WIS;
          af[0].modifier = -1 * dice(1, 6);
          to_vict = "You suddenly come down with cackle fever.";
          to_room = "$n suddenly comes down with cackle fever.";
          break;
        case 3: // filth fever
          af[0].location = APPLY_DEX;
          af[0].modifier = -1 * dice(1, 3);
          SET_BIT_AR(af[1].bitvector, AFF_DISEASE);
          af[1].location = APPLY_CON;
          af[1].modifier = -1 * dice(1, 3);
          af[1].duration = 600;
          to_vict = "You suddenly come down with filth fever.";
          to_room = "$n suddenly comes down with filth fever.";
          break;
        case 4: // mindfire
          af[0].location = APPLY_INT;
          af[0].modifier = -1 * dice(1, 4);
          to_vict = "You feel your mind start to burn incessantly.";
          to_room = "$n's mind starts to burn incessantly.";
          break;
        case 5: // red ache
          af[0].location = APPLY_STR;
          af[0].modifier = -1 * dice(1, 6);
          to_vict = "You suddenly feel weakened by the red ache.";
          to_room = "$n suddenly feels weakened by the red ache.";
          break;
        case 6: // shakes
          af[0].location = APPLY_DEX;
          af[0].modifier = -1 * dice(1, 8);
          to_vict = "You feel yourself start to uncontrollably shake.";
          to_room = "$n starts to shake uncontrollably.";
          break;
        case 7: // slimy doom
          af[0].location = APPLY_CON;
          af[0].modifier = -1 * dice(1, 4);
          to_vict = "You feel yourself affected by the slimy doom.";
          to_room = "$n feels affected by the slimy doom.";
          break;
      }
      SET_BIT_AR(af[0].bitvector, AFF_DISEASE);
      af[0].duration = 600; // 30 real minutes (supposed to be permanent)
      accum_affect = FALSE;
      accum_duration = FALSE;
      break;

    case SPELL_CUNNING: //transmutation
      if (affected_by_spell(victim, SPELL_MASS_CUNNING)) {
        send_to_char(ch, "Your target already has a cunning spell in effect.\r\n");
        return;
      }
      af[0].location = APPLY_INT;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 4;
      accum_duration = TRUE;
      to_vict = "You feel more intelligent!";
      to_room = "$n's intelligence increases!";
      break;

    case SPELL_CURSE: //necromancy
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, 0)) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }

      af[0].location = APPLY_HITROLL;
      af[0].duration = 25 + (CASTER_LEVEL(ch) * 12);
      af[0].modifier = -2;
      SET_BIT_AR(af[0].bitvector, AFF_CURSE);

      af[1].location = APPLY_DAMROLL;
      af[1].duration = 25 + (CASTER_LEVEL(ch) * 12);
      af[1].modifier = -2;
      SET_BIT_AR(af[1].bitvector, AFF_CURSE);

      af[2].location = APPLY_SAVING_WILL;
      af[2].duration = 25 + (CASTER_LEVEL(ch) * 12);
      af[2].modifier = -2;
      SET_BIT_AR(af[2].bitvector, AFF_CURSE);

      accum_duration = TRUE;
      accum_affect = TRUE;
      to_room = "$n briefly glows red!";
      to_vict = "You feel very uncomfortable.";
      break;

    case SPELL_DAZE_MONSTER: //enchantment
      if (GET_LEVEL(victim) > 8) {
        send_to_char(ch, "Your target is too powerful to be affected by this illusion.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, elf_bonus)) {
        return;
      }
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }

      is_mind_affect = TRUE;

      SET_BIT_AR(af[0].bitvector, AFF_STUN);
      af[0].duration = dice(2, 4);
      to_room = "$n is dazed by the spell!";
      to_vict = "You are dazed by the spell!";
      break;

    case SPELL_DEAFNESS: //necromancy
      if (MOB_FLAGGED(victim, MOB_NODEAF)) {
        send_to_char(ch, "Your opponent doesn't seem deafable.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_FORT, 0)) {
        send_to_char(ch, "You fail.\r\n");
        return;
      }

      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_DEAF);

      to_room = "$n seems to be deafened!";
      to_vict = "You have been deafened!";
      break;

    case SPELL_DEATH_WARD: // necromancy
      af[0].duration = 10 * divine_level;
      SET_BIT_AR(af[0].bitvector, AFF_DEATH_WARD);

      accum_affect = FALSE;
      accum_duration = FALSE;
      to_room = "$n is warded against death magic!";
      to_vict = "You are warded against death magic!";
      break;
      
    case SPELL_DEEP_SLUMBER: //enchantment
      if (GET_LEVEL(victim) >= 15 ||
              (!IS_NPC(victim) && GET_RACE(victim) == RACE_ELF)) {
        send_to_char(ch, "The target is too powerful for this enchantment!\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (!CONFIG_PK_ALLOWED && !IS_NPC(ch) && !IS_NPC(victim))
        return;
      if (MOB_FLAGGED(victim, MOB_NOSLEEP)) {
        send_to_char(ch, "Your victim doesn't seem vulnerable to your spell.");
        return;
      }
      if (mag_savingthrow(ch, victim, SAVING_WILL, 0)) {
        return;
      }

      af[0].duration = 100 + (magic_level * 6);
      SET_BIT_AR(af[0].bitvector, AFF_SLEEP);

      if (GET_POS(victim) > POS_SLEEPING) {
        send_to_char(victim, "You feel very sleepy...  Zzzz......\r\n");
        act("$n goes to sleep.", TRUE, victim, 0, 0, TO_ROOM);
        if (FIGHTING(victim))
          stop_fighting(victim);
        GET_POS(victim) = POS_SLEEPING;
        if (FIGHTING(ch) == victim)
          stop_fighting(ch);
      }
      break;

    case SPELL_DETECT_ALIGN:
      af[0].duration = 300 + CASTER_LEVEL(ch) * 25;
      SET_BIT_AR(af[0].bitvector, AFF_DETECT_ALIGN);
      accum_duration = TRUE;
      to_room = "$n's eyes become sensitive to motives!";
      to_vict = "Your eyes become sensitive to motives.";
      break;

    case SPELL_DETECT_INVIS: //divination
      af[0].duration = 300 + magic_level * 25;
      SET_BIT_AR(af[0].bitvector, AFF_DETECT_INVIS);
      accum_duration = TRUE;
      to_vict = "Your eyes tingle, now sensitive to invisibility.";
      to_room = "$n's eyes become sensitive to invisibility!";
      break;

    case SPELL_DETECT_MAGIC: //divination
      af[0].duration = 300 + CASTER_LEVEL(ch) * 25;
      SET_BIT_AR(af[0].bitvector, AFF_DETECT_MAGIC);
      accum_duration = TRUE;
      to_room = "$n's eyes become sensitive to magic!";
      to_vict = "Magic becomes clear as your eyes tingle.";
      break;

    case SPELL_DIMENSIONAL_LOCK: //divination
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, 0)) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }

      af[0].duration = (divine_level + 12);
      SET_BIT_AR(af[0].bitvector, AFF_DIM_LOCK);
      to_room = "$n is bound to this dimension!";
      to_vict = "You feel yourself bound to this dimension!";
      break;

    case SPELL_DISPLACEMENT: //illusion
      af[0].location = APPLY_AC;
      af[0].modifier = -1;
      af[0].duration = 100;
      to_room = "$n's images becomes displaced!";
      to_vict = "You observe as your image becomes displaced!";
      SET_BIT_AR(af[0].bitvector, AFF_DISPLACE);
      accum_duration = FALSE;
      break;

    case SPELL_ENDURANCE: //transmutation
      af[0].location = APPLY_CON;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel more hardy!";
      to_room = "$n begins to feel more hardy!";
      break;

    case SPELL_ENDURE_ELEMENTS: //abjuration
      af[0].duration = 600;
      SET_BIT_AR(af[0].bitvector, AFF_ELEMENT_PROT);
      to_vict = "You feel a slight protection from the elements!";
      to_room = "$n begins to feel slightly protected from the elements!";
      break;

    case SPELL_ENFEEBLEMENT: //enchantment
      if (mag_resistance(ch, victim, 0))
        return;

      if (mag_savingthrow(ch, victim, SAVING_FORT, (elf_bonus-4))) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }

      af[0].location = APPLY_STR;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = -(2 + (CASTER_LEVEL(ch) / 5));

      af[1].location = APPLY_DEX;
      af[1].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[1].modifier = -(2 + (CASTER_LEVEL(ch) / 5));

      af[2].location = APPLY_CON;
      af[2].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[2].modifier = -(2 + (CASTER_LEVEL(ch) / 5));

      accum_duration = FALSE;
      accum_affect = FALSE;
      to_room = "$n is terribly enfeebled!";
      to_vict = "You feel terribly enfeebled!";
      break;

    case SPELL_ENLARGE_PERSON: //transmutation
      af[0].location = APPLY_SIZE;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 1;
      to_vict = "You feel yourself growing!";
      to_room = "$n's begins to grow much larger!";
      break;

    case SPELL_EPIC_MAGE_ARMOR: //epic
      if (isMagicArmored(victim))
        return;

      af[0].location = APPLY_AC_NEW;
      af[0].modifier = 12;
      af[0].duration = 1200;
      af[1].location = APPLY_DEX;
      af[1].modifier = 7;
      af[1].duration = 1200;
      accum_duration = FALSE;
      to_vict = "You feel magic protecting you.";
      to_room = "$n is surrounded by magical bands of armor!";
      break;

    case SPELL_EPIC_WARDING: //no school
      if (affected_by_spell(victim, SPELL_STONESKIN) ||
              affected_by_spell(victim, SPELL_IRONSKIN)) {
        send_to_char(ch, "A magical ward is already in effect on target.\r\n");
        return;
      }
      af[0].location = APPLY_AC;
      af[0].modifier = -1;
      af[0].duration = 600;
      to_room = "$n becomes surrounded by a powerful magical ward!";
      to_vict = "You become surrounded by a powerful magical ward!";
      GET_STONESKIN(victim) = MIN(700, CASTER_LEVEL(ch) * 60);
      break;

    case SPELL_EXPEDITIOUS_RETREAT: //transmutation
      af[0].location = APPLY_MOVE;
      af[0].modifier = 20 + magic_level;
      af[0].duration = magic_level * 2;
      to_vict = "You feel expeditious.";
      to_room = "$n is now expeditious!";
      break;

    case SPELL_EYEBITE: //necromancy
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_FORT, 0)) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }

      af[0].duration = CASTER_LEVEL(ch) * 1000;
      SET_BIT_AR(af[0].bitvector, AFF_DISEASE);
      to_vict = "You feel a powerful necromantic disease overcome you.";
      to_room =
              "$n suffers visibly as a powerful necromantic disease strikes $m!";
      break;

    case SPELL_FAERIE_FIRE: // evocation
      if (mag_resistance(ch, victim, 0))
        return;
      
      // need to make this show an outline around concealed, blue, displaced, invisible people
      SET_BIT_AR(af[0].bitvector, AFF_FAERIE_FIRE);
      af[0].duration = magic_level;
      accum_duration = FALSE;
      accum_affect = FALSE;
      to_room = "A pale blue light begins to glow around $n.";
      to_vict = "You are suddenly surrounded by a pale blue light.";
      break;
       
    case SPELL_FALSE_LIFE: //necromancy
      af[1].location = APPLY_HIT;
      af[1].modifier = 30;
      af[1].duration = 300;

      accum_duration = TRUE;
      to_room = "$n grows strong with \tDdark\tn life!";
      to_vict = "You grow strong with \tDdark\tn life!";
      break;
     
    case SPELL_FEEBLEMIND: //enchantment
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, elf_bonus)) {
        return;
      }
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }

      is_mind_affect = TRUE;

      af[0].location = APPLY_INT;
      af[0].duration = magic_level;
      af[0].modifier = -((victim->real_abils.intel) - 3);

      af[1].location = APPLY_WIS;
      af[1].duration = magic_level;
      af[1].modifier = -((victim->real_abils.wis) - 3);

      to_room = "$n grasps $s head in pain, $s eyes glazing over!";
      to_vict = "Your head starts to throb and a wave of confusion washes over you.";
      break;

    case SPELL_FIRE_SHIELD: //evocation
      if (affected_by_spell(victim, SPELL_ACID_SHEATH) ||
              affected_by_spell(victim, SPELL_COLD_SHIELD)) {
        send_to_char(ch, "You are already affected by an elemental shield!\r\n");
        return;
      }
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_FSHIELD);

      accum_duration = FALSE;
      to_vict = "A shield of flames surrounds you.";
      to_room = "$n is surrounded by shield of flames.";
      break;

    case SPELL_FLY:
      af[0].duration = 600;
      SET_BIT_AR(af[0].bitvector, AFF_FLYING);
      accum_duration = TRUE;
      to_room = "$n begins to fly above the ground!";
      to_vict = "You fly above the ground.";
      break;

    case SPELL_FREE_MOVEMENT:
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_FREE_MOVEMENT);

      accum_duration = FALSE;
      to_vict = "Your limbs feel looser as the free movement spell takes effect.";
      to_room = "$n's limbs now move freer.";
      break;

    case SPELL_GLOBE_OF_INVULN: //abjuration
      if (affected_by_spell(victim, SPELL_MINOR_GLOBE)) {
        send_to_char(ch, "You are already affected by a globe spell!\r\n");
        return;
      }
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_GLOBE_OF_INVULN);

      accum_duration = FALSE;
      to_vict = "A globe of invulnerability surrounds you.";
      to_room = "$n is surrounded by a globe of invulnerability.";
      break;

    case SPELL_GRACE: //transmutation
      af[0].location = APPLY_DEX;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel more dextrous!";
      to_room = "$n's appears to be more dextrous!";
      break;

    case SPELL_GRASPING_HAND: //evocation (also does damage)
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_REFL, 0))
        return;

      SET_BIT_AR(af[0].bitvector, AFF_GRAPPLED);
      af[0].duration = dice(2, 4) - 1;
      accum_duration = FALSE;
      to_room = "$n's is grasped by the spell!";
      to_vict = "You are grasped by the magical hand!";
      break;

    case SPELL_GREASE: //divination
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_REFL, 0)) {
        send_to_char(ch, "You fail.\r\n");
        return;
      }

      af[0].location = APPLY_MOVE;
      af[0].modifier = -20 - magic_level;
      af[0].duration = magic_level * 2;
      to_vict = "Your feet are all greased up!";
      to_room = "$n now has greasy feet!";
      break;

    case SPELL_GREATER_HEROISM: //enchantment
      if (affected_by_spell(victim, SPELL_HEROISM)) {
        send_to_char(ch, "The target is already heroic!\r\n");
        return;
      }
      af[0].location = APPLY_HITROLL;
      af[0].modifier = 4;
      af[0].duration = 300;

      af[1].location = APPLY_SAVING_WILL;
      af[1].modifier = 4;
      af[1].duration = 300;

      af[2].location = APPLY_SAVING_FORT;
      af[2].modifier = 4;
      af[2].duration = 300;

      af[3].location = APPLY_SAVING_REFL;
      af[3].modifier = 4;
      af[3].duration = 300;

      to_room = "$n is now very heroic!";
      to_vict = "You feel very heroic.";
      break;

    case SPELL_GREATER_INVIS: //illusion
      if (!victim)
        victim = ch;

      af[0].duration = 10 + (magic_level * 6);
      af[0].modifier = 4;
      af[0].location = APPLY_AC_NEW;
      SET_BIT_AR(af[0].bitvector, AFF_INVISIBLE);
      accum_duration = TRUE;
      to_vict = "You vanish.";
      to_room = "$n slowly fades out of existence.";
      break;

    case SPELL_GREATER_MAGIC_FANG:
      if (!IS_NPC(victim) || GET_RACE(victim) != NPCRACE_ANIMAL) {
        send_to_char(ch, "Magic fang can only be cast upon animals.\r\n");
        return;
      }
      af[0].location = APPLY_HITROLL;
      if (CLASS_LEVEL(ch, CLASS_DRUID) >= 20)
        af[0].modifier = 5;
      else if (CLASS_LEVEL(ch, CLASS_DRUID) >= 16)
        af[0].modifier = 4;
      else if (CLASS_LEVEL(ch, CLASS_DRUID) >= 12)
        af[0].modifier = 3;
      else if (CLASS_LEVEL(ch, CLASS_DRUID) >= 8)
        af[0].modifier = 2;
      else
        af[0].modifier = 1;
      af[0].duration = 5 * magic_level;

      accum_duration = TRUE;
      to_room = "$n is now affected by magic fang!";
      to_vict = "You are suddenly empowered by magic fang.";
      break;
      
    case SPELL_GREATER_MIRROR_IMAGE: //illusion
      if (affected_by_spell(victim, SPELL_MIRROR_IMAGE) ||
              affected_by_spell(victim, SPELL_GREATER_MIRROR_IMAGE)) {
        send_to_char(ch, "You already have mirror images!\r\n");
        return;
      }
      af[0].location = APPLY_AC;
      af[0].modifier = -1;
      af[0].duration = 300;
      to_room = "$n grins as multiple images pop up and smile!";
      to_vict = "You watch as multiple images pop up and smile at you!";
      GET_IMAGES(victim) = 6 + (magic_level / 3);
      break;

    case SPELL_GREATER_SPELL_MANTLE: //abjuration
      if (affected_by_spell(victim, SPELL_MANTLE)) {
        send_to_char(ch, "A magical mantle is already in effect on target.\r\n");
        return;
      }
      
      af[0].duration = magic_level * 4;
      SET_BIT_AR(af[0].bitvector, AFF_SPELL_MANTLE);
      GET_SPELL_MANTLE(victim) = 4;
      accum_duration = FALSE;
      to_room = "$n begins to shimmer from a greater magical mantle!";
      to_vict = "You begin to shimmer from a greater magical mantle.";
      break;

    case SPELL_HALT_UNDEAD: //necromancy
      if (!IS_UNDEAD(victim)) {
        send_to_char(ch, "Your target is not undead.\r\n");
        return;
      }
      if (GET_LEVEL(victim) > 11) {
        send_to_char(ch, "Your target is too powerful to be affected by this enchantment.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, 0)) {
        return;
      }

      SET_BIT_AR(af[0].bitvector, AFF_PARALYZED);
      af[0].duration = dice(3, 3);
      to_room = "$n is overcome by a powerful hold spell!";
      to_vict = "You are overcome by a powerful hold spell!";
      break;
      
    case SPELL_HASTE: //abjuration
      if (affected_by_spell(victim, SPELL_SLOW)) {
        affect_from_char(victim, SPELL_SLOW);
        send_to_char(ch, "You dispel the slow spell!\r\n");
        send_to_char(victim, "Your slow spell is dispelled!\r\n");
        return;
      }

      af[0].duration = (magic_level * 12);
      SET_BIT_AR(af[0].bitvector, AFF_HASTE);
      to_room = "$n begins to speed up!";
      to_vict = "You begin to speed up!";
      break;

    case SPELL_HEROISM: //necromancy
      if (affected_by_spell(victim, SPELL_GREATER_HEROISM)) {
        send_to_char(ch, "The target is already heroic!\r\n");
        return;
      }
      af[0].location = APPLY_HITROLL;
      af[0].modifier = 2;
      af[0].duration = 300;

      af[1].location = APPLY_SAVING_WILL;
      af[1].modifier = 2;
      af[1].duration = 300;

      af[2].location = APPLY_SAVING_FORT;
      af[2].modifier = 2;
      af[2].duration = 300;

      af[3].location = APPLY_SAVING_REFL;
      af[3].modifier = 2;
      af[3].duration = 300;

      to_room = "$n is now heroic!";
      to_vict = "You feel heroic.";
      break;

    case SPELL_HIDEOUS_LAUGHTER: //enchantment
      if (GET_LEVEL(victim) > 8) {
        send_to_char(ch, "Your target is too powerful to be affected by this illusion.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, elf_bonus)) {
        return;
      }
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }

      is_mind_affect = TRUE;

      SET_BIT_AR(af[0].bitvector, AFF_PARALYZED);
      af[0].duration = dice(1, 4);
      to_room = "$n is overcome by a fit of hideous laughter!";
      to_vict = "You are overcome by a fit of hideous luaghter!";
      break;

    case SPELL_HOLD_ANIMAL: // enchantment
      if (!IS_NPC(victim) || GET_RACE(victim) != NPCRACE_ANIMAL) {
        send_to_char(ch, "This spell is only effective on animals.\r\n");
        return;
      }
      if (GET_LEVEL(victim) > 11) {
        send_to_char(ch, "Your target is too powerful to be affected by this enchantment.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, elf_bonus)) {
        return;
      }

      SET_BIT_AR(af[0].bitvector, AFF_PARALYZED);
      af[0].duration = divine_level; // one round per level
      to_room = "$n is overcome by a powerful hold spell!";
      to_vict = "You are overcome by a powerful hold spell!";
      break;
      
    case SPELL_HOLD_PERSON: //enchantment
      if (GET_LEVEL(victim) > 11) {
        send_to_char(ch, "Your target is too powerful to be affected by this enchantment.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, elf_bonus)) {
        return;
      }

      SET_BIT_AR(af[0].bitvector, AFF_PARALYZED);
      af[0].duration = dice(3, 3);
      to_room = "$n is overcome by a powerful hold spell!";
      to_vict = "You are overcome by a powerful hold spell!";
      break;

    case SPELL_HORIZIKAULS_BOOM:
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_FORT, 0)) {
        return;
      }

      SET_BIT_AR(af[0].bitvector, AFF_DEAF);
      af[0].duration = dice(2, 4);
      to_room = "$n is deafened by the blast!";
      to_vict = "You feel deafened by the blast!";
      break;

    case SPELL_INFRAVISION: //divination, shared
      af[0].duration = 300 + CASTER_LEVEL(ch) * 25;
      SET_BIT_AR(af[0].bitvector, AFF_INFRAVISION);
      accum_duration = TRUE;
      to_vict = "Your eyes glow red.";
      to_room = "$n's eyes glow red.";
      break;

    case SPELL_INTERPOSING_HAND: //evocation
      if (mag_resistance(ch, victim, 0))
        return;
      // no save

      af[0].location = APPLY_HITROLL;
      af[0].duration = 4 + magic_level;
      af[0].modifier = -4;

      to_room = "A disembodied hand moves in front of $n!";
      to_vict = "A disembodied hand moves in front of you!";
      break;

    case SPELL_INVISIBLE: //illusion
      if (!victim)
        victim = ch;

      af[0].duration = 300 + (magic_level * 6);
      af[0].modifier = 4;
      af[0].location = APPLY_AC_NEW;
      SET_BIT_AR(af[0].bitvector, AFF_INVISIBLE);
      accum_duration = TRUE;
      to_vict = "You vanish.";
      to_room = "$n slowly fades out of existence.";
      break;

    case SPELL_IRON_GUTS: //transmutation
      af[0].location = APPLY_SAVING_FORT;
      af[0].modifier = 3;
      af[0].duration = 300;

      to_room = "$n now has guts tough as iron!";
      to_vict = "You feel like your guts are tough as iron!";
      break;

    case SPELL_IRONSKIN: //transmutation
      if (affected_by_spell(victim, SPELL_STONESKIN) ||
              affected_by_spell(victim, SPELL_EPIC_WARDING)) {
        send_to_char(ch, "A magical ward is already in effect on target.\r\n");
        return;
      }
      af[0].location = APPLY_AC;
      af[0].modifier = -1;
      af[0].duration = 600;
      to_room = "$n's skin takes on the texture of iron!";
      to_vict = "Your skin takes on the texture of iron!";
      GET_STONESKIN(victim) = MIN(450, CASTER_LEVEL(ch) * 35);
      break;

    case SPELL_IRRESISTIBLE_DANCE: //enchantment
      if (mag_resistance(ch, victim, 0))
        return;
      // no save

      SET_BIT_AR(af[0].bitvector, AFF_PARALYZED);
      af[0].duration = dice(1, 4) + 1;
      to_room = "$n begins to dance uncontrollably!";
      to_vict = "You begin to dance uncontrollably!";
      break;

    case SPELL_JUMP: // transmutation
      af[0].duration = CLASS_LEVEL(ch, CLASS_DRUID);

      accum_affect = FALSE;
      accum_duration = FALSE;
      to_room = "$n feels much lighter on $s feet.";
      to_vict = "You feel much lighter on your feet.";
      break;

    case SPELL_MAGE_ARMOR: //conjuration
      if (isMagicArmored(victim))
        return;

      af[0].location = APPLY_AC_NEW;
      af[0].modifier = 2;
      af[0].duration = 600;
      accum_duration = FALSE;
      to_vict = "You feel someone protecting you.";
      to_room = "$n is surrounded by magical armor!";
      break;

    case SPELL_MAGIC_FANG:
      if (!IS_NPC(victim) || GET_RACE(victim) != NPCRACE_ANIMAL) {
        send_to_char(ch, "Magic fang can only be cast upon animals.\r\n");
        return;
      }
      af[0].location = APPLY_HITROLL;
      af[0].modifier = 1;
      af[0].duration = magic_level;

      accum_duration = TRUE;
      to_room = "$n is now affected by magic fang!";
      to_vict = "You are suddenly empowered by magic fang.";
      break;
      
    case SPELL_MASS_CHARISMA: //transmutation
      if (affected_by_spell(victim, SPELL_CHARISMA)) {
        send_to_char(ch, "Your target already has a charisma spell in effect.\r\n");
        return;
      }
      af[0].location = APPLY_CHA;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel more charismatic!";
      to_room = "$n's charisma increases!";
      break;

    case SPELL_MASS_CUNNING: //transmutation
      if (affected_by_spell(victim, SPELL_CUNNING)) {
        send_to_char(ch, "Your target already has a cunning spell in effect.\r\n");
        return;
      }

      af[0].location = APPLY_INT;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel more intelligent!";
      to_room = "$n's intelligence increases!";
      break;

    case SPELL_MASS_ENDURANCE: //transmutation
      af[0].location = APPLY_CON;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel more hardy!";
      to_room = "$n's begins to feel more hardy!";
      break;

    case SPELL_MASS_ENHANCE: //transmutation
      if (affected_by_spell(victim, SPELL_GRACE) ||
          affected_by_spell(victim, SPELL_ENDURANCE) ||
          affected_by_spell(victim, SPELL_STRENGTH)    
              ) {
        send_to_char(ch, "Your target already has a physical enhancement spell in effect.\r\n");
        return;
      }
      
      af[0].location = APPLY_STR;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      
      af[1].location = APPLY_DEX;
      af[1].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[1].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      
      af[2].location = APPLY_CON;
      af[2].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[2].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      
      accum_duration = TRUE;      
      to_vict = "You feel your physical atributes enhanced!";
      to_room = "$n's physical attributes are enhanced!";
      break;

    case SPELL_MASS_GRACE: //transmutation
      af[0].location = APPLY_DEX;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel more dextrous!";
      to_room = "$n's appears to be more dextrous!";
      break;

    case SPELL_MASS_HOLD_PERSON: //enchantment
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, elf_bonus)) {
        return;
      }

      SET_BIT_AR(af[0].bitvector, AFF_PARALYZED);
      af[0].duration = dice(3, 4);
      to_room = "$n is overcome by a powerful hold spell!";
      to_vict = "You are overcome by a powerful hold spell!";
      break;

    case SPELL_MASS_STRENGTH: //transmutation
      af[0].location = APPLY_STR;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel stronger!";
      to_room = "$n's muscles begin to bulge!";
      break;

    case SPELL_MASS_WISDOM: //transmutation
      af[0].location = APPLY_WIS;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel more wise!";
      to_room = "$n's wisdom increases!";
      break;

    case SPELL_MINOR_GLOBE: //abjuration
      if (affected_by_spell(victim, SPELL_GLOBE_OF_INVULN)) {
        send_to_char(ch, "You are already affected by a globe spell!\r\n");
        return;
      }
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_MINOR_GLOBE);

      accum_duration = FALSE;
      to_vict = "A minor globe of invulnerability surrounds you.";
      to_room = "$n is surrounded by a minor globe of invulnerability.";
      break;

    case SPELL_MIND_BLANK:  //abjuration
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_MIND_BLANK);

      accum_duration = FALSE;
      to_vict = "Your mind becomes blank from harmful magicks.";
      to_room = "$n's mind becomes blanked from harmful magicks.";
      break;

    case SPELL_MIND_FOG: //illusion
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, gnome_bonus)) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }

      is_mind_affect = TRUE;

      af[0].location = APPLY_SAVING_WILL;
      af[0].duration = 10 + magic_level;
      af[0].modifier = -10;
      to_room = "$n reels in confusion as a mind fog strikes $e!";
      to_vict = "You reel in confusion as a mind fog spell strikes you!";
      break;

    case SPELL_MIRROR_IMAGE: //illusion
      if (affected_by_spell(victim, SPELL_MIRROR_IMAGE) ||
              affected_by_spell(victim, SPELL_GREATER_MIRROR_IMAGE)) {
        send_to_char(ch, "You already have mirror images!\r\n");
        return;
      }
      af[0].location = APPLY_AC;
      af[0].modifier = -1;
      af[0].duration = 300;
      to_room = "$n grins as multiple images pop up and smile!";
      to_vict = "You watch as multiple images pop up and smile at you!";
      GET_IMAGES(victim) = 4 + MIN(5, (int) (magic_level / 3));
      break;

    case SPELL_NIGHTMARE: //illusion
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_FORT, gnome_bonus))
        return;
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }

      is_mind_affect = TRUE;

      SET_BIT_AR(af[0].bitvector, AFF_FATIGUED);
      GET_MOVE(victim) -= magic_level;
      af[0].duration = magic_level;
      to_room = "$n is overcome by overwhelming fatigue from the nightmare!";
      to_vict = "You are overcome by overwhelming fatigue from the nightmare!";
      break;

    case SPELL_NON_DETECTION:
      af[0].duration = 25 + (magic_level * 12);
      SET_BIT_AR(af[0].bitvector, AFF_NON_DETECTION);
      to_room = "$n briefly glows green!";
      to_vict = "You feel protection from scrying.";
      break;

    case SPELL_OBSCURING_MIST: // conjuration
      /* so right now this spell is simply 20% concealment to 1 char, needs
       * to be modified so that it actually creates an obscuring mist object in the room
       * and sets a room flag, which the room flag will determine the effects of the spell.
       * also, gust of wind, fireball, flamestrike, etc. will disperse the mist when cast,
       * or even a strong wind in the weather...
       */
      if (SECT(ch->in_room) == SECT_UNDERWATER) {
        send_to_char(ch, "The obscuring mist quickly disappears under the water.\r\n");
        return;
      }
      
      af[0].duration = divine_level;
      to_room = "An obscuring mist suddenly fills the room!";
      to_vict = "An obscruring mist suddenly surrounds you.";
      break;

    case SPELL_POISON: //enchantment, shared
      if (mag_resistance(ch, victim, 0))
        return;
      int bonus = 0;
      if (GET_RACE(ch) == RACE_DWARF || //dwarf dwarven poison resist
              GET_RACE(ch) == RACE_CRYSTAL_DWARF)
        bonus += 2;
      if (mag_savingthrow(ch, victim, SAVING_FORT, bonus)) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }

      af[0].location = APPLY_STR;
      af[0].duration = CASTER_LEVEL(ch) * 25;
      af[0].modifier = -2;
      SET_BIT_AR(af[0].bitvector, AFF_POISON);
      to_vict = "You feel very sick.";
      to_room = "$n gets violently ill!";
      break;

    case SPELL_POWER_WORD_BLIND: //necromancy
      if (MOB_FLAGGED(victim, MOB_NOBLIND)) {
        send_to_char(ch, "Your opponent doesn't seem blindable.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_REFL, -4)) {
        send_to_char(ch, "You fail.\r\n");
        return;
      }

      af[0].location = APPLY_HITROLL;
      af[0].modifier = -4;
      af[0].duration = 200;
      SET_BIT_AR(af[0].bitvector, AFF_BLIND);

      af[1].location = APPLY_AC_NEW;
      af[1].modifier = -4;
      af[1].duration = 200;
      SET_BIT_AR(af[1].bitvector, AFF_BLIND);

      to_room = "$n seems to be blinded!";
      to_vict = "You have been blinded!";
      break;

    case SPELL_POWER_WORD_STUN: //divination
      if (mag_resistance(ch, victim, 0))
        return;
      // no save

      SET_BIT_AR(af[0].bitvector, AFF_STUN);
      af[0].duration = dice(1, 4);
      to_room = "$n is stunned by a powerful magical word!";
      to_vict = "You are stunned by a powerful magical word!";
      break;

    case SPELL_PRAYER:
      if (affected_by_spell(victim, SPELL_BLESS) ||
              affected_by_spell(victim, SPELL_AID)) {
        send_to_char(ch, "The target is already blessed!\r\n");
        return;
      }
      
      af[0].location = APPLY_HITROLL;
      af[0].modifier = 5;
      af[0].duration = 300;

      af[1].location = APPLY_DAMROLL;
      af[1].modifier = 5;
      af[1].duration = 300;

      af[2].location = APPLY_SAVING_WILL;
      af[2].modifier = 3;
      af[2].duration = 300;

      af[3].location = APPLY_SAVING_FORT;
      af[3].modifier = 3;
      af[3].duration = 300;

      af[4].location = APPLY_SAVING_REFL;
      af[4].modifier = 3;
      af[4].duration = 300;

      af[5].location = APPLY_HIT;
      af[5].modifier = dice(4, 12) + divine_level;
      af[5].duration = 300;

      accum_duration = TRUE;
      to_room = "$n is now divinely blessed and aided!";
      to_vict = "You feel divinely blessed and aided.";
      break;

    case SPELL_PRISMATIC_SPRAY: //illusion, does damage too
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, gnome_bonus)) {
        return;
      }

      switch (dice(1, 4)) {
        case 1:
          SET_BIT_AR(af[0].bitvector, AFF_STUN);
          af[0].duration = dice(2, 4);
          to_room = "$n is stunned by the colors!";
          to_vict = "You are stunned by the colors!";
          break;
        case 2:
          SET_BIT_AR(af[0].bitvector, AFF_PARALYZED);
          af[0].duration = dice(1, 6);
          to_room = "$n is paralyzed by the colors!";
          to_vict = "You are paralyzed by the colors!";
          break;
        case 3:
          af[0].location = APPLY_HITROLL;
          af[0].modifier = -4;
          af[0].duration = 25;
          SET_BIT_AR(af[0].bitvector, AFF_BLIND);

          af[1].location = APPLY_AC_NEW;
          af[1].modifier = -4;
          af[1].duration = 25;
          SET_BIT_AR(af[1].bitvector, AFF_BLIND);

          to_room = "$n seems to be blinded by the colors!";
          to_vict = "You have been blinded by the colors!";

          break;
        case 4:
          af[0].duration = magic_level;
          SET_BIT_AR(af[0].bitvector, AFF_SLOW);
          to_room = "$n begins to slow down from the prismatic spray!";
          to_vict = "You feel yourself slow down because of the prismatic spray!";

          break;
      }
      break;

    case SPELL_PROT_FROM_EVIL: // abjuration
      af[0].duration = 600;
      SET_BIT_AR(af[0].bitvector, AFF_PROTECT_EVIL);
      accum_duration = TRUE;
      to_vict = "You feel invulnerable to evil!";
      break;

    case SPELL_PROT_FROM_GOOD: // abjuration
      af[0].duration = 600;
      SET_BIT_AR(af[0].bitvector, AFF_PROTECT_GOOD);
      accum_duration = TRUE;
      to_vict = "You feel invulnerable to good!";
      break;

    case SPELL_PROTECT_FROM_SPELLS: //divination
      af[1].location = APPLY_SAVING_WILL;
      af[1].modifier = 1;
      af[1].duration = 100;

      to_room = "$n is now protected from spells!";
      to_vict = "You feel protected from spells!";
      break;

    case SPELL_RAINBOW_PATTERN: //illusion
      if (GET_LEVEL(victim) > 13) {
        send_to_char(ch, "Your target is too powerful to be affected by this illusion.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, gnome_bonus)) {
        return;
      }
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }

      is_mind_affect = TRUE;

      SET_BIT_AR(af[0].bitvector, AFF_STUN);
      af[0].duration = dice(3, 4);
      to_room = "$n is stunned by the pattern of bright colors!";
      to_vict = "You are dazed by the pattern of bright colors!";
      break;

    case SPELL_RAY_OF_ENFEEBLEMENT: //necromancy
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_REFL, 0)) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }

      af[0].location = APPLY_STR;
      af[0].duration = 25 + (magic_level * 12);
      af[0].modifier = -dice(2, 4);
      accum_duration = TRUE;
      to_room = "$n is struck by enfeeblement!";
      to_vict = "You feel enfeebled!";
      break;

    case SPELL_REGENERATION:
      af[0].duration = 100;
      SET_BIT_AR(af[0].bitvector, AFF_REGEN);

      accum_duration = FALSE;
      to_vict = "You begin regenerating.";
      to_room = "$n begins regenerating.";
      break;

    case SPELL_RESIST_ENERGY: //abjuration
      af[0].duration = 600;
      SET_BIT_AR(af[0].bitvector, AFF_ELEMENT_PROT);
      to_vict = "You feel a slight protection from energy!";
      to_room = "$n begins to feel slightly protected from energy!";
      break;

    case SPELL_SANCTUARY:
      af[0].duration = 100;
      SET_BIT_AR(af[0].bitvector, AFF_SANCTUARY);

      accum_duration = FALSE;
      to_vict = "A white aura momentarily surrounds you.";
      to_room = "$n is surrounded by a white aura.";
      break;

    case SPELL_SCARE: //illusion
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, gnome_bonus)) {
        return;
      }
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }
      is_mind_affect = TRUE;

      if (GET_LEVEL(victim) >= 7) {
        send_to_char(ch, "The victim is too powerful for this illusion!\r\n");
        return;
      }

      SET_BIT_AR(af[0].bitvector, AFF_FEAR);
      af[0].duration = dice(2, 6);
      to_room = "$n is imbued with fear!";
      to_vict = "You feel scared and fearful!";
      break;

    case SPELL_SCINT_PATTERN: //illusion
      if (mag_resistance(ch, victim, 0))
        return;
      // no save

      SET_BIT_AR(af[0].bitvector, AFF_CONFUSED);
      af[0].duration = dice(2, 4) + 2;
      to_room = "$n is confused by the scintillating pattern!";
      to_vict = "You are confused by the scintillating pattern!";
      break;

    case SPELL_SENSE_LIFE:
      to_vict = "Your feel your awareness improve.";
      to_room = "$n's eyes become aware of life forms!";
      af[0].duration = divine_level * 25;
      SET_BIT_AR(af[0].bitvector, AFF_SENSE_LIFE);
      accum_duration = TRUE;
      break;

    case SPELL_SHADOW_SHIELD: //illusion
      if (isMagicArmored(victim))
        return;
      
      af[0].location = APPLY_AC_NEW;
      af[0].modifier = 5;
      af[0].duration = magic_level * 5;

      af[1].duration = magic_level * 5;
      SET_BIT_AR(af[1].bitvector, AFF_SHADOW_SHIELD);
      // this affect gives:  12 DR, 100% resist negative damage

      to_vict = "You feel someone protecting you with the shadows.";
      to_room = "$n is surrounded by a shadowy shield!";
      break;

    case SPELL_SHIELD: //transmutation
      if (isMagicArmored(victim))
        return;

      af[0].location = APPLY_AC_NEW;
      af[0].modifier = 2;
      af[0].duration = 300;
      to_vict = "You feel someone protecting you.";
      to_room = "$n is surrounded by magical armor!";
      break;

    case SPELL_SHRINK_PERSON: //transmutation
      af[0].location = APPLY_SIZE;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = -1;
      to_vict = "You feel yourself shrinking!";
      to_room = "$n's begins to shrink to being much smaller!";
      break;

    case SPELL_SLEEP: //enchantment
      if (GET_LEVEL(victim) >= 7 || (!IS_NPC(victim) && GET_RACE(victim) == RACE_ELF)) {
        send_to_char(ch, "The target is too powerful for this enchantment!\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (!CONFIG_PK_ALLOWED && !IS_NPC(ch) && !IS_NPC(victim))
        return;
      if (MOB_FLAGGED(victim, MOB_NOSLEEP)) {
        send_to_char(ch, "Your victim doesn't seem vulnerable to your spell.");
        return;
      }
      if (mag_savingthrow(ch, victim, SAVING_WILL, 0)) {
        return;
      }

      af[0].duration = 100 + (magic_level * 6);
      SET_BIT_AR(af[0].bitvector, AFF_SLEEP);

      if (GET_POS(victim) > POS_SLEEPING) {
        send_to_char(victim, "You feel very sleepy...  Zzzz......\r\n");
        act("$n goes to sleep.", TRUE, victim, 0, 0, TO_ROOM);
        if (FIGHTING(victim))
          stop_fighting(victim);
        GET_POS(victim) = POS_SLEEPING;
        if (FIGHTING(ch) == victim)
          stop_fighting(ch);
      }
      break;

    case SPELL_SLOW: //abjuration
      if (affected_by_spell(victim, SPELL_HASTE)) {
        affect_from_char(victim, SPELL_HASTE);
        send_to_char(ch, "You dispel the haste spell!\r\n");
        send_to_char(victim, "Your haste spell is dispelled!\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_REFL, 0)) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }

      af[0].duration = (magic_level * 12);
      SET_BIT_AR(af[0].bitvector, AFF_SLOW);
      to_room = "$n begins to slow down!";
      to_vict = "You feel yourself slow down!";
      break;

    case SPELL_SPELL_MANTLE: //abjuration
      if (affected_by_spell(victim, SPELL_GREATER_SPELL_MANTLE)) {
        send_to_char(ch, "A magical mantle is already in effect on target.\r\n");
        return;
      }
      
      af[0].duration = magic_level * 3;
      SET_BIT_AR(af[0].bitvector, AFF_SPELL_MANTLE);
      GET_SPELL_MANTLE(victim) = 2;
      accum_duration = FALSE;
      to_room = "$n begins to shimmer from a magical mantle!";
      to_vict = "You begin to shimmer from a magical mantle.";
      break;

    case SPELL_SPELL_RESISTANCE:
      af[0].duration = 50 + divine_level;
      SET_BIT_AR(af[0].bitvector, AFF_SPELL_RESISTANT);

      accum_duration = FALSE;
      to_vict = "You feel your spell resistance increase.";
      to_room = "$n's spell resistance increases.";
      break;

    case SPELL_SPELL_TURNING:  //abjuration
      af[0].duration = 100;
      SET_BIT_AR(af[0].bitvector, AFF_SPELL_TURNING);

      accum_duration = FALSE;
      to_vict = "A spell-turning shield surrounds you.";
      to_room = "$n is surrounded by a spell turning shield.";
      break;

    case SPELL_STENCH:
      if (GET_LEVEL(victim) >= 9) {
        return;
      }
      if (mag_savingthrow(ch, victim, SAVING_FORT, 0)) {
        return;
      }

      SET_BIT_AR(af[0].bitvector, AFF_NAUSEATED);
      af[0].duration = 3;
      to_room = "$n becomes nauseated from the stinky fumes!";
      to_vict = "You become nauseated from the stinky fumes!";
      break;

    case SPELL_STONESKIN:
      if (affected_by_spell(victim, SPELL_EPIC_WARDING) ||
              affected_by_spell(victim, SPELL_IRONSKIN)) {
        send_to_char(ch, "A magical ward is already in effect on target.\r\n");
        return;
      }
      af[0].location = APPLY_AC;
      af[0].modifier = -1;
      af[0].duration = 600;
      to_room = "$n's skin becomes hard as rock!";
      to_vict = "Your skin becomes hard as stone.";
      /* using level variable here for weapon spells and druid compatibility */
      GET_STONESKIN(victim) = MIN(225, level * 15);
      break;

    case SPELL_STRENGTH: //transmutation
      af[0].location = APPLY_STR;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 2 + (CASTER_LEVEL(ch) / 5);
      accum_duration = TRUE;
      to_vict = "You feel stronger!";
      to_room = "$n's muscles begin to bulge!";
      break;
      
    case SPELL_STRENGTHEN_BONE:
      if (!IS_UNDEAD(victim))
        return;

      af[0].location = APPLY_AC_NEW;
      af[0].modifier = 2;
      af[0].duration = 600;
      accum_duration = TRUE;
      to_vict = "You feel your bones harden.";
      to_room = "$n's bones harden!";
      break;

    case SPELL_SUNBEAM: // evocation[light]
    case SPELL_SUNBURST: // divination, does damage and room affect
      if (MOB_FLAGGED(victim, MOB_NOBLIND)) {
        send_to_char(ch, "Your opponent doesn't seem blindable.\r\n");
        return;
      }
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_REFL, 0)) {
        return;
      }

      af[0].location = APPLY_HITROLL;
      af[0].modifier = -4;
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_BLIND);

      af[1].location = APPLY_AC_NEW;
      af[1].modifier = -4;
      af[1].duration = 50;
      SET_BIT_AR(af[1].bitvector, AFF_BLIND);

      to_room = "$n seems to be blinded!";
      to_vict = "You have been blinded!";
      break;

    case SPELL_THUNDERCLAP: //abjuration
      success = 0;

      if (!MOB_FLAGGED(victim, MOB_NODEAF) &&
              !mag_savingthrow(ch, victim, SAVING_FORT, 0) &&
              !mag_resistance(ch, victim, 0)) {
        af[0].duration = 10;
        SET_BIT_AR(af[0].bitvector, AFF_DEAF);

        act("You have been deafened!", FALSE, victim, 0, ch, TO_CHAR);
        act("$n seems to be deafened!", TRUE, victim, 0, ch, TO_ROOM);
        success = 1;
      }

      if (!mag_savingthrow(ch, victim, SAVING_WILL, 0) &&
              !mag_resistance(ch, victim, 0)) {
        af[1].duration = 4;
        SET_BIT_AR(af[1].bitvector, AFF_STUN);

        act("You have been stunned!", FALSE, victim, 0, ch, TO_CHAR);
        act("$n seems to be stunned!", TRUE, victim, 0, ch, TO_ROOM);
        success = 1;
      }

      if (!mag_savingthrow(ch, victim, SAVING_REFL, 0) &&
              !mag_resistance(ch, victim, 0)) {
        GET_POS(victim) = POS_SITTING;
        SET_WAIT(victim, PULSE_VIOLENCE * 1);

        act("You have been knocked down!", FALSE, victim, 0, ch, TO_CHAR);
        act("$n is knocked down!", TRUE, victim, 0, ch, TO_ROOM);
      }

      if (!success)
        return;
      break;

    case SPELL_TIMESTOP:  //abjuration
      af[0].duration = 7;
      SET_BIT_AR(af[0].bitvector, AFF_TIME_STOPPED);

      accum_duration = FALSE;
      to_vict = "The world around starts moving very slowly.";
      to_room = "$n begins to move outside of time.";
      break;

    case SPELL_TOUCH_OF_IDIOCY: //enchantment
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_WILL, elf_bonus)) {
        send_to_char(ch, "%s", CONFIG_NOEFFECT);
        return;
      }
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }

      is_mind_affect = TRUE;

      af[0].location = APPLY_INT;
      af[0].duration = 25 + (magic_level * 12);
      af[0].modifier = -(dice(1, 6));

      af[1].location = APPLY_WIS;
      af[1].duration = 25 + (magic_level * 12);
      af[1].modifier = -(dice(1, 6));

      af[2].location = APPLY_CHA;
      af[2].duration = 25 + (magic_level * 12);
      af[2].modifier = -(dice(1, 6));

      accum_duration = TRUE;
      accum_affect = FALSE;
      to_room = "A look of idiocy crosses $n's face!";
      to_vict = "You feel very idiotic.";
      break;

    case SPELL_TRANSFORMATION: //necromancy
      af[0].duration = 50;
      SET_BIT_AR(af[0].bitvector, AFF_TFORM);

      accum_duration = FALSE;
      to_vict = "You feel your combat skill increase!";
      to_room = "The combat skill of $n increases!";
      break;

    case SPELL_TRUE_SEEING: //divination
      af[0].duration = 20 + magic_level;
      SET_BIT_AR(af[0].bitvector, AFF_TRUE_SIGHT);
      to_vict = "Your eyes tingle, now with true-sight.";
      to_room = "$n's eyes become enhanced with true-sight!";
      break;

    case SPELL_TRUE_STRIKE: //illusion
      af[0].location = APPLY_HITROLL;
      af[0].duration = (magic_level * 12) + 100;
      af[0].modifier = 20;
      accum_duration = TRUE;
      to_vict = "You feel able to strike true!";
      to_room = "$n is now able to strike true!";
      break;

    case SPELL_WAIL_OF_THE_BANSHEE: //necromancy (does damage too)
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_FORT, 0)) {
        return;
      }
      if (AFF_FLAGGED(victim, AFF_MIND_BLANK)) {
        send_to_char(ch, "Mind blank protects %s!", GET_NAME(victim));
        send_to_char(victim, "Mind blank protects you from %s!",
                GET_NAME(ch));
        return;
      }
      is_mind_affect = TRUE;

      SET_BIT_AR(af[0].bitvector, AFF_FEAR);
      af[0].duration = dice(2, 6);
      to_room = "$n is imbued with fear!";
      to_vict = "You feel scared and fearful!";
      break;

    case SPELL_WATER_BREATHE:
      af[0].duration = 600;
      SET_BIT_AR(af[0].bitvector, AFF_SCUBA);
      accum_duration = TRUE;
      to_vict = "You feel gills grow behind your neck.";
      to_room = "$n's neck grows gills!";
      break;

    case SPELL_WATERWALK: //transmutation
      af[0].duration = 600;
      SET_BIT_AR(af[0].bitvector, AFF_WATERWALK);
      accum_duration = TRUE;
      to_vict = "You feel webbing between your toes.";
      to_room = "$n's feet grow webbing!";
      break;
      
    case SPELL_WAVES_OF_EXHAUSTION: //necromancy
      if (mag_resistance(ch, victim, 0))
        return;
      // no save

      SET_BIT_AR(af[0].bitvector, AFF_FATIGUED);
      GET_MOVE(victim) -= 20 + magic_level;
      af[0].duration = magic_level + 10;
      to_room = "$n is overcome by overwhelming exhaustion!!";
      to_vict = "You are overcome by overwhelming exhaustion!!";
      break;

    case SPELL_WAVES_OF_FATIGUE: //necromancy
      if (mag_resistance(ch, victim, 0))
        return;
      if (mag_savingthrow(ch, victim, SAVING_FORT, 0)) {
        return;
      }
      SET_BIT_AR(af[0].bitvector, AFF_FATIGUED);
      GET_MOVE(victim) -= 10 + magic_level;
      af[0].duration = magic_level;
      to_room = "$n is overcome by overwhelming fatigue!!";
      to_vict = "You are overcome by overwhelming fatigue!!";
      break;

    case SPELL_WEB: //conjuration
      if (MOB_FLAGGED(victim, MOB_NOGRAPPLE)) {
        send_to_char(ch, "Your opponent doesn't seem webbable.\r\n");
        return;
      }
      if (mag_savingthrow(ch, victim, SAVING_REFL, 0)) {
        send_to_char(ch, "You fail.\r\n");
        return;
      }

      af[0].duration = 7 * magic_level;
      SET_BIT_AR(af[0].bitvector, AFF_GRAPPLED);

      to_room = "$n is covered in a sticky magical web!";
      to_vict = "You are covered in a sticky magical web!";
      break;

    case SPELL_WEIRD: //illusion (also does damage)
      if (mag_resistance(ch, victim, 0))
        return;
      // no save

      af[0].location = APPLY_STR;
      af[0].duration = magic_level;
      af[0].modifier = -(dice(1, 4));
      to_room = "$n's strength is withered!";
      to_vict = "You feel your strength wither!";
      
      SET_BIT_AR(af[1].bitvector, AFF_STUN);
      af[1].duration = 1;
      to_room = "$n is stunned by a terrible WEIRD!";
      to_vict = "You are stunned by a terrible WEIRD!";
      break;

    case SPELL_WISDOM: //transmutation
      af[0].location = APPLY_WIS;
      af[0].duration = (CASTER_LEVEL(ch) * 12) + 100;
      af[0].modifier = 4;
      accum_duration = TRUE;
      to_vict = "You feel more wise!";
      to_room = "$n's wisdom increases!";
      break;
  }

  /* slippery mind */
  if (is_mind_affect && !IS_NPC(victim) &&
          GET_SKILL(victim, SKILL_SLIPPERY_MIND)) {
    increase_skill(victim, SKILL_SLIPPERY_MIND);
    send_to_char(victim, "\tW*Slippery Mind*\tn  ");
    if (mag_savingthrow(ch, victim, SAVING_WILL, 0)) {
      return;
    }
  }

  /* If this is a mob that has this affect set in its mob file, do not perform
   * the affect.  This prevents people from un-sancting mobs by sancting them
   * and waiting for it to fade, for example. */
  if (IS_NPC(victim) && !affected_by_spell(victim, spellnum)) {
    for (i = 0; i < MAX_SPELL_AFFECTS; i++) {
      for (j = 0; j < NUM_AFF_FLAGS; j++) {
        if (IS_SET_AR(af[i].bitvector, j) && AFF_FLAGGED(victim, j)) {
          send_to_char(ch, "%s", CONFIG_NOEFFECT);
          return;
        }
      }
    }
  }

  /* If the victim is already affected by this spell, and the spell does not
   * have an accumulative effect, then fail the spell. */
  if (affected_by_spell(victim, spellnum) && !(accum_duration || accum_affect)) {
    send_to_char(ch, "%s", CONFIG_NOEFFECT);
    return;
  }

  if (to_vict != NULL)
    act(to_vict, FALSE, victim, 0, ch, TO_CHAR);
  if (to_room != NULL)
    act(to_room, TRUE, victim, 0, ch, TO_ROOM);

  for (i = 0; i < MAX_SPELL_AFFECTS; i++)
    if (af[i].bitvector[0] || af[i].bitvector[1] ||
            af[i].bitvector[2] || af[i].bitvector[3] ||
            (af[i].location != APPLY_NONE))
      affect_join(victim, af + i, accum_duration, FALSE, accum_affect, FALSE);
}

/* This function is used to provide services to mag_groups.  This function is
 * the one you should change to add new group spells. */
static void perform_mag_groups(int level, struct char_data *ch,
        struct char_data *tch, struct obj_data *obj, int spellnum,
        int savetype) {

  switch (spellnum) {
    case SPELL_GROUP_HEAL:
      mag_points(level, ch, tch, obj, SPELL_HEAL, savetype);
      break;
    case SPELL_GROUP_ARMOR:
      mag_affects(level, ch, tch, obj, SPELL_ARMOR, savetype);
      break;
    case SPELL_MASS_HASTE:
      mag_affects(level, ch, tch, obj, SPELL_HASTE, savetype);
      break;
    case SPELL_MASS_CURE_CRIT:
      mag_affects(level, ch, tch, obj, SPELL_CURE_CRITIC, savetype);
      break;
    case SPELL_MASS_CURE_SERIOUS:
      mag_affects(level, ch, tch, obj, SPELL_CURE_SERIOUS, savetype);
      break;
    case SPELL_MASS_CURE_MODERATE:
      mag_affects(level, ch, tch, obj, SPELL_CURE_MODERATE, savetype);
      break;
    case SPELL_MASS_CURE_LIGHT:
      mag_affects(level, ch, tch, obj, SPELL_CURE_LIGHT, savetype);
      break;
    case SPELL_CIRCLE_A_EVIL:
      mag_affects(level, ch, tch, obj, SPELL_PROT_FROM_EVIL, savetype);
      break;
    case SPELL_CIRCLE_A_GOOD:
      mag_affects(level, ch, tch, obj, SPELL_PROT_FROM_GOOD, savetype);
      break;
    case SPELL_INVISIBILITY_SPHERE:
      mag_affects(level, ch, tch, obj, SPELL_INVISIBLE, savetype);
      break;
    case SPELL_GROUP_RECALL:
      spell_recall(level, ch, tch, NULL);
      break;
    case SPELL_MASS_FLY:
      mag_affects(level, ch, tch, obj, SPELL_FLY, savetype);
      break;
    case SPELL_MASS_CUNNING:
      mag_affects(level, ch, tch, obj, SPELL_MASS_CUNNING, savetype);
      break;
    case SPELL_MASS_CHARISMA:
      mag_affects(level, ch, tch, obj, SPELL_MASS_CHARISMA, savetype);
      break;
    case SPELL_MASS_WISDOM:
      mag_affects(level, ch, tch, obj, SPELL_MASS_WISDOM, savetype);
      break;
    case SPELL_MASS_ENHANCE:
      mag_affects(level, ch, tch, obj, SPELL_MASS_ENHANCE, savetype);
      break;
    case SPELL_AID:
      mag_affects(level, ch, tch, obj, SPELL_AID, savetype);
      break;
    case SPELL_PRAYER:
      mag_affects(level, ch, tch, obj, SPELL_PRAYER, savetype);
      break;
    case SPELL_MASS_ENDURANCE:
      mag_affects(level, ch, tch, obj, SPELL_MASS_ENDURANCE, savetype);
      break;
    case SPELL_MASS_GRACE:
      mag_affects(level, ch, tch, obj, SPELL_MASS_GRACE, savetype);
      break;
    case SPELL_MASS_STRENGTH:
      mag_affects(level, ch, tch, obj, SPELL_MASS_STRENGTH, savetype);
      break;
    case SPELL_ANIMAL_SHAPES:
      /* found in act.other.c */
      perform_wildshape(tch, rand_number(1, (NUM_SHAPE_TYPES - 1)), spellnum);
      break;
  }
}

/* Every spell that affects the group should run through here perform_mag_groups
 * contains the switch statement to send us to the right magic. Group spells
 * affect everyone grouped with the caster who is in the room, caster last. To
 * add new group spells, you shouldn't have to change anything in mag_groups.
 * Just add a new case to perform_mag_groups.
 * UPDATE:  added some to_char and to_room messages here for fun  */
void mag_groups(int level, struct char_data *ch, struct obj_data *obj,
        int spellnum, int savetype) {
  char *to_char = NULL, *to_room = NULL;
  struct char_data *tch;

  if (ch == NULL)
    return;

  if (!GROUP(ch))
    return;

  switch (spellnum) {
    case SPELL_GROUP_HEAL:
      to_char = "You summon massive beams of healing light!\tn";
      to_room = "$n summons massive beams of healing light!\tn";
      break;
    case SPELL_GROUP_ARMOR:
      to_char = "You manifest magical armoring for your companions!\tn";
      to_room = "$n manifests magical armoring for $s companions!\tn";
      break;
    case SPELL_MASS_HASTE:
      to_char = "You spin quickly with an agile magical flourish!\tn";
      to_room = "$n spins quickly with an agile magical flourish!\tn";
      break;
    case SPELL_CIRCLE_A_EVIL:
      to_char = "You draw a 6-point magical star in the air!\tn";
      to_room = "$n draw a 6-point magical star in the air!\tn";
      break;
    case SPELL_CIRCLE_A_GOOD:
      to_char = "You draw a 5-point magical star in the air!\tn";
      to_room = "$n draw a 5-point magical star in the air!\tn";
      break;
    case SPELL_INVISIBILITY_SPHERE:
      to_char = "Your magicks brings forth an invisibility sphere!\tn";
      to_room = "$n brings forth an invisibility sphere!\tn";
      break;
    case SPELL_GROUP_RECALL:
      to_char = "You create a huge ball of light that consumes the area!\tn";
      to_room = "$n creates a huge ball of light that consumes the area!\tn";
      break;
    case SPELL_MASS_FLY:
      to_char = "Your magicks brings strong magical winds to aid in flight!\tn";
      to_room = "$n brings strong magical winds to aid in flight!\tn";
      break;
    case SPELL_ANIMAL_SHAPES:
      to_char = "You transform your group!\tn";
      to_room = "$n transforms $s group!\tn";
      break;
  }

  if (to_char != NULL)
    act(to_char, FALSE, ch, 0, 0, TO_CHAR);
  if (to_room != NULL)
    act(to_room, FALSE, ch, 0, 0, TO_ROOM);

  while ((tch = (struct char_data *) simple_list(GROUP(ch)->members)) !=
          NULL) {
    if (IN_ROOM(tch) != IN_ROOM(ch))
      continue;
    perform_mag_groups(level, ch, tch, obj, spellnum, savetype);
  }
}

/* Mass spells affect every creature in the room except the caster. No spells
 * of this class currently implemented. */
void mag_masses(int level, struct char_data *ch, struct obj_data *obj,
        int spellnum, int savetype) {
  struct char_data *tch, *tch_next;
  int isEffect = FALSE;

  for (tch = world[IN_ROOM(ch)].people; tch; tch = tch_next) {
    tch_next = tch->next_in_room;
    if (tch == ch)
      continue;

    switch (spellnum) {
      case SPELL_STENCH:
        isEffect = TRUE;
        break;
      case SPELL_ACID:
        break;
      case SPELL_BLADES:
        break;
    }

    if (isEffect)
      mag_affects(level, ch, tch, obj, spellnum, savetype);
    else
      mag_damage(level, ch, tch, obj, spellnum, 1);
  }
}

// CHARM PERSON CHARM_PERSON - enchantment (found in spells.c)
// ENCHANT WEAPON ENCHANT_WEAPON - enchantment (spells.c)


//  pass values spellnum
// spellnum = spellnum
// -1 = no spellnum, no special handling
// -2 = tailsweep
//  return values
// 0 = not allowed to hit target
// 1 = allowed to hit target

int aoeOK(struct char_data *ch, struct char_data *tch, int spellnum) {
  // skip self - tested
  if (tch == ch)
    return 0;

  // immorts that are nohas
  if (!IS_NPC(tch) && GET_LEVEL(tch) >= LVL_IMMORT &&
          PRF_FLAGGED(tch, PRF_NOHASSLE))
    return 0;

  // earthquake currently doesn't work on flying victims
  if ((spellnum == SPELL_EARTHQUAKE) && AFF_FLAGGED(tch, AFF_FLYING))
    return 0;

  // tailsweep currently doesn't work on flying victims
  if ((spellnum == -2) && AFF_FLAGGED(tch, AFF_FLYING))
    return 0;

  // same group, skip
  if (GROUP(tch) && GROUP(ch) && GROUP(ch) == GROUP(tch))
    return 0;

  // don't hit the charmee of a group member
  if (tch->master)
    if (AFF_FLAGGED(tch, AFF_CHARM) &&
            GROUP(tch->master) && GROUP(ch) && GROUP(ch) == GROUP(tch->master))
      return 0;

  // charmee, don't hit a group member of master
  if (ch->master)
    if (AFF_FLAGGED(ch, AFF_CHARM) &&
            GROUP(ch->master) && GROUP(tch) && GROUP(tch) == GROUP(ch->master))
      return 0;

  // charmee, don't hit a charmee of group member of master
  if (ch->master && tch->master)
    if (AFF_FLAGGED(ch, AFF_CHARM) && AFF_FLAGGED(tch, AFF_CHARM) &&
            GROUP(ch->master) && GROUP(tch->master) &&
            GROUP(ch->master) == GROUP(tch->master))
      return 0;

  // don't hit your master
  if (ch->master)
    if (AFF_FLAGGED(ch, AFF_CHARM) && ch->master == tch)
      return 0;

  // don't hit your master's ungroupped charmies

  // don't hit your charmee
  if (tch->master)
    if (AFF_FLAGGED(tch, AFF_CHARM) && tch->master == ch)
      return 0;

  // npc that isn't charmed shouldn't hurt other npc's
  if (IS_NPC(ch) && !AFF_FLAGGED(ch, AFF_CHARM) && IS_NPC(tch))
    return 0;

  // PK MUD settings
  if (CONFIG_PK_ALLOWED) {
    // PK settings
  } else {

    // if pc cast, !pk mud, skip pc
    if (!IS_NPC(ch) && !IS_NPC(tch))
      return 0;

    // do not hit pc charmee
    if (!IS_NPC(ch) && AFF_FLAGGED(tch, AFF_CHARM) && !IS_NPC(tch->master))
      return 0;

    // charmee shouldn't hit pc's
    if (IS_NPC(ch) && AFF_FLAGGED(ch, AFF_CHARM) && !IS_NPC(ch->master) &&
            !IS_NPC(tch))
      return 0;

  }

  return 1;
}

/* Every spell that affects an area (room) runs through here.  These are
 * generally offensive spells.  This calls mag_damage to do the actual damage.
 * All spells listed here must also have a case in mag_damage() in order for
 * them to work. Area spells have limited targets within the room. */
void mag_areas(int level, struct char_data *ch, struct obj_data *obj,
        int spellnum, int savetype) {
  struct char_data *tch, *next_tch;
  const char *to_char = NULL, *to_room = NULL;
  int isEffect = FALSE, is_eff_and_dam = FALSE, is_uneffect = FALSE;

  if (ch == NULL)
    return;

  /* to add spells just add the message here plus an entry in mag_damage for
   * the damaging part of the spell.   */
  switch (spellnum) {
    case SPELL_CALL_LIGHTNING_STORM:
      to_char = "You call down a furious lightning storm upon the area!";
      to_room = "$n raises $s arms and calls down a furious lightning storm!";
      break;
    case SPELL_CHAIN_LIGHTNING:
      to_char = "Arcing bolts of lightning flare from your fingertips!";
      to_room = "Arcing bolts of lightning fly from the fingers of $n!";
      break;
    case SPELL_DEATHCLOUD: //cloudkill
      break;
    case SPELL_DOOM: // creeping doom
      break;
    case SPELL_EARTHQUAKE:
      to_char = "You gesture and the earth begins to shake all around you!";
      to_room = "$n gracefully gestures and the earth begins to shake violently!";
      break;
    case SPELL_ENFEEBLEMENT:
      isEffect = TRUE;
      to_char = "You invoke a powerful enfeeblement enchantment!\tn";
      to_room = "$n invokes a powerful enfeeblement enchantment!\tn";
      break;
    case SPELL_FAERIE_FOG:
      is_uneffect = TRUE;
      to_char = "You summon faerie fog!\tn";
      to_room = "$n summons faerie fog!\tn";
      break;
    case SPELL_FIRE_STORM:
      to_char = "You call forth sheets of roaring flame!";
      to_room = "$n calls forth sheets of roaring flame!";
      break;
    case SPELL_FLAMING_SPHERE:
      to_char = "You summon a burning globe of fire that rolls through the area!";
      to_room = "$n summons a burning globe of fire that rolls through the area!";
      break;
    case SPELL_HALT_UNDEAD:
      isEffect = TRUE;
      to_char = "\tDYou invoke a powerful halt spell!\tn";
      to_room = "$n\tD invokes a powerful halt spell!\tn";
      break;
    case SPELL_HELLBALL:
      to_char = "\tMYou conjures a pure ball of Hellfire!\tn";
      to_room = "$n\tM conjures a pure ball of Hellfire!\tn";
      break;
    case SPELL_HORRID_WILTING:
      to_char = "Your wilting causes the moisture to leave the area!";
      to_room = "$n's horrid wilting causes all the moisture to leave the area!";
      break;
    case SPELL_ICE_STORM:
      to_char = "You conjure a storm of ice that blankets the area!";
      to_room = "$n conjures a storm of ice, blanketing the area!";
      break;
    case SPELL_INCENDIARY: //incendiary cloud
      break;
    case SPELL_INSECT_PLAGUE:
      to_char = "You summon a swarm of locusts into the area!";
      to_room = "$n summons a swarm of locusts into the area!";
      break;
    case SPELL_MASS_HOLD_PERSON:
      isEffect = TRUE;
      to_char = "You invoke a powerful hold person enchantment!\tn";
      to_room = "$n invokes a powerful hold person enchantment!\tn";
      break;
    case SPELL_METEOR_SWARM:
      to_char = "You call down meteors from the sky to pummel your foes!";
      to_room = "$n invokes a swarm of meteors to rain from the sky!";
      break;
    case SPELL_PRISMATIC_SPRAY:
      is_eff_and_dam = TRUE;
      to_char = "\tnYou fire from your hands a \tYr\tRa\tBi\tGn\tCb\tWo\tDw\tn of color!\tn";
      to_room = "$n \tnfires from $s hands a \tYr\tRa\tBi\tGn\tCb\tWo\tDw\tn of color!\tn";
      break;
    case SPELL_SUNBEAM:
      is_eff_and_dam = TRUE;
      to_char = "\tnYou bring forth a powerful sunbeam!\tn";
      to_room = "$n brings forth a powerful sunbeam!\tn";
      break;
    case SPELL_SUNBURST:
      is_eff_and_dam = TRUE;
      to_char = "\tnYou bring forth a powerful sunburst!\tn";
      to_room = "$n brings forth a powerful sunburst!\tn";
      break;
    case SPELL_THUNDERCLAP:
      is_eff_and_dam = TRUE;
      to_char = "\tcA loud \twCRACK\tc fills the air with deafening force!\tn";
      to_room = "\tcA loud \twCRACK\tc fills the air with deafening force!\tn";
      break;
    case SPELL_WAIL_OF_THE_BANSHEE:
      is_eff_and_dam = TRUE;
      to_char = "You emit a terrible banshee wail!\tn";
      to_room = "$n emits a terrible banshee wail!\tn";
      break;
    case SPELL_WAVES_OF_EXHAUSTION:
      isEffect = TRUE;
      to_char = "\tDYou muster the power of death creating waves of exhaustion!\tn";
      to_room = "$n\tD musters the power of death creating waves of exhaustion!\tn";
      break;
    case SPELL_WAVES_OF_FATIGUE:
      isEffect = TRUE;
      to_char = "\tDYou muster the power of death creating waves of fatigue!\tn";
      to_room = "$n\tD musters the power of death creating waves of fatigue!\tn";
      break;
    case SPELL_WHIRLWIND:
      to_char = "You call down a rip-roaring cyclone on the area!";
      to_room = "$n calls down a rip-roaring cyclone on the area!";
      break;
  }

  if (to_char != NULL)
    act(to_char, FALSE, ch, 0, 0, TO_CHAR);
  if (to_room != NULL)
    act(to_room, FALSE, ch, 0, 0, TO_ROOM);

  for (tch = world[IN_ROOM(ch)].people; tch; tch = next_tch) {
    next_tch = tch->next_in_room;

    if (aoeOK(ch, tch, spellnum)) {
      if (is_eff_and_dam) {
        mag_damage(level, ch, tch, obj, spellnum, 1);
        mag_affects(level, ch, tch, obj, spellnum, savetype);
      } else if (isEffect)
        mag_affects(level, ch, tch, obj, spellnum, savetype);
      else if (is_uneffect)
        mag_unaffects(level, ch, tch, obj, spellnum, savetype);
      else
        mag_damage(level, ch, tch, obj, spellnum, 1);

      /* we gotta start combat here */
      if (isEffect && spell_info[spellnum].violent && tch && GET_POS(tch) == POS_STANDING &&
              !FIGHTING(tch) && spellnum != SPELL_CHARM && spellnum != SPELL_CHARM_ANIMAL &&
              spellnum != SPELL_DOMINATE_PERSON && spellnum != SPELL_MASS_DOMINATION) {
        if (tch != ch) { // funny results from potions/scrolls
          if (IN_ROOM(tch) == IN_ROOM(ch)) {
            hit(tch, ch, TYPE_UNDEFINED, DAM_RESERVED_DBC, 0, FALSE);
          }
        }
      } /* end start combat */

    } /* end aoeOK */
  } /* for loop for cycling through room chars */

}

/*----------------------------------------------------------------------------*/
/* Begin Magic Summoning - Generic Routines and Local Globals */
/*----------------------------------------------------------------------------*/

/* Every spell which summons/gates/conjours a mob comes through here. */
/* These use act(), don't put the \r\n. */
static const char *mag_summon_msgs[] = {
  "\r\n", //0
  "$n makes a strange magical gesture; you feel a strong breeze!", //1
  "$n animates a corpse!", //2
  "$N appears from a cloud of thick blue smoke!", //3
  "$N appears from a cloud of thick green smoke!", //4
  "$N appears from a cloud of thick red smoke!", //5
  "$N disappears in a thick black cloud!", //6
  "\tCAs \tn$n\tC makes a strange magical gesture, you feel a strong breeze.\tn", //7
  "\tRAs \tn$n\tR makes a strange magical gesture, you feel a searing heat.\tn", //8
  "\tYAs \tn$n\tY makes a strange magical gesture, you feel a sudden shift in the earth.\tn", //9
  "\tBAs \tn$n\tB makes a strange magical gesture, you feel the dust swirl.\tn", //10
  "$n magically divides!", //11 clone
  "$n animates a corpse!", //12 animate dead
  "$N breaks through the ground and bows before $n.", //13 mummy lord
  "With a roar $N soars to the ground next to $n.", //14 young red dragon
  "$N pops into existence next to $n.", //15 shelgarn's dragger
  "$N skimpers into the area, then quickly moves next to $n.", //16 dire badger
  "$N charges into the area, looks left, then right... "
  "then quickly moves next to $n.", //17 dire boar
  "$N moves into the area, sniffing cautiously.", //18 dire wolf
  "$N neighs and walks up to $n.", //19 phantom steed
  "$N skitters into the area and moves next to $n.", //20 dire spider
  "$N lumbers into the area and moves next to $n.", //21 dire bear
  "$N manifests with an ancient howl, then moves towards $n.", //22 hound
  "$N stalks into the area, roars loudly, then moves towards $n.", //23 d tiger
  "$N pops into existence next to $n.", //24 black blade of disaster
  "$N skulks into the area, seemingly from nowhere!", // 25 shambler
};
static const char *mag_summon_to_msgs[] = {
  "\r\n", //0
  "You make the magical gesture; you feel a strong breeze!", //1
  "You animate a corpse!", //2
  "You conjure $N from a cloud of thick blue smoke!", //3
  "You conjure $N from a cloud of thick green smoke!", //4
  "You conjure $N from a cloud of thick red smoke!", //5
  "You make $N appear in a thick black cloud!", //6
  "\tCYou make a magical gesture, you feel a strong breeze.\tn", //7
  "\tRYou make a magical gesture, you feel a searing heat.\tn", //8
  "\tYYou make a magical gesture, you feel a sudden shift in the earth.\tn", //9
  "\tBYou make a magical gesture, you feel the dust swirl.\tn", //10
  "You magically divide!", //11 clone
  "You animate a corpse!", //12 animate dead
  "$N breaks through the ground and bows before you.", //13 mummy lord
  "With a roar $N soars to the ground next to you.", //14 young red dragon
  "$N pops into existence next to you.", //15 shelgarn's dragger
  "$N skimpers into the area, then quickly moves next to you.", //16 dire badger
  "$N charges into the area, looks left, then right... "
  "then quickly moves next to you.", //17 dire boar
  "$N moves into the area, sniffing cautiously.", //18 dire wolf
  "$N neighs and walks up to you.", //19 phantom steed
  "$N skitters into the area and moves next to you.", //20 dire spider
  "$N lumbers into the area and moves next to you.", //21 dire bear
  "$N manifests with an ancient howl, then moves towards you.", //22 hound
  "$N stalks into the area, roars loudly, then moves towards you.", //23 d tiger
  "$N pops into existence next to you.", //24 black blade of disaster
  "$N skulks into the area, seemingly from nowhere!", // 25 shambler
};


/* Keep the \r\n because these use send_to_char. */
static const char *mag_summon_fail_msgs[] = {
  "\r\n",
  "There are no such creatures.\r\n",
  "Uh oh...\r\n",
  "Oh dear.\r\n",
  "Gosh durnit!\r\n",
  "The elements resist!\r\n",
  "You failed.\r\n",
  "There is no corpse!\r\n"
};

/* Defines for Mag_Summons */
// objects
#define OBJ_CLONE             161  /**< vnum for clone material. */
// mobiles
#define MOB_CLONE             10   /**< vnum for the clone mob. */
#define MOB_ZOMBIE            11   /* animate dead levels 1-7 */
#define MOB_GHOUL             35   // " " level 11+
#define MOB_GIANT_SKELETON    36   // " " level 21+
#define MOB_MUMMY             37   // " " level 30
#define MOB_MUMMY_LORD		38   // epic spell mummy dust
#define MOB_RED_DRAGON		39   // epic spell dragon knight
#define MOB_SHELGARNS_BLADE	40
#define MOB_DIRE_BADGER		41   // summon creature i
#define MOB_DIRE_BOAR		42   // " " ii
#define MOB_DIRE_WOLF		43   // " " iii
#define MOB_PHANTOM_STEED	44
//45    wizard eye
#define MOB_DIRE_SPIDER		46   // summon creature iv
//47    wall of force
#define MOB_DIRE_BEAR		48   // summon creature v
#define MOB_HOUND             49
#define MOB_DIRE_TIGER		50   // summon creature vi
#define MOB_FIRE_ELEMENTAL    51
#define MOB_EARTH_ELEMENTAL   52
#define MOB_AIR_ELEMENTAL     53
#define MOB_WATER_ELEMENTAL   54  // these elementals are for rest of s.c.
#define MOB_GHOST   55  // great animation
#define MOB_SPECTRE   56  // great animation
#define MOB_BANSHEE   57  // great animation
#define MOB_WIGHT   58  // great animation
#define MOB_BLADE_OF_DISASTER   59  // black blade of disaster
#define MOB_DIRE_RAT    9400 // summon natures ally i

void mag_summons(int level, struct char_data *ch, struct obj_data *obj,
        int spellnum, int savetype) {
  struct char_data *mob = NULL;
  struct obj_data *tobj, *next_obj;
  int pfail = 0, msg = 0, fmsg = 0, num = 1, handle_corpse = FALSE, i;
  int hp_bonus = 0, dam_bonus = 0, hit_bonus = 0;
  int mob_level = 0;
  mob_vnum mob_num = 0;
  struct follow_type *k = NULL, *next = NULL;

  if (ch == NULL)
    return;

  mob_level = CASTER_LEVEL(ch) - 11;

  switch (spellnum) {

    case SPELL_CLONE:
      msg = 11;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_CLONE;
      /*
       * We have designated the clone spell as the example for how to use the
       * mag_materials function.
       * In stock LuminariMUD it checks to see if the character has item with
       * vnum 161 which is a set of sacrificial entrails. If we have the entrails
       * the spell will succeed,  and if not, the spell will fail 102% of the time
       * (prevents random success... see below).
       * The object is extracted and the generic cast messages are displayed.
       */
      if (!mag_materials(ch, OBJ_CLONE, NOTHING, NOTHING, TRUE, TRUE))
        pfail = 102; /* No materials, spell fails. */
      else
        pfail = 0; /* We have the entrails, spell is successfully cast. */
      break;

    case SPELL_ANIMATE_DEAD: //necromancy
      if (obj == NULL || !IS_CORPSE(obj)) {
        act(mag_summon_fail_msgs[7], FALSE, ch, 0, 0, TO_CHAR);
        return;
      }
      if (IS_HOLY(IN_ROOM(ch))) {
        send_to_char(ch, "This place is too holy for such blasphemy!");
        return;
      }
      handle_corpse = TRUE;
      msg = 12;
      fmsg = rand_number(2, 6); /* Random fail message. */
      if (CASTER_LEVEL(ch) >= 30)
        mob_num = MOB_MUMMY;
      else if (CASTER_LEVEL(ch) >= 20)
        mob_num = MOB_GIANT_SKELETON;
      else if (CASTER_LEVEL(ch) >= 10)
        mob_num = MOB_GHOUL;
      else
        mob_num = MOB_ZOMBIE;
      pfail = 10; /* 10% failure, should vary in the future. */
      break;

    case SPELL_GREATER_ANIMATION: //necromancy
      if (obj == NULL || !IS_CORPSE(obj)) {
        act(mag_summon_fail_msgs[7], FALSE, ch, 0, 0, TO_CHAR);
        return;
      }
      handle_corpse = TRUE;
      msg = 12;
      fmsg = rand_number(2, 6); /* Random fail message. */
      if (CASTER_LEVEL(ch) >= 30)
        mob_num = MOB_WIGHT;
      else if (CASTER_LEVEL(ch) >= 25)
        mob_num = MOB_BANSHEE;
      else if (CASTER_LEVEL(ch) >= 20)
        mob_num = MOB_SPECTRE;
      else
        mob_num = MOB_GHOST;
      pfail = 10; /* 10% failure, should vary in the future. */

      hp_bonus += mob_level * 5;
      dam_bonus += mob_level;
      hit_bonus += mob_level;

      break;

    case SPELL_MUMMY_DUST: //epic
      handle_corpse = FALSE;
      msg = 13;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_MUMMY_LORD;
      pfail = 0;
      break;

    case SPELL_DRAGON_KNIGHT: //epic
      handle_corpse = FALSE;
      msg = 14;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_RED_DRAGON;
      pfail = 0;
      break;

    case SPELL_ELEMENTAL_SWARM: // conjuration
      handle_corpse = FALSE;
      fmsg = rand_number(2, 6);
      mob_num = 9412 + rand_number(0, 3); // 9412-9415
      switch (mob_num)
      {
        case 9412: msg = 7; break;
        case 9413: msg = 9; break;
        case 9414: msg = 8; break;
        case 9415: msg = 10; break;
      }
      num = dice(2, 4);
      break;
      
    case SPELL_FAITHFUL_HOUND: //divination
      handle_corpse = FALSE;
      msg = 22;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_HOUND;
      pfail = 0;
      break;

    case SPELL_PHANTOM_STEED: //conjuration
      handle_corpse = FALSE;
      msg = 19;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_PHANTOM_STEED;
      pfail = 0;
      break;

    case SPELL_SHAMBLER: // conjuration
      handle_corpse = FALSE;
      msg = 25;
      fmsg = rand_number(2, 6);
      mob_num = 9499;
      num = dice(1, 4) + 2;
      break;
      
    case SPELL_SHELGARNS_BLADE: //divination
      handle_corpse = FALSE;
      msg = 15;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_SHELGARNS_BLADE;
      pfail = 0;
      break;

    case SPELL_SUMMON_CREATURE_1: //conjuration
      handle_corpse = FALSE;
      msg = 16;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_DIRE_BADGER;
      pfail = 0;
      break;

    case SPELL_SUMMON_CREATURE_2: //conjuration
      handle_corpse = FALSE;
      msg = 17;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_DIRE_BOAR;
      pfail = 0;
      break;

    case SPELL_SUMMON_CREATURE_3: //conjuration
      handle_corpse = FALSE;
      msg = 18;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_DIRE_WOLF;
      pfail = 0;
      break;

    case SPELL_SUMMON_CREATURE_4: //conjuration
      handle_corpse = FALSE;
      msg = 20;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_DIRE_SPIDER;
      pfail = 0;
      break;

    case SPELL_SUMMON_CREATURE_5: //conjuration
      handle_corpse = FALSE;
      msg = 21;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_DIRE_BEAR;
      pfail = 0;
      break;

    case SPELL_SUMMON_CREATURE_6: //conjuration
      handle_corpse = FALSE;
      msg = 23;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_DIRE_TIGER;
      pfail = 0;
      break;

    case SPELL_BLADE_OF_DISASTER: //evocation
      handle_corpse = FALSE;
      msg = 24;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = MOB_BLADE_OF_DISASTER;
      pfail = 0;
      
      hp_bonus += mob_level * 5;
      dam_bonus += mob_level;
      hit_bonus += mob_level;
      
      break;

    case SPELL_SUMMON_CREATURE_9: //conjuration
      hp_bonus += mob_level * 5;
      dam_bonus += 4;
      hit_bonus += 5;
    case SPELL_SUMMON_CREATURE_8: //conjuration
      hp_bonus += mob_level * 5;
      dam_bonus += 3;
      hit_bonus += 4;
    case SPELL_SUMMON_CREATURE_7: //conjuration
      handle_corpse = FALSE;
      fmsg = rand_number(2, 6); /* Random fail message. */
      switch (dice(1, 4)) {
        case 1:
          mob_num = MOB_FIRE_ELEMENTAL;
          msg = 8;
          break;
        case 2:
          mob_num = MOB_EARTH_ELEMENTAL;
          msg = 9;
          break;
        case 3:
          mob_num = MOB_AIR_ELEMENTAL;
          msg = 7;
          break;
        case 4:
          mob_num = MOB_WATER_ELEMENTAL;
          msg = 10;
          break;
      }
      hp_bonus += mob_level * 5;
      dam_bonus += mob_level;
      hit_bonus += mob_level;
      pfail = 0;
      break;

    case SPELL_SUMMON_NATURES_ALLY_1: //conjuration
      handle_corpse = FALSE;
      msg = 20;
      fmsg = rand_number(2, 6); /* Random fail message. */
      mob_num = 9400 + rand_number(0, 7); // 9400-9407
      pfail = 0;
      break;
    
    case SPELL_SUMMON_NATURES_ALLY_2: // conjuration
      handle_corpse = FALSE;
      msg = 20;
      fmsg = rand_number(2, 6);
      mob_num = 9408 + rand_number(0, 6); // 9408-9414 for now
      pfail = 0;
      break;
      
    default:
      return;
  }

  /* start off with some possible fail conditions */
  if (AFF_FLAGGED(ch, AFF_CHARM)) {
    send_to_char(ch, "You are too giddy to have any followers!\r\n");
    return;
  }
  if (rand_number(0, 101) < pfail) {
    send_to_char(ch, "%s", mag_summon_fail_msgs[fmsg]);
    return;
  }

  /* new limit cap on certain mobiles */
  switch (spellnum) {
    case SPELL_SUMMON_CREATURE_9: //conjuration
    case SPELL_SUMMON_CREATURE_8: //conjuration
    case SPELL_SUMMON_CREATURE_7: //conjuration    
      for (k = ch->followers; k; k = next) {
        next = k->next;
        if (IS_NPC(k->follower) && AFF_FLAGGED(k->follower, AFF_CHARM) &&
                (MOB_FLAGGED(k->follower, MOB_ELEMENTAL))) {
          send_to_char(ch, "You can't control more elementals!\r\n");
          return;
        }
      }
      break;
  }
  switch (spellnum) {
    case SPELL_GREATER_ANIMATION:
      for (k = ch->followers; k; k = next) {
        next = k->next;
        if (IS_NPC(k->follower) && AFF_FLAGGED(k->follower, AFF_CHARM) &&
                (MOB_FLAGGED(k->follower, MOB_ANIMATED_DEAD))) {
          send_to_char(ch, "You can't control more power undead!\r\n");
          return;
        }
      }
      break;
  }

  /* bring the mob into existence! */
  for (i = 0; i < num; i++) {
    if (!(mob = read_mobile(mob_num, VIRTUAL))) {
      send_to_char(ch, "You don't quite remember how to make that creature.\r\n");
      return;
    }
    char_to_room(mob, IN_ROOM(ch));
    IS_CARRYING_W(mob) = 0;
    IS_CARRYING_N(mob) = 0;
    SET_BIT_AR(AFF_FLAGS(mob), AFF_CHARM);

    /* give the mobile some bonuses */
    /* ALSO special handling for clone spell */
    switch (spellnum) {
      case SPELL_SUMMON_CREATURE_9: //conjuration
      case SPELL_SUMMON_CREATURE_8: //conjuration
      case SPELL_SUMMON_CREATURE_7: //conjuration    
      case SPELL_GREATER_ANIMATION: //necromancy
        GET_LEVEL(mob) += MIN(mob_level, LVL_IMPL - GET_LEVEL(mob));
        GET_MAX_HIT(mob) += hp_bonus;
        GET_DAMROLL(mob) += dam_bonus;
        GET_HITROLL(mob) += hit_bonus;
        break;
      case SPELL_CLONE:
        /* Don't mess up the prototype; use new string copies. */
        mob->player.name = strdup(GET_NAME(ch));
        mob->player.short_descr = strdup(GET_NAME(ch));
        break;
    }

    act(mag_summon_msgs[msg], FALSE, ch, 0, mob, TO_ROOM);
    act(mag_summon_to_msgs[msg], FALSE, ch, 0, mob, TO_CHAR);
    load_mtrigger(mob);
    add_follower(mob, ch);
    if (GROUP(ch) && GROUP_LEADER(GROUP(ch)) == ch)
      join_group(mob, GROUP(ch));
  }

  /* raise dead type of spells */
  if (handle_corpse) {
    for (tobj = obj->contains; tobj; tobj = next_obj) {
      next_obj = tobj->next_content;
      obj_from_obj(tobj);
      obj_to_char(tobj, mob);
    }
    extract_obj(obj);
  }
}
#undef OBJ_CLONE
#undef MOB_CLONE
#undef MOB_ZOMBIE
#undef MOB_GHOUL		
#undef MOB_GIANT_SKELETON	
#undef MOB_MUMMY		
#undef MOB_RED_DRAGON
#undef MOB_SHELGARNS_BLADE
#undef MOB_DIRE_BADGER
#undef MOB_DIRE_BOAR
#undef MOB_DIRE_SPIDER
#undef MOB_DIRE_BEAR
#undef MOB_HOUND
#undef MOB_DIRE_TIGER
#undef MOB_FIRE_ELEMENTAL
#undef MOB_EARTH_ELEMENTAL
#undef MOB_AIR_ELEMENTAL
#undef MOB_WATER_ELEMENTAL      


/*----------------------------------------------------------------------------*/
/* End Magic Summoning - Generic Routines and Local Globals */

/*----------------------------------------------------------------------------*/


void mag_points(int level, struct char_data *ch, struct char_data *victim,
        struct obj_data *obj, int spellnum, int savetype) {
  int healing = 0, move = 0;
  const char *to_notvict = NULL, *to_char = NULL, *to_vict = NULL;

  if (victim == NULL)
    return;
  
  /* bards also get some healing spells */
  level = DIVINE_LEVEL(ch) + CLASS_LEVEL(ch, CLASS_BARD);

  switch (spellnum) {
    case SPELL_CURE_LIGHT:
      healing = dice(2, 4) + 5 + MIN(10, level);

      //to_notvict = "$n \twcures light wounds\tn on $N.";
      to_notvict = "$N \twfeels a little better\tn.";
      if (ch == victim) {
        //to_char = "You \twcure lights wounds\tn on yourself.";
        to_char = "You \twfeel a little better\tn.";
      } else {
        to_char = "You \twcure lights wounds\tn on $N.";
      }
      to_vict = "$n \twcures light wounds\tn on you.";
      break;
    case SPELL_CURE_MODERATE:
      healing = dice(3, 4) + 10 + MIN(15, level);

      to_notvict = "$n \twcures moderate wounds\tn on $N.";
      if (ch == victim)
        to_char = "You \twcure moderate wounds\tn on yourself.";
      else
        to_char = "You \twcure moderate wounds\tn on $N.";
      to_vict = "$n \twcures moderate wounds\tn on you.";
      break;
    case SPELL_CURE_SERIOUS:
      healing = dice(4, 4) + 15 + MIN(20, level);

      to_notvict = "$n \twcures serious wounds\tn on $N.";
      if (ch == victim)
        to_char = "You \twcure serious wounds\tn on yourself.";
      else
        to_char = "You \twcure serious wounds\tn on $N.";
      to_vict = "$n \twcures serious wounds\tn on you.";
      break;
    case SPELL_CURE_CRITIC:
      healing = dice(6, 4) + 20 + MIN(25, level);

      to_notvict = "$n \twcures critical wounds\tn on $N.";
      if (ch == victim)
        to_char = "You \twcure critical wounds\tn on yourself.";
      else
        to_char = "You \twcure critical wounds\tn on $N.";
      to_vict = "$n \twcures critical wounds\tn on you.";
      break;
    case SPELL_HEAL:
      healing = level * 10 + 20;

      to_notvict = "$n \tWheals\tn $N.";
      if (ch == victim)
        to_char = "You \tWheal\tn yourself.";
      else
        to_char = "You \tWheal\tn $N.";
      to_vict = "$n \tWheals\tn you.";
      break;
    case SPELL_VAMPIRIC_TOUCH:
      victim = ch;
      healing = dice(MIN(15, CASTER_LEVEL(ch)), 4);

      to_notvict = "$N's wounds are \tWhealed\tn by \tRvampiric\tD magic\tn.";
      send_to_char(victim, "A \tWwarm feeling\tn floods your body as \tRvampiric "
              "\tDmagic\tn takes over.\r\n");
      break;
    case SPELL_REGENERATION:
      healing = dice(4, 4) + 15 + MIN(20, level);

      to_notvict = "$n \twcures some wounds\tn on $N.";
      if (ch == victim)
        to_char = "You \twcure some wounds\tn on yourself.";
      else
        to_char = "You \twcure some wounds\tn on $N.";
      to_vict = "$n \twcures some wounds\tn on you.";
      break;
  }

  send_to_char(ch, "<%d> ", healing);
  if (ch != victim)
    send_to_char(victim, "<%d> ", healing);
  
  if (to_notvict != NULL)
    act(to_notvict, TRUE, ch, 0, victim, TO_NOTVICT);
  if (to_vict != NULL && ch != victim)
    act(to_vict, TRUE, ch, 0, victim, TO_VICT | TO_SLEEP);
  if (to_char != NULL)
    act(to_char, TRUE, ch, 0, victim, TO_CHAR);

  GET_HIT(victim) = MIN(GET_MAX_HIT(victim), GET_HIT(victim) + healing);
  GET_MOVE(victim) = MIN(GET_MAX_MOVE(victim), GET_MOVE(victim) + move);
  update_pos(victim);
}

void mag_unaffects(int level, struct char_data *ch, struct char_data *victim,
        struct obj_data *obj, int spellnum, int type) {
  int spell = 0, msg_not_affected = TRUE, affect = 0;
  const char *to_vict = NULL, *to_char = NULL, *to_notvict = NULL;

  if (victim == NULL)
    return;

  switch (spellnum) {
    case SPELL_HEAL:
      /* Heal also restores health, so don't give the "no effect" message if the
       * target isn't afflicted by the 'blindness' spell. */
      msg_not_affected = FALSE;
      /* fall-through */
    case SPELL_CURE_BLIND:
      /* this has fall-through from above */
      spell = SPELL_BLINDNESS;
      affect = AFF_BLIND;
      to_char = "You restore $N's vision.";
      to_vict = "$n restores your vision!";
      to_notvict = "There's a momentary gleam in $N's eyes.";
      break;
      
    case SPELL_REMOVE_POISON:
      spell = SPELL_POISON;
      affect = AFF_POISON;
      to_char = "You remove the poison from $N's body.";
      to_vict = "A warm feeling originating from $n runs through your body!";
      to_notvict = "$N looks better.";
      break;
      
    case SPELL_REMOVE_CURSE:
      spell = SPELL_CURSE;
      affect = AFF_CURSE;
      to_char = "You remove the curse from $N.";
      to_vict = "$n removes the curse upon you.";
      to_notvict = "$N briefly glows blue.";
      break;
      
    case SPELL_REMOVE_DISEASE:
      spell = SPELL_EYEBITE;
      affect = AFF_DISEASE;
      to_char = "You remove the disease from $N.";
      to_vict = "$n removes the disease inflicting you.";
      to_notvict = "$N briefly flushes red then no longer looks diseased.";
      break;
      
    case SPELL_REMOVE_FEAR:
      spell = SPELL_SCARE;
      affect = AFF_FEAR;
      to_char = "You remove the fear from $N.";
      to_vict = "$n removes the fear upon you.";
      to_notvict = "$N looks brave again.";
      break;
      
    case SPELL_BRAVERY:
      spell = SPELL_SCARE;
      affect = AFF_CURSE;
      to_char = "You remove the fear from $N.";
      to_vict = "$n removes the fear upon you.";
      to_notvict = "$N looks brave again.";
      break;
      
    case SPELL_CURE_DEAFNESS:
      spell = SPELL_DEAFNESS;
      affect = AFF_DEAF;
      to_char = "You remove the deafness from $N.";
      to_vict = "$n removes the deafness from you.";
      to_notvict = "$N looks like $E can hear again.";
      break;
      
    case SPELL_FREE_MOVEMENT:
      spell = SPELL_WEB;
      affect = AFF_GRAPPLED;
      to_char = "You remove the web from $N.";
      to_vict = "$n removes the web from you.";
      to_notvict = "$N looks like $E can move again.";
      break;
      
    case SPELL_FAERIE_FOG:
      spell = SPELL_INVISIBLE;
      affect = AFF_INVISIBLE;
      /* a message isn't appropriate for failure here */
      msg_not_affected = FALSE;
      
      to_char = "Your fog reveals $N.";
      to_vict = "$n reveals you with faerie fog.";
      to_notvict = "$N is revealed by $n's faerie fog.";
      break;
      
    default:
      log("SYSERR: unknown spellnum %d passed to mag_unaffects.", spellnum);
      return;
  }

  if (!affected_by_spell(victim, spell) && !AFF_FLAGGED(victim, affect)) {
    if (msg_not_affected)
      send_to_char(ch, "%s", CONFIG_NOEFFECT);
    return;
  }

  /* first remove spell affect */
  affect_from_char(victim, spell);
  /* then remove affect flag if it somehow is still around */
  if (AFF_FLAGGED(victim, affect))
    REMOVE_BIT_AR(AFF_FLAGS(victim), affect);    
  
  if (to_notvict != NULL)
    act(to_notvict, TRUE, ch, 0, victim, TO_NOTVICT);
  if (to_vict != NULL)
    act(to_vict, TRUE, ch, 0, victim, TO_VICT | TO_SLEEP);
  if (to_char != NULL)
    act(to_char, TRUE, ch, 0, victim, TO_CHAR);
}

void mag_alter_objs(int level, struct char_data *ch, struct obj_data *obj,
        int spellnum, int savetype) {
  const char *to_char = NULL, *to_room = NULL;

  if (obj == NULL)
    return;

  switch (spellnum) {
    case SPELL_BLESS:
      if (!OBJ_FLAGGED(obj, ITEM_BLESS) &&
              (GET_OBJ_WEIGHT(obj) <= 5 * DIVINE_LEVEL(ch))) {
        SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_BLESS);
        to_char = "$p glows briefly.";
      }
      break;
    case SPELL_CURSE:
      if (!OBJ_FLAGGED(obj, ITEM_NODROP)) {
        SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_NODROP);
        if (GET_OBJ_TYPE(obj) == ITEM_WEAPON)
          GET_OBJ_VAL(obj, 2)--;
        to_char = "$p briefly glows red.";
      }
      break;
    case SPELL_INVISIBLE:
      if (!OBJ_FLAGGED(obj, ITEM_NOINVIS) && !OBJ_FLAGGED(obj, ITEM_INVISIBLE)) {
        SET_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_INVISIBLE);
        to_char = "$p vanishes.";
      }
      break;
    case SPELL_POISON:
      if (((GET_OBJ_TYPE(obj) == ITEM_DRINKCON) ||
              (GET_OBJ_TYPE(obj) == ITEM_FOUNTAIN) ||
              (GET_OBJ_TYPE(obj) == ITEM_FOOD)) && !GET_OBJ_VAL(obj, 3)) {
        GET_OBJ_VAL(obj, 3) = 1;
        to_char = "$p steams briefly.";
      }
      break;
    case SPELL_REMOVE_CURSE:
      if (OBJ_FLAGGED(obj, ITEM_NODROP)) {
        REMOVE_BIT_AR(GET_OBJ_EXTRA(obj), ITEM_NODROP);
        if (GET_OBJ_TYPE(obj) == ITEM_WEAPON)
          GET_OBJ_VAL(obj, 2)++;
        to_char = "$p briefly glows blue.";
      }
      break;
    case SPELL_REMOVE_POISON:
      if (((GET_OBJ_TYPE(obj) == ITEM_DRINKCON) ||
              (GET_OBJ_TYPE(obj) == ITEM_FOUNTAIN) ||
              (GET_OBJ_TYPE(obj) == ITEM_FOOD)) && GET_OBJ_VAL(obj, 3)) {
        GET_OBJ_VAL(obj, 3) = 0;
        to_char = "$p steams briefly.";
      }
      break;
  }

  if (to_char == NULL)
    send_to_char(ch, "%s", CONFIG_NOEFFECT);
  else
    act(to_char, TRUE, ch, obj, 0, TO_CHAR);

  if (to_room != NULL)
    act(to_room, TRUE, ch, obj, 0, TO_ROOM);
  else if (to_char != NULL)
    act(to_char, TRUE, ch, obj, 0, TO_ROOM);
}

void mag_creations(int level, struct char_data *ch, struct char_data *vict,
        struct obj_data *obj, int spellnum) {
  struct obj_data *tobj = NULL, *portal = NULL;
  obj_vnum object_vnum = 0;
  const char *to_char = NULL, *to_room = NULL;
  bool obj_to_floor = FALSE;
  bool portal_process = FALSE;
  bool gate_process = FALSE;
  char arg[MAX_INPUT_LENGTH] = {'\0'};
  room_rnum gate_dest = NOWHERE;

  if (ch == NULL)
    return;

  switch (spellnum) {
    case SPELL_CREATE_FOOD:
      to_char = "You create $p.";
      to_room = "$n creates $p.";
      object_vnum = 10;
      break;
    case SPELL_CONTINUAL_FLAME:
      to_char = "You create $p.";
      to_room = "$n creates $p.";
      object_vnum = 222;
      break;
    case SPELL_FIRE_SEEDS:
      to_char = "You create $p.";
      to_room = "$n creates $p.";
      if (rand_number(0, 1))
        object_vnum = 9404;
      else
        object_vnum = 9405;
      break;
    case SPELL_GATE:
      to_char = "\tnYou fold \tMtime\tn and \tDspace\tn, and create $p\tn.";
      to_room = "$n \tnfolds \tMtime\tn and \tDspace\tn, and creates $p\tn.";
      obj_to_floor = TRUE;
      object_vnum = 802;
      /* a little more work with gates */
      gate_process = TRUE;

      /* where is it going? */
      one_argument(cast_arg2, arg);
      if (!valid_mortal_tele_dest(ch, IN_ROOM(ch), TRUE)) {
        send_to_char(ch, "A bright flash prevents your spell from working!");
        return;
      }

      if (is_abbrev(arg, "astral")) {

        if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ASTRAL_PLANE)) {
          send_to_char(ch, "You are already on the astral plane!\r\n");
          return;
        }

        do {
          gate_dest = rand_number(0, top_of_world);
        } while (!ZONE_FLAGGED(GET_ROOM_ZONE(gate_dest), ZONE_ASTRAL_PLANE));

      } else if (is_abbrev(arg, "ethereal")) {

        if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ETH_PLANE)) {
          send_to_char(ch, "You are already on the ethereal plane!\r\n");
          return;
        }

        do {
          gate_dest = rand_number(0, top_of_world);
        } while (!ZONE_FLAGGED(GET_ROOM_ZONE(gate_dest), ZONE_ETH_PLANE));

      } else if (is_abbrev(arg, "elemental")) {

        if (ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ELEMENTAL)) {
          send_to_char(ch, "You are already on the elemental plane!\r\n");
          return;
        }

        do {
          gate_dest = rand_number(0, top_of_world);
        } while (!ZONE_FLAGGED(GET_ROOM_ZONE(gate_dest), ZONE_ELEMENTAL));

      } else if (is_abbrev(arg, "prime")) {

        if (!ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ASTRAL_PLANE) &&
                !ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ETH_PLANE) &&
                !ZONE_FLAGGED(GET_ROOM_ZONE(IN_ROOM(ch)), ZONE_ELEMENTAL)
                ) {
          send_to_char(ch,
                  "You need to be off the prime plane to gate to it!\r\n");
          return;
        }

        do {
          gate_dest = rand_number(0, top_of_world);
        } while ((ZONE_FLAGGED(GET_ROOM_ZONE(gate_dest), ZONE_ELEMENTAL) ||
                ZONE_FLAGGED(GET_ROOM_ZONE(gate_dest), ZONE_ETH_PLANE) ||
                ZONE_FLAGGED(GET_ROOM_ZONE(gate_dest), ZONE_ASTRAL_PLANE))
                );

      } else {
        send_to_char(ch, "Not a valid target (astral, ethereal, elemental, prime)");
        return;
      }
      break;
    case SPELL_GOODBERRY:
      to_char = "You create $p.";
      to_room = "$n creates $p.";
      object_vnum = 9400;
      break;
    case SPELL_HOLY_SWORD:
      to_char = "You summon $p.";
      to_room = "$n summons $p.";
      object_vnum = 810;
      break;
    case SPELL_MAGIC_STONE:
      to_char = "You create $p.";
      to_room = "$n creates $p.";
      object_vnum = 9401;
      break;
    case SPELL_PORTAL:
      if (vict == NULL) {
        send_to_char(ch, "Spell failed!  You have no target!\r\n");
        return;
      }
      if (IS_NPC(vict) && MOB_FLAGGED(vict, MOB_NOSUMMON)) {
        send_to_char(ch, "The portal begins to open, then shuts suddenly!\r\n");
        return;
      }
      to_char = "\tnYou fold \tMtime\tn and \tDspace\tn, and create $p\tn.";
      to_room = "$n \tnfolds \tMtime\tn and \tDspace\tn, and creates $p\tn.";
      obj_to_floor = TRUE;
      object_vnum = 801;
      /* a little more work with portals */
      portal_process = TRUE;
      break;
    case SPELL_SPRING_OF_LIFE:
      to_char = "You create $p.";
      to_room = "$n creates $p.";
      obj_to_floor = TRUE;
      object_vnum = 805;
      break;
    case SPELL_WALL_OF_FIRE:
      to_char = "You create $p.";
      to_room = "$n creates $p.";
      obj_to_floor = TRUE;
      object_vnum = 9402;
      break;
    case SPELL_WALL_OF_THORNS:
      to_char = "You create $p.";
      to_room = "$n creates $p.";
      obj_to_floor = TRUE;
      object_vnum = 9403;
      break;

    default:
      send_to_char(ch, "Spell unimplemented, it would seem.\r\n");
      return;
  }

  if (!(tobj = read_object(object_vnum, VIRTUAL))) {
    send_to_char(ch, "I seem to have goofed.\r\n");
    log("SYSERR: spell_creations, spell %d, obj %d: obj not found",
            spellnum, object_vnum);
    return;
  }

  /* a little more work for portal object */
  /* the obj (801) should already bet set right, but just in case */
  if (portal_process) {
    if (!(portal = read_object(object_vnum, VIRTUAL))) {
      send_to_char(ch, "I seem to have goofed.\r\n");
      log("SYSERR: spell_creations, spell %d, obj %d: obj not found",
              spellnum, object_vnum);
      return;
    }

    /* make sure its a portal **/
    GET_OBJ_TYPE(tobj) = ITEM_PORTAL;
    GET_OBJ_TYPE(portal) = ITEM_PORTAL;
    /* set it to a tick duration */
    GET_OBJ_TIMER(tobj) = 2;
    GET_OBJ_TIMER(portal) = 2;
    /* set it to a normal portal */
    tobj->obj_flags.value[0] = PORTAL_NORMAL;
    portal->obj_flags.value[0] = PORTAL_NORMAL;
    /* set destination to vict */
    tobj->obj_flags.value[1] = GET_ROOM_VNUM(IN_ROOM(vict));
    portal->obj_flags.value[1] = GET_ROOM_VNUM(IN_ROOM(ch));
    /* make sure it decays */
    if (!OBJ_FLAGGED(tobj, ITEM_DECAY))
      TOGGLE_BIT_AR(GET_OBJ_EXTRA(tobj), ITEM_DECAY);
    if (!OBJ_FLAGGED(portal, ITEM_DECAY))
      TOGGLE_BIT_AR(GET_OBJ_EXTRA(portal), ITEM_DECAY);

    /* make sure the portal is two-sided */
    obj_to_room(portal, IN_ROOM(vict));

    /* make sure the victim room sees the message */
    act("With a \tBflash\tn, $p appears in the room.",
            FALSE, vict, portal, 0, TO_CHAR);
    act("With a \tBflash\tn, $p appears in the room.",
            FALSE, vict, portal, 0, TO_ROOM);
  }
  else if (gate_process) {
    if (!(portal = read_object(object_vnum, VIRTUAL))) {
      send_to_char(ch, "I seem to have goofed.\r\n");
      log("SYSERR: spell_creations, spell %d, obj %d: obj not found",
              spellnum, object_vnum);
      return;
    }

    if (gate_dest == NOWHERE) {
      send_to_char(ch, "The spell failed!\r\n");
      return;
    }

    if (!valid_mortal_tele_dest(ch, gate_dest, TRUE)) {
      send_to_char(ch, "The spell fails!\r\n");
      return;
    }
    

    /* make sure its a portal **/
    GET_OBJ_TYPE(tobj) = ITEM_PORTAL;
    GET_OBJ_TYPE(portal) = ITEM_PORTAL;
    /* set it to a tick duration */
    GET_OBJ_TIMER(tobj) = 2;
    GET_OBJ_TIMER(portal) = 2;
    /* set it to a normal portal */
    tobj->obj_flags.value[0] = PORTAL_NORMAL;
    portal->obj_flags.value[0] = PORTAL_NORMAL;
    /* set destination to plane */
    tobj->obj_flags.value[1] = GET_ROOM_VNUM(gate_dest);
    portal->obj_flags.value[1] = GET_ROOM_VNUM(IN_ROOM(ch));
    /* make sure it decays */
    if (!OBJ_FLAGGED(tobj, ITEM_DECAY))
      TOGGLE_BIT_AR(GET_OBJ_EXTRA(tobj), ITEM_DECAY);
    if (!OBJ_FLAGGED(portal, ITEM_DECAY))
      TOGGLE_BIT_AR(GET_OBJ_EXTRA(portal), ITEM_DECAY);

    /* make sure the portal is two-sided */
    obj_to_room(portal, gate_dest);

    /* make sure the victim room sees the message */
    act("With a \tBflash\tn, $p appears in the room.",
            FALSE, vict, portal, 0, TO_CHAR);
    act("With a \tBflash\tn, $p appears in the room.",
            FALSE, vict, portal, 0, TO_ROOM);
  }
  else {
    /* a little convenient idea, item should match char size */
    GET_OBJ_SIZE(tobj) = GET_SIZE(ch);

  }

  if (obj_to_floor)
    obj_to_room(tobj, IN_ROOM(ch));
  else
    obj_to_char(tobj, ch);
  act(to_char, FALSE, ch, tobj, 0, TO_CHAR);
  act(to_room, FALSE, ch, tobj, 0, TO_ROOM);
  load_otrigger(tobj);
}

/* so this function is becoming a beast, we have to support both
   room-affections AND room-events now
 */
void mag_room(int level, struct char_data *ch, struct obj_data *obj,
        int spellnum) {
  long aff = -1; /* what affection, -1 means it must be an event */
  int rounds = 0; /* how many rounds this spell lasts (duration) */
  char *to_char = NULL;
  char *to_room = NULL, buf[MAX_INPUT_LENGTH] = {'\0'};
  struct raff_node *raff = NULL;
  extern struct raff_node *raff_list;
  room_rnum rnum = NOWHERE;
  bool failure = FALSE;
  event_id IdNum = -1; /* which event? -1 means it must be an affection */

  if (ch == NULL)
    return;
  
  rnum = IN_ROOM(ch);  

  if (ROOM_FLAGGED(rnum, ROOM_NOMAGIC))
    failure = TRUE;
  
  level = MAX(MIN(level, LVL_IMPL), 1);

  switch (spellnum) {
    /*******  ROOM EVENTS     ************/
    case SPELL_I_DARKNESS:
      IdNum = eDARKNESS;
      if (ROOM_FLAGGED(rnum, ROOM_DARK))
        failure = TRUE;
        
      rounds = 10;
      SET_BIT_AR(ROOM_FLAGS(rnum), ROOM_DARK);
        
      to_char = "You cast a shroud of darkness upon the area.";
      to_room = "$n casts a shroud of darkness upon this area.";
    break;
    /*******  END ROOM EVENTS ************/
    
    /*******  ROOM AFFECTIONS ************/
    case SPELL_ACID_FOG: //conjuration
      to_char = "You create a thick bank of acid fog!";
      to_room = "$n creates a thick bank of acid fog!";
      aff = RAFF_ACID_FOG;
      rounds = MAGIC_LEVEL(ch);
      break;

    case SPELL_ANTI_MAGIC_FIELD: //illusion
      to_char = "You create an anti-magic field!";
      to_room = "$n creates an anti-magic field!";
      aff = RAFF_ANTI_MAGIC;
      rounds = 15;
      break;

    case SPELL_BILLOWING_CLOUD: //conjuration
      to_char = "Clouds of billowing thickness fill the area.";
      to_room = "$n creates clouds of billowing thickness that fill the area.";
      aff = RAFF_BILLOWING;
      rounds = 15;
      break;

    case SPELL_BLADE_BARRIER: //divine spell
      to_char = "You create a barrier of spinning blades!";
      to_room = "$n creates a barrier of spinning blades!";
      aff = RAFF_BLADE_BARRIER;
      rounds = DIVINE_LEVEL(ch);
      break;

    case SPELL_DARKNESS: //divination
      to_char = "You create a blanket of pitch black.";
      to_room = "$n creates a blanket of pitch black.";
      aff = RAFF_DARKNESS;
      rounds = 15;
      break;

    case SPELL_DAYLIGHT: //illusion
    case SPELL_SUNBEAM: // evocation[light]
    case SPELL_SUNBURST: //divination
      to_char = "You create a blanket of artificial daylight.";
      to_room = "$n creates a blanket of artificial daylight.";
      aff = RAFF_LIGHT;
      rounds = 15;
      break;

    case SPELL_HALLOW: // evocation
      to_char = "A holy aura fills the area.";
      to_room = "A holy aura fills the area as $n finishes $s spell.";
      aff = RAFF_HOLY;
      rounds = 1000;
      break;
     
    case SPELL_SPIKE_GROWTH: // transmutation
      if (SECT(ch->in_room) == SECT_UNDERWATER ||
              SECT(ch->in_room)== SECT_FLYING ||
              SECT(ch->in_room) == SECT_WATER_SWIM ||
              SECT(ch->in_room) == SECT_WATER_NOSWIM ||
              SECT(ch->in_room) == SECT_OCEAN) {
        send_to_char(ch, "Your spikes are not effective in this terrain.\r\n");
        return;
      }
      to_char = "Large spikes suddenly protrude from the ground.";
      to_room = "Large spikes suddenly protrude from the ground.";
      aff = RAFF_SPIKE_GROWTH;
      rounds = DIVINE_LEVEL(ch);
      break;
      
    case SPELL_SPIKE_STONES: // transmutation
      if (SECT(ch->in_room) != SECT_MOUNTAIN) {
        send_to_char(ch, "Your spike stones are not effective in this terrain.\r\n");
        return;
      }
      to_char = "Large stone spikes suddenly protrude from the ground.";
      to_room = "Large stone spikes suddenly protrude from the ground.";
      aff = RAFF_SPIKE_STONES;
      rounds = DIVINE_LEVEL(ch);
      break;

    case SPELL_STINKING_CLOUD: //conjuration
      to_char = "Clouds of billowing stinking fumes fill the area.";
      to_room = "$n creates clouds of billowing stinking fumes that fill the area.";
      aff = RAFF_STINK;
      rounds = 12;
      break;

    case SPELL_UNHALLOW: // evocation
      to_char = "An unholy aura fills the area.";
      to_room = "An unholy aura fills the area as $n finishes $s spell.";
      aff = RAFF_UNHOLY;
      rounds = 1000;
      break;
      
    case SPELL_WALL_OF_FOG: //illusion
      to_char = "You create a fog out of nowhere.";
      to_room = "$n creates a fog out of nowhere.";
      aff = RAFF_FOG;
      rounds = 8 + CASTER_LEVEL(ch);
      break;

    /*******  END ROOM AFFECTIONS ***********/
      
    default:
      sprintf(buf, "SYSERR: unknown spellnum %d passed to mag_room", spellnum);
      log(buf);
      break;
  }

  /* no event data or room-affection */
  if (IdNum == -1 && aff == -1) {
    send_to_char(ch, "Your spell is inert!\r\n");
    return;
  }
  
  /* failed for whatever reason! */
  if (failure) {
    send_to_char(ch, "You failed!\r\n");
    return;
  }
  
  /* first check if this is a room event */
  if (IdNum != -1) {
    /* note, as of now we are setting the room flag in the switch() above */
    NEW_EVENT(IdNum, &world[rnum], NULL, rounds * PULSE_VIOLENCE);
  }
  /* ok, must be a room affection */
  else if (aff != -1) {
    /* create, initialize, and link a room-affection node */
    CREATE(raff, struct raff_node, 1);
    raff->room = rnum;
    raff->timer = rounds;
    raff->affection = aff;
    raff->ch = ch;
    raff->spell = spellnum;
    raff->next = raff_list;
    raff_list = raff;

    /* set the affection */
    SET_BIT(ROOM_AFFECTIONS(raff->room), aff);
  } else {
    /* should not get here */
    send_to_char(ch, "Your spell is completely inert!\r\n");
    return;    
  }
  
  /* OK send message now */
  if (to_char == NULL)
    send_to_char(ch, "%s", CONFIG_NOEFFECT);
  else
    act(to_char, TRUE, ch, 0, 0, TO_CHAR);

  if (to_room != NULL)
    act(to_room, TRUE, ch, 0, 0, TO_ROOM);
  else if (to_char != NULL)
    act(to_char, TRUE, ch, 0, 0, TO_ROOM);
}


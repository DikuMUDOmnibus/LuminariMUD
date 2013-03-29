/**************************************************************************
 *  File: spell_parser.c                                    Part of tbaMUD *
 *  Usage: Top-level magic routines; outside points of entry to magic sys. *
 *                                                                         *
 *  All rights reserved.  See license for complete information.            *
 *                                                                         *
 *  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
 *  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
 **************************************************************************/

#define __SPELL_PARSER_C__

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "interpreter.h"
#include "spells.h"
#include "handler.h"
#include "comm.h"
#include "db.h"
#include "dg_scripts.h"
#include "fight.h"  /* for hit() */
#include "constants.h"
#include "mud_event.h"
#include "spec_procs.h"

#define SINFO spell_info[spellnum]

/* Global Variables definitions, used elsewhere */
struct spell_info_type spell_info[TOP_SPELL_DEFINE + 1];
char cast_arg2[MAX_INPUT_LENGTH] = {'\0'};
const char *unused_spellname = "!UNUSED!"; /* So we can get &unused_spellname */
const char *unused_wearoff = "!UNUSED WEAROFF!"; /* So we can get &unused_wearoff */

/* Local (File Scope) Function Prototypes */
static void say_spell(struct char_data *ch, int spellnum, struct char_data *tch,
        struct obj_data *tobj, bool start);
static void spello(int spl, const char *name, int max_mana, int min_mana,
        int mana_change, int minpos, int targets, int violent, int routines,
        const char *wearoff, int time, int memtime, int school);
//static int mag_manacost(struct char_data *ch, int spellnum);

/* Local (File Scope) Variables */
struct syllable {
  const char *org;
  const char *news;
};
static struct syllable syls[] = {
  {" ", " "},
  {"ar", "abra"},
  {"ate", "i"},
  {"cau", "kada"},
  {"blind", "nose"},
  {"bur", "mosa"},
  {"cu", "judi"},
  {"de", "oculo"},
  {"dis", "mar"},
  {"ect", "kamina"},
  {"en", "uns"},
  {"gro", "cra"},
  {"light", "dies"},
  {"lo", "hi"},
  {"magi", "kari"},
  {"mon", "bar"},
  {"mor", "zak"},
  {"move", "sido"},
  {"ness", "lacri"},
  {"ning", "illa"},
  {"per", "duda"},
  {"ra", "gru"},
  {"re", "candus"},
  {"son", "sabru"},
  {"tect", "infra"},
  {"tri", "cula"},
  {"ven", "nofo"},
  {"word of", "inset"},
  {"a", "i"},
  {"b", "v"},
  {"c", "q"},
  {"d", "m"},
  {"e", "o"},
  {"f", "y"},
  {"g", "t"},
  {"h", "p"},
  {"i", "u"},
  {"j", "y"},
  {"k", "t"},
  {"l", "r"},
  {"m", "w"},
  {"n", "b"},
  {"o", "a"},
  {"p", "s"},
  {"q", "d"},
  {"r", "f"},
  {"s", "g"},
  {"t", "h"},
  {"u", "e"},
  {"v", "z"},
  {"w", "x"},
  {"x", "n"},
  {"y", "l"},
  {"z", "k"},
  {"", ""},
  {"1", "echad"},
  {"2", "shtayim"},
  {"3", "shelosh"},
  {"4", "arba"},
  {"5", "chamesh"},
  {"6", "sheish"},
  {"7", "shevah"},
  {"8", "shmoneh"},
  {"9", "teisha"},
  {"0", "efes"}
};

/* may use this for mobs to control their casting
static int mag_manacost(struct char_data *ch, int spellnum)
{

  return (MAX(SINFO.mana_max - (SINFO.mana_change *
              (GET_LEVEL(ch) - SINFO.min_level[(int) GET_CLASS(ch)])),
          SINFO.mana_min) / 2);
}
 */
/* calculates lowest possible level of a spell (spells can be different
 levels for different classes) */
int lowest_spell_level(int spellnum) {
  int i, lvl = SINFO.min_level[0];

  for (i = 1; i < NUM_CLASSES; i++)
    if (lvl >= SINFO.min_level[i])
      lvl = SINFO.min_level[i];

  return lvl;
}

/* displays substitude text for spells to represent 'magical phrases' */
static void say_spell(struct char_data *ch, int spellnum, struct char_data *tch,
        struct obj_data *tobj, bool start) {
  char lbuf[MEDIUM_STRING], buf[MEDIUM_STRING],
          buf1[MEDIUM_STRING], buf2[MEDIUM_STRING]; /* FIXME */
  const char *format;
  struct char_data *i;
  int j, ofs = 0, dc_of_id = 0, attempt = 0;

  dc_of_id = 20; //DC of identifying the spell

  *buf = '\0';
  strlcpy(lbuf, skill_name(spellnum), sizeof (lbuf));

  while (lbuf[ofs]) {
    for (j = 0; *(syls[j].org); j++) {
      if (!strncmp(syls[j].org, lbuf + ofs, strlen(syls[j].org))) {
        strcat(buf, syls[j].news); /* strcat: BAD */
        ofs += strlen(syls[j].org);
        break;
      }
    }
    /* i.e., we didn't find a match in syls[] */
    if (!*syls[j].org) {
      log("No entry in syllable table for substring of '%s'", lbuf);
      ofs++;
    }
  }

  if (tch != NULL && IN_ROOM(tch) == IN_ROOM(ch)) {
    if (tch == ch) {
      if (!start)
        format = "\tn$n \tccloses $s eyes and utters the words, '\tC%s\tc'.\tn";
      else
        format =
              "\tn$n \tcweaves $s hands in an \tmintricate\tc pattern and begins to chant the words, '\tC%s\tc'.\tn";
    } else {
      if (!start)
        format = "\tn$n \tcstares at \tn$N\tc and utters the words, '\tC%s\tc'.\tn";
      else
        format =
              "\tn$n \tcweaves $s hands in an \tmintricate\tc pattern and begins to chant the words, '\tC%s\tc' at \tn$N\tc.\tn";
    }
  } else if (tobj != NULL &&
          ((IN_ROOM(tobj) == IN_ROOM(ch)) || (tobj->carried_by == ch))) {
    if (!start)
      format = "\tn$n \tcstares at $p and utters the words, '\tC%s\tc'.\tn";
    else
      format = "\tn$n \tcstares at $p and begins chanting the words, '\tC%s\tc'.\tn";
  } else {
    if (!start)
      format = "\tn$n \tcutters the words, '\tC%s\tc'.\tn";
    else
      format = "\tn$n \tcbegins chanting the words, '\tC%s\tc'.\tn";
  }

  snprintf(buf1, sizeof (buf1), format, skill_name(spellnum));
  snprintf(buf2, sizeof (buf2), format, buf);

  for (i = world[IN_ROOM(ch)].people; i; i = i->next_in_room) {
    if (i == ch || i == tch || !i->desc || !AWAKE(i))
      continue;

    if (!IS_NPC(i))
      attempt = compute_ability(i, ABILITY_SPELLCRAFT) + dice(1, 20);
    else
      attempt = 10 + dice(1, 20);

    if (attempt > dc_of_id)
      perform_act(buf1, ch, tobj, tch, i);
    else
      perform_act(buf2, ch, tobj, tch, i);
  }

  if (tch != NULL && !IS_NPC(tch))
    attempt = compute_ability(tch, ABILITY_SPELLCRAFT) + dice(1, 20);
  else
    attempt = 10 + dice(1, 20);

  if (tch != NULL && tch != ch && IN_ROOM(tch) == IN_ROOM(ch)) {
    if (!start)
      snprintf(buf1, sizeof (buf1), "\tn$n \tcstares at you and utters the words, '\tC%s\tc'.\tn",
            attempt > dc_of_id ? skill_name(spellnum) : buf);
    else
      snprintf(buf1, sizeof (buf1),
            "\tn$n \tcweaves $s hands in an intricate pattern and begins to chant the words, '\tC%s\tc' at you.\tn",
            attempt > dc_of_id ? skill_name(spellnum) : buf);
    act(buf1, FALSE, ch, NULL, tch, TO_VICT);
  }

}

/* checks if a spellnum corresponds to an epic spell */
bool isEpicSpell(int spellnum) {
  switch (spellnum) {
    case SPELL_MUMMY_DUST:
    case SPELL_DRAGON_KNIGHT:
    case SPELL_GREATER_RUIN:
    case SPELL_HELLBALL:
    case SPELL_EPIC_MAGE_ARMOR:
    case SPELL_EPIC_WARDING:
      return TRUE;
  }
  return FALSE;
}

/* This function should be used anytime you are not 100% sure that you have
 * a valid spell/skill number.  A typical for() loop would not need to use
 * this because you can guarantee > 0 and <= TOP_SPELL_DEFINE. */
const char *skill_name(int num) {
  if (num > 0 && num <= TOP_SPELL_DEFINE)
    return (spell_info[num].name);
  else if (num == -1)
    return ("Not-Used");
  else
    return ("Non-Spell-Effect");
}

/* send a string that is theortically the name of a spell/skill, return
   the spell/skill number
 */
int find_skill_num(char *name) {
  int skindex, ok;
  char *temp, *temp2;
  char first[MEDIUM_STRING], first2[MEDIUM_STRING], tempbuf[MEDIUM_STRING];

  for (skindex = 1; skindex <= TOP_SPELL_DEFINE; skindex++) {
    if (is_abbrev(name, spell_info[skindex].name))
      return (skindex);

    ok = TRUE;
    strlcpy(tempbuf, spell_info[skindex].name, sizeof (tempbuf)); /* strlcpy: OK */
    temp = any_one_arg(tempbuf, first);
    temp2 = any_one_arg(name, first2);
    while (*first && *first2 && ok) {
      if (!is_abbrev(first2, first))
        ok = FALSE;
      temp = any_one_arg(temp, first);
      temp2 = any_one_arg(temp2, first2);
    }

    if (ok && !*first2)
      return (skindex);
  }

  return (-1);
}

/* send a string that is theortically the name of an ability, return
   the ability number
 */
int find_ability_num(char *name) {
  int skindex, ok;
  char *temp, *temp2;
  char first[MEDIUM_STRING], first2[MEDIUM_STRING], tempbuf[MEDIUM_STRING];

  for (skindex = 1; skindex < NUM_ABILITIES; skindex++) {
    if (is_abbrev(name, ability_names[skindex]))
      return (skindex);

    ok = TRUE;
    strlcpy(tempbuf, ability_names[skindex], sizeof (tempbuf));
    temp = any_one_arg(tempbuf, first);
    temp2 = any_one_arg(name, first2);
    while (*first && *first2 && ok) {
      if (!is_abbrev(first2, first))
        ok = FALSE;
      temp = any_one_arg(temp, first);
      temp2 = any_one_arg(temp2, first2);
    }

    if (ok && !*first2)
      return (skindex);
  }

  return (-1);
}

/* This function is the very heart of the entire magic system.  All invocations
 * of all types of magic -- objects, spoken and unspoken PC and NPC spells, the
 * works -- all come through this function eventually. This is also the entry
 * point for non-spoken or unrestricted spells. Spellnum 0 is legal but silently
 * ignored here, to make callers simpler. */
int call_magic(struct char_data *caster, struct char_data *cvict,
        struct obj_data *ovict, int spellnum, int level, int casttype) {
  int savetype = 0;
  struct char_data *tmp = NULL;

  if (spellnum < 1 || spellnum > TOP_SPELL_DEFINE)
    return (0);

  if (!cast_wtrigger(caster, cvict, ovict, spellnum))
    return 0;
  if (!cast_otrigger(caster, ovict, spellnum))
    return 0;
  if (!cast_mtrigger(caster, cvict, spellnum))
    return 0;

  if (ROOM_AFFECTED(caster->in_room, RAFF_ANTI_MAGIC)) {
    send_to_char(caster, "Your magic fizzles out and dies!\r\n");
    act("$n's magic fizzles out and dies...", FALSE, caster, 0, 0, TO_ROOM);
    return (0);
  }

  if (cvict && ROOM_AFFECTED(cvict->in_room, RAFF_ANTI_MAGIC)) {
    send_to_char(caster, "Your magic fizzles out and dies!\r\n");
    act("$n's magic fizzles out and dies...", FALSE, caster, 0, 0, TO_ROOM);
    return (0);
  }

  if (ROOM_FLAGGED(IN_ROOM(caster), ROOM_NOMAGIC)) {
    send_to_char(caster, "Your magic fizzles out and dies.\r\n");
    act("$n's magic fizzles out and dies.", FALSE, caster, 0, 0, TO_ROOM);
    return (0);
  }
  
  if (cvict && ROOM_FLAGGED(IN_ROOM(cvict), ROOM_NOMAGIC)) {
    send_to_char(caster, "Your magic fizzles out and dies.\r\n");
    act("$n's magic fizzles out and dies.", FALSE, caster, 0, 0, TO_ROOM);
    return (0);
  }
  
  if (ROOM_FLAGGED(IN_ROOM(caster), ROOM_PEACEFUL) &&
          (SINFO.violent || IS_SET(SINFO.routines, MAG_DAMAGE))) {
    send_to_char(caster, "A flash of white light fills the room, dispelling your violent magic!\r\n");
    act("White light from no particular source suddenly fills the room, then vanishes.", FALSE, caster, 0, 0, TO_ROOM);
    return (0);
  }
  
  if (cvict && MOB_FLAGGED(cvict, MOB_NOKILL)) {
    send_to_char(caster, "This mob is protected.\r\n");
    return (0);
  }

  //attach event for epic spells, increase skill
  switch (spellnum) {
    case SPELL_MUMMY_DUST:
      attach_mud_event(new_mud_event(eMUMMYDUST, caster, NULL), 3 * SECS_PER_MUD_DAY);
      if (!IS_NPC(caster))
        increase_skill(caster, SKILL_MUMMY_DUST);
      break;
    case SPELL_DRAGON_KNIGHT:
      attach_mud_event(new_mud_event(eDRAGONKNIGHT, caster, NULL), 3 * SECS_PER_MUD_DAY);
      if (!IS_NPC(caster))
        increase_skill(caster, SKILL_DRAGON_KNIGHT);
      break;
    case SPELL_GREATER_RUIN:
      attach_mud_event(new_mud_event(eGREATERRUIN, caster, NULL), 3 * SECS_PER_MUD_DAY);
      if (!IS_NPC(caster))
        increase_skill(caster, SKILL_GREATER_RUIN);
      break;
    case SPELL_HELLBALL:
      attach_mud_event(new_mud_event(eHELLBALL, caster, NULL), 3 * SECS_PER_MUD_DAY);
      if (!IS_NPC(caster))
        increase_skill(caster, SKILL_HELLBALL);
      break;
    case SPELL_EPIC_MAGE_ARMOR:
      attach_mud_event(new_mud_event(eEPICMAGEARMOR, caster, NULL), 3 * SECS_PER_MUD_DAY);
      if (!IS_NPC(caster))
        increase_skill(caster, SKILL_EPIC_MAGE_ARMOR);
      break;
    case SPELL_EPIC_WARDING:
      attach_mud_event(new_mud_event(eEPICWARDING, caster, NULL), 3 * SECS_PER_MUD_DAY);
      if (!IS_NPC(caster))
        increase_skill(caster, SKILL_EPIC_WARDING);
      break;
  }

  /* globe of invulernability spell(s)
   * and spell mantles */
  if (cvict) {
    int lvl = lowest_spell_level(spellnum);    

    /* minor globe */
    /* we're translating level to circle, so 4 = 2nd circle */
    if (AFF_FLAGGED(cvict, AFF_MINOR_GLOBE) && lvl <= 4 &&
            (SINFO.violent || IS_SET(SINFO.routines, MAG_DAMAGE))) {
      send_to_char(caster,
              "A minor globe from your victim repels your spell!\r\n");
      act("$n's magic is repelled by $N's minor globe spell!", FALSE, caster,
              0, cvict, TO_ROOM);
      if (!FIGHTING(caster))
        set_fighting(caster, cvict);
      if (!FIGHTING(cvict))
        set_fighting(cvict, caster);
      return (0);

      /* major globe */
      /* we're translating level to circle so 8 = 4th circle */
    } else if (AFF_FLAGGED(cvict, AFF_GLOBE_OF_INVULN) && lvl <= 8 &&
            (SINFO.violent || IS_SET(SINFO.routines, MAG_DAMAGE))) {
      send_to_char(caster, "A globe from your victim repels your spell!\r\n");
      act("$n's magic is repelled by $N's globe spell!", FALSE, caster, 0,
              cvict, TO_ROOM);
      if (!FIGHTING(caster))
        set_fighting(caster, cvict);
      if (!FIGHTING(cvict))
        set_fighting(cvict, caster);
      return (0);

      /* here is spell mantles */
    } else if (AFF_FLAGGED(cvict, AFF_SPELL_MANTLE) &&
            GET_SPELL_MANTLE(cvict) > 0 && (SINFO.violent ||
            IS_SET(SINFO.routines, MAG_DAMAGE))) {
      send_to_char(caster, "A spell mantle from your victim absorbs your spell!\r\n");
      act("$n's magic is absorbed by $N's spell mantle!", FALSE, caster, 0,
              cvict, TO_ROOM);
      GET_SPELL_MANTLE(cvict)--;
      if (GET_SPELL_MANTLE(cvict) <= 0) {
        affect_from_char(cvict, SPELL_SPELL_MANTLE);
        affect_from_char(cvict, SPELL_GREATER_SPELL_MANTLE);
        send_to_char(cvict, "\tDYour spell mantle has fallen!\tn\r\n");
      }
      if (!FIGHTING(caster))
        set_fighting(caster, cvict);
      if (!FIGHTING(cvict))
        set_fighting(cvict, caster);
      return (0);
    }
  }

  /* determine the type of saving throw */
  switch (casttype) {
    case CAST_STAFF:
    case CAST_SCROLL:
    case CAST_POTION:
    case CAST_WAND:
      savetype = SAVING_WILL;
      break;
    case CAST_SPELL:
      savetype = SAVING_WILL;
      break;
    default:
      savetype = SAVING_WILL;
      break;
  }

  /* spell turning */
  if (cvict) {
    if (AFF_FLAGGED(cvict, AFF_SPELL_TURNING) && (SINFO.violent ||
            IS_SET(SINFO.routines, MAG_DAMAGE))) {
      send_to_char(caster, "Your spell has been turned!\r\n");
      act("$n's magic is turned by $N!", FALSE, caster, 0,
              cvict, TO_ROOM);
      REMOVE_BIT_AR(AFF_FLAGS(cvict), AFF_SPELL_TURNING);
      tmp = cvict;
      cvict = caster;
      caster = tmp;
    }
  }

  if (IS_SET(SINFO.routines, MAG_DAMAGE))
    if (mag_damage(level, caster, cvict, ovict, spellnum, savetype) == -1)
      return (-1); /* Successful and target died, don't cast again. */

  if (IS_SET(SINFO.routines, MAG_AFFECTS))
    mag_affects(level, caster, cvict, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_UNAFFECTS))
    mag_unaffects(level, caster, cvict, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_POINTS))
    mag_points(level, caster, cvict, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_ALTER_OBJS))
    mag_alter_objs(level, caster, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_GROUPS))
    mag_groups(level, caster, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_MASSES))
    mag_masses(level, caster, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_AREAS))
    mag_areas(level, caster, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_SUMMONS))
    mag_summons(level, caster, ovict, spellnum, savetype);

  if (IS_SET(SINFO.routines, MAG_CREATIONS))
    mag_creations(level, caster, cvict, ovict, spellnum);

  if (IS_SET(SINFO.routines, MAG_ROOM))
    mag_room(level, caster, ovict, spellnum);

  /* this switch statement sends us to spells.c for the manual spells */
  if (IS_SET(SINFO.routines, MAG_MANUAL))
    switch (spellnum) {
      case SPELL_ACID_ARROW:
        MANUAL_SPELL(spell_acid_arrow);
        break;
      case SPELL_BANISH:
        MANUAL_SPELL(spell_banish);
        break;
      case SPELL_CHARM:
        MANUAL_SPELL(spell_charm);
        break;
      case SPELL_CHARM_ANIMAL:
        MANUAL_SPELL(spell_charm_animal);
        break;
      case SPELL_CLAIRVOYANCE:
        MANUAL_SPELL(spell_clairvoyance);
        break;
      case SPELL_CLOUDKILL:
        MANUAL_SPELL(spell_cloudkill);
        break;
      case SPELL_CONTROL_PLANTS:
        MANUAL_SPELL(spell_control_plants);
        break;
      case SPELL_CONTROL_WEATHER:
        MANUAL_SPELL(spell_control_weather);
        break;
      case SPELL_CREATE_WATER:
        MANUAL_SPELL(spell_create_water);
        break;
      case SPELL_CREEPING_DOOM:
        MANUAL_SPELL(spell_creeping_doom);
        break;
      case SPELL_DETECT_POISON:
        MANUAL_SPELL(spell_detect_poison);
        break;
      case SPELL_DISMISSAL:
        MANUAL_SPELL(spell_dismissal);
        break;
      case SPELL_DISPEL_MAGIC:
        MANUAL_SPELL(spell_dispel_magic);
        break;
      case SPELL_DOMINATE_PERSON:
        MANUAL_SPELL(spell_dominate_person);
        break;
      case SPELL_ENCHANT_WEAPON:
        MANUAL_SPELL(spell_enchant_weapon);
        break;
      case SPELL_GREATER_DISPELLING:
        MANUAL_SPELL(spell_greater_dispelling);
        break;
      case SPELL_GROUP_SUMMON:
        MANUAL_SPELL(spell_group_summon);
        break;
      case SPELL_IDENTIFY:
        MANUAL_SPELL(spell_identify);
        break;
      case SPELL_IMPLODE:
        MANUAL_SPELL(spell_implode);
        break;
      case SPELL_INCENDIARY_CLOUD:
        MANUAL_SPELL(spell_incendiary_cloud);
        break;
      case SPELL_LOCATE_CREATURE:
        MANUAL_SPELL(spell_locate_creature);
        break;
      case SPELL_LOCATE_OBJECT:
        MANUAL_SPELL(spell_locate_object);
        break;
      case SPELL_MASS_DOMINATION:
        MANUAL_SPELL(spell_mass_domination);
        break;
      case SPELL_PLANE_SHIFT:
        MANUAL_SPELL(spell_plane_shift);
        break;
      case SPELL_POLYMORPH:
        MANUAL_SPELL(spell_polymorph);
        break;
      case SPELL_PRISMATIC_SPHERE:
        MANUAL_SPELL(spell_prismatic_sphere);
        break;
      case SPELL_REFUGE:
        MANUAL_SPELL(spell_refuge);
        break;
      case SPELL_SALVATION:
        MANUAL_SPELL(spell_salvation);
        break;
      case SPELL_SPELLSTAFF:
        MANUAL_SPELL(spell_spellstaff);
        break;
      case SPELL_STORM_OF_VENGEANCE:
        MANUAL_SPELL(spell_storm_of_vengeance);
        break;
      case SPELL_SUMMON:
        MANUAL_SPELL(spell_summon);
        break;
      case SPELL_TELEPORT:
        MANUAL_SPELL(spell_teleport);
        break;
      case SPELL_TRANSPORT_VIA_PLANTS:
        MANUAL_SPELL(spell_transport_via_plants);
        break;
      case SPELL_WALL_OF_FORCE:
        MANUAL_SPELL(spell_wall_of_force);
        break;
      case SPELL_WIZARD_EYE:
        MANUAL_SPELL(spell_wizard_eye);
        break;
      case SPELL_WORD_OF_RECALL:
        MANUAL_SPELL(spell_recall);
        break;
    } /* end manual spells */

  /* NOTE:  this requires a victim, so AoE effects have another
     similar method added -zusuk */
  if (SINFO.violent && cvict && GET_POS(cvict) == POS_STANDING &&
          !FIGHTING(cvict) && spellnum != SPELL_CHARM && spellnum != SPELL_CHARM_ANIMAL &&
          spellnum != SPELL_DOMINATE_PERSON) {
    if (cvict != caster) { // funny results from potions/scrolls
      if (IN_ROOM(cvict) == IN_ROOM(caster)) {
        hit(cvict, caster, TYPE_UNDEFINED, DAM_RESERVED_DBC, 0, FALSE);
      }
    }
  }

  return (1);
}

/* mag_objectmagic: This is the entry-point for all magic items.  This should
 * only be called by the 'quaff', 'use', 'recite', etc. routines.
 * For reference, object values 0-3:
 * staff  - [0]	level	[1] max charges	[2] num charges	[3] spell num
 * wand   - [0]	level	[1] max charges	[2] num charges	[3] spell num
 * scroll - [0]	level	[1] spell num	[2] spell num	[3] spell num
 * potion - [0] level	[1] spell num	[2] spell num	[3] spell num
 * Staves and wands will default to level 14 if the level is not specified; the
 * DikuMUD format did not specify staff and wand levels in the world files */
void mag_objectmagic(struct char_data *ch, struct obj_data *obj,
        char *argument) {
  char arg[MAX_INPUT_LENGTH];
  int i, k;
  struct char_data *tch = NULL, *next_tch;
  struct obj_data *tobj = NULL;

  one_argument(argument, arg);

  k = generic_find(arg, FIND_CHAR_ROOM | FIND_OBJ_INV | FIND_OBJ_ROOM |
          FIND_OBJ_EQUIP, ch, &tch, &tobj);

  switch (GET_OBJ_TYPE(obj)) {
    case ITEM_STAFF:
      act("You tap $p three times on the ground.", FALSE, ch, obj, 0, TO_CHAR);
      if (obj->action_description)
        act(obj->action_description, FALSE, ch, obj, 0, TO_ROOM);
      else
        act("$n taps $p three times on the ground.", FALSE, ch, obj, 0, TO_ROOM);

      if (GET_OBJ_VAL(obj, 2) <= 0) {
        send_to_char(ch, "It seems powerless.\r\n");
        act("Nothing seems to happen.", FALSE, ch, obj, 0, TO_ROOM);
      } else {
        GET_OBJ_VAL(obj, 2)--;
        SET_WAIT(ch, PULSE_VIOLENCE);
        /* Level to cast spell at. */
        k = GET_OBJ_VAL(obj, 0) ? GET_OBJ_VAL(obj, 0) : DEFAULT_STAFF_LVL;

        /* Area/mass spells on staves can cause crashes. So we use special cases
         * for those spells spells here. */
        if (HAS_SPELL_ROUTINE(GET_OBJ_VAL(obj, 3), MAG_MASSES | MAG_AREAS)) {
          for (i = 0, tch = world[IN_ROOM(ch)].people; tch; tch = tch->next_in_room)
            i++;
          while (i-- > 0)
            call_magic(ch, NULL, NULL, GET_OBJ_VAL(obj, 3), k, CAST_STAFF);
        } else {
          for (tch = world[IN_ROOM(ch)].people; tch; tch = next_tch) {
            next_tch = tch->next_in_room;
            if (ch != tch)
              call_magic(ch, tch, NULL, GET_OBJ_VAL(obj, 3), k, CAST_STAFF);
          }
        }
      }
      break;
    case ITEM_WAND:
      if (k == FIND_CHAR_ROOM) {
        if (tch == ch) {
          act("You point $p at yourself.", FALSE, ch, obj, 0, TO_CHAR);
          act("$n points $p at $mself.", FALSE, ch, obj, 0, TO_ROOM);
        } else {
          act("You point $p at $N.", FALSE, ch, obj, tch, TO_CHAR);
          if (obj->action_description)
            act(obj->action_description, FALSE, ch, obj, tch, TO_ROOM);
          else
            act("$n points $p at $N.", TRUE, ch, obj, tch, TO_ROOM);
        }
      } else if (tobj != NULL) {
        act("You point $p at $P.", FALSE, ch, obj, tobj, TO_CHAR);
        if (obj->action_description)
          act(obj->action_description, FALSE, ch, obj, tobj, TO_ROOM);
        else
          act("$n points $p at $P.", TRUE, ch, obj, tobj, TO_ROOM);
      } else if (IS_SET(spell_info[GET_OBJ_VAL(obj, 3)].routines, MAG_AREAS | MAG_MASSES)) {
        /* Wands with area spells don't need to be pointed. */
        act("You point $p outward.", FALSE, ch, obj, NULL, TO_CHAR);
        act("$n points $p outward.", TRUE, ch, obj, NULL, TO_ROOM);
      } else {
        act("At what should $p be pointed?", FALSE, ch, obj, NULL, TO_CHAR);
        return;
      }

      if (GET_OBJ_VAL(obj, 2) <= 0) {
        send_to_char(ch, "It seems powerless.\r\n");
        act("Nothing seems to happen.", FALSE, ch, obj, 0, TO_ROOM);
        return;
      }
      GET_OBJ_VAL(obj, 2)--;
      SET_WAIT(ch, PULSE_VIOLENCE);
      if (GET_OBJ_VAL(obj, 0))
        call_magic(ch, tch, tobj, GET_OBJ_VAL(obj, 3),
              GET_OBJ_VAL(obj, 0), CAST_WAND);
      else
        call_magic(ch, tch, tobj, GET_OBJ_VAL(obj, 3),
              DEFAULT_WAND_LVL, CAST_WAND);
      break;
    case ITEM_SCROLL:
      if (*arg) {
        if (!k) {
          act("There is nothing to here to affect with $p.", FALSE,
                  ch, obj, NULL, TO_CHAR);
          return;
        }
      } else
        tch = ch;

      act("You recite $p which dissolves.", TRUE, ch, obj, 0, TO_CHAR);
      if (obj->action_description)
        act(obj->action_description, FALSE, ch, obj, tch, TO_ROOM);
      else
        act("$n recites $p.", FALSE, ch, obj, NULL, TO_ROOM);

      SET_WAIT(ch, PULSE_VIOLENCE);
      for (i = 1; i <= 3; i++)
        if (call_magic(ch, tch, tobj, GET_OBJ_VAL(obj, i),
                GET_OBJ_VAL(obj, 0), CAST_SCROLL) <= 0)
          break;

      if (obj != NULL)
        extract_obj(obj);
      break;
    case ITEM_POTION:
      tch = ch;

      if (!consume_otrigger(obj, ch, OCMD_QUAFF)) /* check trigger */
        return;

      act("You quaff $p.", FALSE, ch, obj, NULL, TO_CHAR);
      if (obj->action_description)
        act(obj->action_description, FALSE, ch, obj, NULL, TO_ROOM);
      else
        act("$n quaffs $p.", TRUE, ch, obj, NULL, TO_ROOM);

      SET_WAIT(ch, PULSE_VIOLENCE);
      for (i = 1; i <= 3; i++)
        if (call_magic(ch, ch, NULL, GET_OBJ_VAL(obj, i),
                GET_OBJ_VAL(obj, 0), CAST_POTION) <= 0)
          break;

      if (obj != NULL)
        extract_obj(obj);
      break;
    default:
      log("SYSERR: Unknown object_type %d in mag_objectmagic.",
              GET_OBJ_TYPE(obj));
      break;
  }
}

void resetCastingData(struct char_data *ch) {
  IS_CASTING(ch) = FALSE;
  CASTING_TIME(ch) = 0;
  CASTING_SPELLNUM(ch) = 0;
  CASTING_TCH(ch) = NULL;
  CASTING_TOBJ(ch) = NULL;
}

int castingCheckOk(struct char_data *ch) {
  int spellnum = CASTING_SPELLNUM(ch);

  if ((GET_POS(ch) != POS_STANDING &&
          GET_POS(ch) != POS_FIGHTING) ||
          (CASTING_TOBJ(ch) && CASTING_TOBJ(ch)->in_room != ch->in_room &&
          !IS_SET(SINFO.targets, TAR_OBJ_WORLD | TAR_OBJ_INV)) ||
          (CASTING_TCH(ch) && CASTING_TCH(ch)->in_room != ch->in_room && SINFO.violent)) {
    act("A spell from $n is aborted!", FALSE, ch, 0, 0,
            TO_ROOM);
    send_to_char(ch, "You are unable to continue your spell!\r\n");
    resetCastingData(ch);
    return 0;
  }
  if (AFF_FLAGGED(ch, AFF_NAUSEATED)) {
    send_to_char(ch, "You are too nauseated to continue casting!\r\n");
    act("$n seems to be too nauseated to continue casting!",
            TRUE, ch, 0, 0, TO_ROOM);
    resetCastingData(ch);
    return (0);
  }
  if (AFF_FLAGGED(ch, AFF_STUN) || AFF_FLAGGED(ch, AFF_PARALYZED) ||
          char_has_mud_event(ch, eSTUNNED)) {
    send_to_char(ch, "You are unable to continue casting!\r\n");
    act("$n seems to be unable to continue casting!",
            TRUE, ch, 0, 0, TO_ROOM);
    resetCastingData(ch);
    return (0);
  }
  return 1;
}

void finishCasting(struct char_data *ch) {
  say_spell(ch, CASTING_SPELLNUM(ch), CASTING_TCH(ch), CASTING_TOBJ(ch), FALSE);
  send_to_char(ch, "You complete your spell...");
  call_magic(ch, CASTING_TCH(ch), CASTING_TOBJ(ch), CASTING_SPELLNUM(ch),
          CASTER_LEVEL(ch), CAST_SPELL);
  resetCastingData(ch);
}

EVENTFUNC(event_casting) {
  struct char_data *ch;
  struct mud_event_data *pMudEvent;
  int x, failure = -1, time_stopped = FALSE;
  char buf[MAX_INPUT_LENGTH];

  //initialize everything and dummy checks
  if (event_obj == NULL) return 0;
  pMudEvent = (struct mud_event_data *) event_obj;
  ch = (struct char_data *) pMudEvent->pStruct;
  if (!IS_NPC(ch) && !IS_PLAYING(ch->desc)) return 0;
  int spellnum = CASTING_SPELLNUM(ch);

  // is he casting?
  if (!IS_CASTING(ch))
    return 0;

  // this spell time-stoppable?
  if (IS_AFFECTED(ch, AFF_TIME_STOPPED) &&
          !SINFO.violent && !IS_SET(SINFO.routines, MAG_DAMAGE)) {
    time_stopped = TRUE;
  }

  // still some time left to cast
  if ((CASTING_TIME(ch) > 0) && !time_stopped) {

    //checking positions, targets
    if (!castingCheckOk(ch))
      return 0;
    else {
      // concentration challenge
      failure += spell_info[spellnum].min_level[CASTING_CLASS(ch)] * 2;
      if (!IS_NPC(ch))
        failure -= CASTER_LEVEL(ch) + ((compute_ability(ch, ABILITY_CONCENTRATION) - 3) * 2);
      else
        failure -= (GET_LEVEL(ch)) * 3;
      //chance of failure calculated here, so far:  taunt, grappled
      if (char_has_mud_event(ch, eTAUNTED))
        failure += 15;
      if (AFF_FLAGGED(ch, AFF_GRAPPLED))
        failure += 15;
      if (!FIGHTING(ch))
        failure -= 50;

      if (dice(1, 101) < failure) {
        send_to_char(ch, "You lost your concentration!\r\n");
        resetCastingData(ch);
        return 0;
      }

      //display time left to finish spell
      sprintf(buf, "Casting: %s ", SINFO.name);
      for (x = CASTING_TIME(ch); x > 0; x--)
        strcat(buf, "*");
      strcat(buf, "\r\n");
      send_to_char(ch, buf);

      if (!IS_NPC(ch) && GET_SKILL(ch, SKILL_QUICK_CHANT))
        if (!IS_NPC(ch) && GET_SKILL(ch, SKILL_QUICK_CHANT) > dice(1, 100))
          CASTING_TIME(ch)--;

      CASTING_TIME(ch)--;

      //chance quick chant bumped us to finish early
      if (CASTING_TIME(ch) <= 0) {

        //do all our checks
        if (!castingCheckOk(ch))
          return 0;

        finishCasting(ch);
        return 0;
      } else
        return (7);
    }

    //spell needs to be completed now (casting time <= 0)
  } else {

    //do all our checks
    if (!castingCheckOk(ch))
      return 0;
    else {
      finishCasting(ch);
      return 0;
    }
  }
}

/* cast_spell is used generically to cast any spoken spell, assuming we already
 * have the target char/obj and spell number.  It checks all restrictions,
 * prints the words, etc. Entry point for NPC casts.  Recommended entry point
 * for spells cast by NPCs via specprocs. */
int cast_spell(struct char_data *ch, struct char_data *tch,
        struct obj_data *tobj, int spellnum) {
  int position = GET_POS(ch);

  if (spellnum < 0 || spellnum > TOP_SPELL_DEFINE) {
    log("SYSERR: cast_spell trying to call spellnum %d/%d.", spellnum,
            TOP_SPELL_DEFINE);
    return (0);
  }

  if (FIGHTING(ch) && GET_POS(ch) > POS_STUNNED)
    position = POS_FIGHTING;

  if (position < SINFO.min_position) {
    switch (position) {
      case POS_SLEEPING:
        send_to_char(ch, "You dream about great magical powers.\r\n");
        break;
      case POS_RESTING:
        send_to_char(ch, "You cannot concentrate while resting.\r\n");
        break;
      case POS_SITTING:
        send_to_char(ch, "You can't do this sitting!\r\n");
        break;
      case POS_FIGHTING:
        send_to_char(ch, "Impossible!  You can't concentrate enough!\r\n");
        break;
      default:
        send_to_char(ch, "You can't do much of anything like this!\r\n");
        break;
    }
    return (0);
  }

  if (AFF_FLAGGED(ch, AFF_CHARM) && (ch->master == tch)) {
    send_to_char(ch, "You are afraid you might hurt your master!\r\n");
    return (0);
  }

  if ((tch != ch) && IS_SET(SINFO.targets, TAR_SELF_ONLY)) {
    send_to_char(ch, "You can only cast this spell upon yourself!\r\n");
    return (0);
  }

  if ((tch == ch) && IS_SET(SINFO.targets, TAR_NOT_SELF)) {
    send_to_char(ch, "You cannot cast this spell upon yourself!\r\n");
    return (0);
  }

  if (IS_SET(SINFO.routines, MAG_GROUPS) && !GROUP(ch)) {
    send_to_char(ch, "You can't cast this spell if you're not in a group!\r\n");
    return (0);
  }

  if (AFF_FLAGGED(ch, AFF_NAUSEATED)) {
    send_to_char(ch, "You are too nauseated to cast!\r\n");
    act("$n seems to be too nauseated to cast!",
            TRUE, ch, 0, 0, TO_ROOM);
    return (0);
  }

  //default casting class will be the highest level casting class
  int class = -1, clevel = -1;

  if (IS_WIZARD(ch)) {
    class = CLASS_WIZARD;
    clevel = IS_WIZARD(ch);
  }
  if (IS_CLERIC(ch) > clevel) {
    class = CLASS_CLERIC;
    clevel = IS_CLERIC(ch);
  }
  if (IS_SORCERER(ch) > clevel) {
    class = CLASS_SORCERER;
    clevel = IS_SORCERER(ch);
  }
  if (IS_DRUID(ch) > clevel) {
    class = CLASS_DRUID;
    clevel = IS_DRUID(ch);
  }
  if (IS_BARD(ch) > clevel) {
    class = CLASS_BARD;
    clevel = IS_BARD(ch);
  }
  /* paladins cast at half their level strength */
  if ((IS_PALADIN(ch) / 2) > clevel) {
    class = CLASS_PALADIN;
    clevel = (IS_PALADIN(ch) / 2);
  }
  /* rangers cast at half their level strength */
  if ((IS_RANGER(ch) / 2) > clevel) {
    class = CLASS_RANGER;
    clevel = (IS_RANGER(ch) / 2);
  }

  if (!isEpicSpell(spellnum) && !IS_NPC(ch)) {
    //      && spellnum != SPELL_ACID_SPLASH && spellnum != SPELL_RAY_OF_FROST) {

    class = forgetSpell(ch, spellnum, -1);

    if (class == -1) {
      send_to_char(ch, "ERR:  Report BUG98237 to an IMM!\r\n");
      return 0;
    }

    /* sorcerer's call is made already in forgetSpell() */
    if (class != CLASS_SORCERER && class != CLASS_BARD)
      addSpellMemming(ch, spellnum, spell_info[spellnum].memtime, class);
  }

  if (SINFO.time <= 0) {
    send_to_char(ch, "%s", CONFIG_OK);
    say_spell(ch, spellnum, tch, tobj, FALSE);
    return (call_magic(ch, tch, tobj, spellnum, CASTER_LEVEL(ch), CAST_SPELL));
  }

  //casting time entry point
  if (char_has_mud_event(ch, eCASTING)) {
    send_to_char(ch, "You are already attempting to cast!\r\n");
    return (0);
  }
  send_to_char(ch, "You begin casting your spell...\r\n");
  say_spell(ch, spellnum, tch, tobj, TRUE);
  IS_CASTING(ch) = TRUE;
  CASTING_TIME(ch) = SINFO.time;
  CASTING_TCH(ch) = tch;
  CASTING_TOBJ(ch) = tobj;
  CASTING_SPELLNUM(ch) = spellnum;
  CASTING_CLASS(ch) = class;
  NEW_EVENT(eCASTING, ch, NULL, 1 * PASSES_PER_SEC);

  //this return value has to be checked -zusuk
  return (1);
}

ACMD(do_abort) {
  if (IS_NPC(ch))
    return;

  if (!IS_CASTING(ch)) {
    send_to_char(ch, "You aren't casting!\r\n");
    return;
  }

  send_to_char(ch, "You abort your spell.\r\n");
  resetCastingData(ch);
}

/* do_cast is the entry point for PC-casted spells.  It parses the arguments,
 * determines the spell number and finds a target, throws the die to see if
 * the spell can be cast, checks for sufficient mana and subtracts it, and
 * passes control to cast_spell(). */
ACMD(do_cast) {
  struct char_data *tch = NULL;
  struct obj_data *tobj = NULL;
  char *s = NULL, *t = NULL;
  int number = 0, spellnum = 0, i = 0, target = 0;
  // int mana;

  if (IS_NPC(ch))
    return;

  /* get: blank, spell name, target name */
  s = strtok(argument, "'");

  if (s == NULL) {
    send_to_char(ch, "Cast what where?\r\n");
    return;
  }
  s = strtok(NULL, "'");
  if (s == NULL) {
    send_to_char(ch, "Spell names must be enclosed in the Holy Magic Symbols: '\r\n");
    return;
  }
  t = strtok(NULL, "\0");

  skip_spaces(&s);

  /* spellnum = search_block(s, spells, 0); */
  spellnum = find_skill_num(s);

  if ((spellnum < 1) || (spellnum > MAX_SPELLS) || !*s) {
    send_to_char(ch, "Cast what?!?\r\n");
    return;
  }

  if (IS_AFFECTED(ch, AFF_TFORM) ||
          IS_AFFECTED(ch, AFF_BATTLETIDE)) {
    send_to_char(ch, "Cast?  Why when you can SMASH!!\r\n");
    return;
  }

  if (!IS_CASTER(ch)) {
    send_to_char(ch, "You are not even a caster!\r\n");
    return;
  }

  if (CLASS_LEVEL(ch, CLASS_WIZARD) < SINFO.min_level[CLASS_WIZARD] &&
          CLASS_LEVEL(ch, CLASS_CLERIC) < SINFO.min_level[CLASS_CLERIC] &&
          CLASS_LEVEL(ch, CLASS_DRUID) < SINFO.min_level[CLASS_DRUID] &&
          CLASS_LEVEL(ch, CLASS_RANGER) < SINFO.min_level[CLASS_RANGER] &&
          CLASS_LEVEL(ch, CLASS_PALADIN) < SINFO.min_level[CLASS_PALADIN] &&
          CLASS_LEVEL(ch, CLASS_BARD) < SINFO.min_level[CLASS_BARD] &&
          CLASS_LEVEL(ch, CLASS_SORCERER) < SINFO.min_level[CLASS_SORCERER]
          ) {
    send_to_char(ch, "You do not know that spell!\r\n");
    return;
  }

  if (GET_SKILL(ch, spellnum) == 0) {
    send_to_char(ch, "You are unfamiliar with that spell.\r\n");
    return;
  }

  if (!hasSpell(ch, spellnum) && !isEpicSpell(spellnum)) {
    //       && spellnum != SPELL_ACID_SPLASH && spellnum != SPELL_RAY_OF_FROST) {
    send_to_char(ch, "You aren't ready to cast that spell... (help preparation)\r\n");
    return;
  }

  /* further restrictions, this needs fixing! -zusuk */
  if (CLASS_LEVEL(ch, CLASS_WIZARD) && GET_INT(ch) < 10) {
    send_to_char(ch, "You are not smart enough to cast spells...\r\n");
    return;
  }
  if (CLASS_LEVEL(ch, CLASS_SORCERER) && GET_CHA(ch) < 10) {
    send_to_char(ch, "You are not charismatic enough to cast spells...\r\n");
    return;
  }
  if (CLASS_LEVEL(ch, CLASS_BARD) && GET_CHA(ch) < 10) {
    send_to_char(ch, "You are not charismatic enough to cast spells...\r\n");
    return;
  }
  if (CLASS_LEVEL(ch, CLASS_CLERIC) && GET_WIS(ch) < 10) {
    send_to_char(ch, "You are not wise enough to cast spells...\r\n");
    return;
  }
  if (CLASS_LEVEL(ch, CLASS_RANGER) && GET_WIS(ch) < 10) {
    send_to_char(ch, "You are not wise enough to cast spells...\r\n");
    return;
  }
  if (CLASS_LEVEL(ch, CLASS_PALADIN) && GET_WIS(ch) < 10) {
    send_to_char(ch, "You are not wise enough to cast spells...\r\n");
    return;
  }
  if (CLASS_LEVEL(ch, CLASS_DRUID) && GET_WIS(ch) < 10) {
    send_to_char(ch, "You are not wise enough to cast spells...\r\n");
    return;
  }

  //epic spell cooldown
  if (char_has_mud_event(ch, eMUMMYDUST) && spellnum == SPELL_MUMMY_DUST) {
    send_to_char(ch, "You must wait longer before you can use this spell again.\r\n");
    send_to_char(ch, "OOC:  The cooldown is approximately 9 minutes.\r\n");
    return;
  }
  if (char_has_mud_event(ch, eDRAGONKNIGHT) && spellnum == SPELL_DRAGON_KNIGHT) {
    send_to_char(ch, "You must wait longer before you can use this spell again.\r\n");
    send_to_char(ch, "OOC:  The cooldown is approximately 9 minutes.\r\n");
    return;
  }
  if (char_has_mud_event(ch, eGREATERRUIN) && spellnum == SPELL_GREATER_RUIN) {
    send_to_char(ch, "You must wait longer before you can use this spell again.\r\n");
    send_to_char(ch, "OOC:  The cooldown is approximately 9 minutes.\r\n");
    return;
  }
  if (char_has_mud_event(ch, eHELLBALL) && spellnum == SPELL_HELLBALL) {
    send_to_char(ch, "You must wait longer before you can use this spell again.\r\n");
    send_to_char(ch, "OOC:  The cooldown is approximately 9 minutes.\r\n");
    return;
  }
  if (char_has_mud_event(ch, eEPICMAGEARMOR) && spellnum == SPELL_EPIC_MAGE_ARMOR) {
    send_to_char(ch, "You must wait longer before you can use this spell again.\r\n");
    send_to_char(ch, "OOC:  The cooldown is approximately 9 minutes.\r\n");
    return;
  }
  if (char_has_mud_event(ch, eEPICWARDING) && spellnum == SPELL_EPIC_WARDING) {
    send_to_char(ch, "You must wait longer before you can use this spell again.\r\n");
    send_to_char(ch, "OOC:  The cooldown is approximately 9 minutes.\r\n");
    return;
  }

  /* Find the target */
  if (t != NULL) {
    char arg[MAX_INPUT_LENGTH];

    strlcpy(arg, t, sizeof (arg));
    one_argument(arg, t);
    skip_spaces(&t);

    /* Copy target to global cast_arg2, for use in spells like locate object */
    strcpy(cast_arg2, t);
  }
  
  if (IS_SET(SINFO.targets, TAR_IGNORE)) {
    target = TRUE;
  } else if (t != NULL && *t) {
    number = get_number(&t);
    if (!target && (IS_SET(SINFO.targets, TAR_CHAR_ROOM))) {
      if ((tch = get_char_vis(ch, t, &number, FIND_CHAR_ROOM)) != NULL)
        target = TRUE;
    }
    if (!target && IS_SET(SINFO.targets, TAR_CHAR_WORLD))
      if ((tch = get_char_vis(ch, t, &number, FIND_CHAR_WORLD)) != NULL)
        target = TRUE;

    if (!target && IS_SET(SINFO.targets, TAR_OBJ_INV))
      if ((tobj = get_obj_in_list_vis(ch, t, &number, ch->carrying)) != NULL)
        target = TRUE;

    if (!target && IS_SET(SINFO.targets, TAR_OBJ_EQUIP)) {
      for (i = 0; !target && i < NUM_WEARS; i++)
        if (GET_EQ(ch, i) && isname(t, GET_EQ(ch, i)->name)) {
          tobj = GET_EQ(ch, i);
          target = TRUE;
        }
    }
    if (!target && IS_SET(SINFO.targets, TAR_OBJ_ROOM))
      if ((tobj = get_obj_in_list_vis(ch, t, &number, world[IN_ROOM(ch)].contents)) != NULL)
        target = TRUE;

    if (!target && IS_SET(SINFO.targets, TAR_OBJ_WORLD))
      if ((tobj = get_obj_vis(ch, t, &number)) != NULL)
        target = TRUE;

  } else { /* if target string is empty */
    if (!target && IS_SET(SINFO.targets, TAR_FIGHT_SELF))
      if (FIGHTING(ch) != NULL) {
        tch = ch;
        target = TRUE;
      }
    if (!target && IS_SET(SINFO.targets, TAR_FIGHT_VICT))
      if (FIGHTING(ch) != NULL) {
        tch = FIGHTING(ch);
        target = TRUE;
      }
    /* if no target specified, and the spell isn't violent, default to self */
    if (!target && IS_SET(SINFO.targets, TAR_CHAR_ROOM) &&
            !SINFO.violent) {
      tch = ch;
      target = TRUE;
    }
    if (!target) {
      send_to_char(ch, "Upon %s should the spell be cast?\r\n",
              IS_SET(SINFO.targets, TAR_OBJ_ROOM | TAR_OBJ_INV | TAR_OBJ_WORLD | TAR_OBJ_EQUIP) ? "what" : "who");
      return;
    }
  }

  if (target && (tch == ch) && SINFO.violent && (spellnum != SPELL_DISPEL_MAGIC)) {
    send_to_char(ch, "You shouldn't cast that on yourself -- could be bad for your health!\r\n");
    return;
  }

  if (!target) {
    send_to_char(ch, "Cannot find the target of your spell!\r\n");
    return;
  }

  //maybe use this as a way to keep npc's in check
  //  mana = mag_manacost(ch, spellnum);
  //  if ((mana > 0) && (GET_MANA(ch) < mana) && (GET_LEVEL(ch) < LVL_IMMORT)) {
  //    send_to_char(ch, "You haven't the energy to cast that spell!\r\n");
  //    return;
  //  }

  if (ROOM_AFFECTED(ch->in_room, RAFF_ANTI_MAGIC)) {
    send_to_char(ch, "Your magic fizzles out and dies!\r\n");
    act("$n's magic fizzles out and dies...", FALSE, ch, 0, 0, TO_ROOM);
    return;
  }

  if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_NOMAGIC)) {
    send_to_char(ch, "Your magic fizzles out and dies.\r\n");
    act("$n's magic fizzles out and dies.", FALSE, ch, 0, 0, TO_ROOM);
    return;
  }

  if (ROOM_FLAGGED(IN_ROOM(ch), ROOM_PEACEFUL) &&
          (SINFO.violent || IS_SET(SINFO.routines, MAG_DAMAGE))) {
    send_to_char(ch, "A flash of white light fills the room, dispelling your violent magic!\r\n");
    act("White light from no particular source suddenly fills the room, then vanishes.", FALSE, ch, 0, 0, TO_ROOM);
    return;
  }

  if (cast_spell(ch, tch, tobj, spellnum))
    SET_WAIT(ch, PULSE_VIOLENCE);
}

void spell_level(int spell, int chclass, int level) {
  int bad = 0;

  if (spell < 0 || spell > TOP_SPELL_DEFINE) {
    log("SYSERR: attempting assign to illegal spellnum %d/%d", spell, TOP_SPELL_DEFINE);
    return;
  }

  if (chclass < 0 || chclass >= NUM_CLASSES) {
    log("SYSERR: assigning '%s' to illegal class %d/%d.", skill_name(spell),
            chclass, NUM_CLASSES - 1);
    bad = 1;
  }

  if (level < 1 || level > LVL_IMPL) {
    log("SYSERR: assigning '%s' to illegal level %d/%d.", skill_name(spell),
            level, LVL_IMPL);
    bad = 1;
  }

  if (!bad)
    spell_info[spell].min_level[chclass] = level;
}

/* Assign the spells on boot up */
static void spello(int spl, const char *name, int max_mana, int min_mana,
        int mana_change, int minpos, int targets, int violent,
        int routines, const char *wearoff, int time, int memtime, int school) {
  int i;

  for (i = 0; i < NUM_CLASSES; i++)
    spell_info[spl].min_level[i] = LVL_IMMORT;
  spell_info[spl].mana_max = max_mana;
  spell_info[spl].mana_min = min_mana;
  spell_info[spl].mana_change = mana_change;
  spell_info[spl].min_position = minpos;
  spell_info[spl].targets = targets;
  spell_info[spl].violent = violent;
  spell_info[spl].routines = routines;
  spell_info[spl].name = name;
  spell_info[spl].wear_off_msg = wearoff;
  spell_info[spl].time = time;
  spell_info[spl].memtime = memtime;
  spell_info[spl].schoolOfMagic = school;
}

/* initializing the spells as unknown for missing info */
void unused_spell(int spl) {
  int i;

  for (i = 0; i < NUM_CLASSES; i++)
    spell_info[spl].min_level[i] = LVL_IMPL + 1;
  spell_info[spl].mana_max = 0;
  spell_info[spl].mana_min = 0;
  spell_info[spl].mana_change = 0;
  spell_info[spl].min_position = POS_DEAD;
  spell_info[spl].targets = 0;
  spell_info[spl].violent = 0;
  spell_info[spl].routines = 0;
  spell_info[spl].name = unused_spellname;
  spell_info[spl].wear_off_msg = unused_wearoff;
  spell_info[spl].time = 0;
  spell_info[spl].memtime = 0;
  spell_info[spl].schoolOfMagic = NOSCHOOL; // noschool
}


#define skillo(skill, name, category)  spello(skill, name, 0, 0, 0, 0, 0, 0, \
                                       0, NULL, 0, 0, category);


/* Arguments for spello calls:
 * spellnum, maxmana, minmana, manachng, minpos, targets, violent?, routines.
 * spellnum:  Number of the spell.  Usually the symbolic name as defined in
 *  spells.h (such as SPELL_HEAL).
 * spellname: The name of the spell.
 * maxmana :  The maximum mana this spell will take (i.e., the mana it
 *  will take when the player first gets the spell).
 * minmana :  The minimum mana this spell will take, no matter how high
 *  level the caster is.
 * manachng:  The change in mana for the spell from level to level.  This
 *  number should be positive, but represents the reduction in mana cost as
 *  the caster's level increases.
 * minpos  :  Minimum position the caster must be in for the spell to work
 *  (usually fighting or standing). targets :  A "list" of the valid targets
 *  for the spell, joined with bitwise OR ('|').
 * violent :  TRUE or FALSE, depending on if this is considered a violent
 *  spell and should not be cast in PEACEFUL rooms or on yourself.  Should be
 *  set on any spell that inflicts damage, is considered aggressive (i.e.
 *  charm, curse), or is otherwise nasty.
 * routines:  A list of magic routines which are associated with this spell
 *  if the spell uses spell templates.  Also joined with bitwise OR ('|').
 * time:  casting time of the spell 
 * memtime:  memtime of the spell 
 * schoolOfMagic:  if magical spell, which school does it belong?
 * Note about - schoolOfMagic:  for skills this is used to categorize it
 * 
 * See the documentation for a more detailed description of these fields. You
 * only need a spello() call to define a new spell; to decide who gets to use
 * a spell or skill, look in class.c.  -JE */

/* please leave these here for my usage -zusuk */
/* evocation */
/* conjuration */
/* necromancy */
/* enchantment */
/* illusion */
/* divination */
/* abjuration */
/* transmutation */

void mag_assign_spells(void) {
  int i;

  /* Do not change the loop below. */
  for (i = 0; i <= TOP_SPELL_DEFINE; i++)
    unused_spell(i);
  /* Do not change the loop above. */

  // sorted the spells by shared / magical / divine, and by circle
  // in each category (school) -zusuk

  //shared
  spello(SPELL_INFRAVISION, "infravision", 0, 0, 0, POS_FIGHTING, //enchant
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your night vision seems to fade.", 4, 8,
          ENCHANTMENT); // wizard 4, cleric 4
  spello(SPELL_DETECT_POISON, "detect poison", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE, MAG_MANUAL,
          "The detect poison wears off.", 4, 11,
          DIVINATION); // wizard 7, cleric 2
  spello(SPELL_POISON, "poison", 0, 0, 0, POS_FIGHTING, //enchantment
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_OBJ_INV, TRUE,
          MAG_AFFECTS | MAG_ALTER_OBJS,
          "You feel less sick.", 5, 8,
          ENCHANTMENT); // wizard 4, cleric 5
  spello(SPELL_ENERGY_DRAIN, "energy drain", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_MANUAL,
          NULL, 9, 14,
          NECROMANCY); // wizard 9, cleric 9
  spello(SPELL_REMOVE_CURSE, "remove curse", 0, 0, 0, POS_FIGHTING, //abjur
          TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_EQUIP, FALSE,
          MAG_UNAFFECTS | MAG_ALTER_OBJS,
          NULL, 4, 8, ABJURATION); // wizard 4, cleric 4
  spello(SPELL_ENDURANCE, "endurance", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your magical endurance has faded away.", 2, 6,
          TRANSMUTATION); // wizard 1, cleric 1
  spello(SPELL_RESIST_ENERGY, "resist energy", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your energy resistance dissipates.", 2, 6,
          ABJURATION); // wizard 1, cleric 1
  spello(SPELL_CUNNING, "cunning", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your magical cunning has faded away.", 3, 7,
          TRANSMUTATION); // wizard 2, cleric 3
  spello(SPELL_WISDOM, "wisdom", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your magical wisdom has faded away.", 3, 7,
          TRANSMUTATION); // wizard 2, cleric 2
  spello(SPELL_CHARISMA, "charisma", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your magical charisma has faded away.", 3, 7,
          TRANSMUTATION); // wizard 2, cleric 2  
  spello(SPELL_CONTROL_WEATHER, "control weather", 72, 57, 1, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_MANUAL,
          NULL, 14, 11, CONJURATION); // wiz 7, cleric x
  spello(SPELL_NEGATIVE_ENERGY_RAY, "negative energy ray", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 1, 5, NECROMANCY); // wiz 1, cleric 1
  spello(SPELL_ENDURE_ELEMENTS, "endure elements", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your element protection wear off.", 2, 5, ABJURATION); //wiz1 cle1
  spello(SPELL_PROT_FROM_EVIL, "protection from evil", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less protected from evil.", 5, 5, ABJURATION); //wiz1 cle1
  spello(SPELL_PROT_FROM_GOOD, "protection from good", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less protected from good.", 5, 5, ABJURATION); //wiz1 cle1
  spello(SPELL_SUMMON_CREATURE_1, "summon creature i", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 5, 5, CONJURATION); //wiz1, cle1
  spello(SPELL_STRENGTH, "strength", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel weaker.", 2, 6, TRANSMUTATION); //wiz2, cle1
  spello(SPELL_GRACE, "grace", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less dextrous.", 2, 6, TRANSMUTATION); //wiz2, cle1
  spello(SPELL_SCARE, "scare", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You no longer feel scared.", 1, 5, ILLUSION); //wiz1, cle2
  spello(SPELL_SUMMON_CREATURE_2, "summon creature ii", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 6, 6, CONJURATION); //wiz2, cle2
  spello(SPELL_DETECT_MAGIC, "detect magic", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "The detect magic wears off.", 1, 6, DIVINATION); //wiz2, cle2
  spello(SPELL_DARKNESS, "darkness", 0, 0, 0, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "The cloak of darkness in the area dissolves.", 5, 6, DIVINATION); //wiz2, cle2
  spello(SPELL_SUMMON_CREATURE_3, "summon creature iii", 95, 80, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 7, 7, CONJURATION); //wiz3, cle3
  spello(SPELL_DEAFNESS, "deafness", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel like you can hear again.", 3, 6,
          NECROMANCY); //wiz2, cle3
  spello(SPELL_DISPEL_MAGIC, "dispel magic", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_MANUAL,
          NULL, 4, 7, DIVINATION); //wiz3, cle3
  spello(SPELL_ANIMATE_DEAD, "animate dead", 72, 57, 1, POS_FIGHTING,
          TAR_OBJ_ROOM, FALSE, MAG_SUMMONS,
          NULL, 10, 8, NECROMANCY); //wiz4, cle3
  spello(SPELL_SUMMON_CREATURE_4, "summon creature iv", 95, 80, 1,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 8, 8, CONJURATION); //wiz4, cle4
  spello(SPELL_BLINDNESS, "blindness", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel a cloak of blindness dissolve.", 3, 6,
          NECROMANCY); // wiz2, cle3
  spello(SPELL_CIRCLE_A_EVIL, "circle against evil", 58, 43, 1,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 7, 7, ABJURATION); //wiz3 cle4
  spello(SPELL_CIRCLE_A_GOOD, "circle against good", 58, 43, 1,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 7, 7, ABJURATION); //wiz3 cle4
  spello(SPELL_CURSE, "curse", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT | TAR_OBJ_INV, TRUE, MAG_AFFECTS |
          MAG_ALTER_OBJS, "You feel more optimistic.", 7, 8, NECROMANCY); //wiz4 cle4
  spello(SPELL_DAYLIGHT, "daylight", 50, 25, 5, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "The artificial daylight fades away.", 6, 7, ILLUSION); //wiz3, cle4
  spello(SPELL_SUMMON_CREATURE_6, "summon creature vi", 0, 0, 0,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 9, 10, CONJURATION); //wiz6 cle6
  spello(SPELL_EYEBITE, "eyebite", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS |
          MAG_ALTER_OBJS, "You feel the disease fade away.", 6, 10, NECROMANCY); //wiz6 cle6
  spello(SPELL_MASS_WISDOM, "mass wisdom", 0, 0, 0, POS_FIGHTING, TAR_IGNORE, //wiz7, cle6
          FALSE, MAG_GROUPS, "The wisdom spell fades away.", 5, 11, TRANSMUTATION); //wiz7, cle6
  spello(SPELL_MASS_CHARISMA, "mass charisma", 0, 0, 0, POS_FIGHTING, TAR_IGNORE, //wiz7, cle6
          FALSE, MAG_GROUPS, "The charisma spell fades away.", 5, 11, TRANSMUTATION);
  spello(SPELL_MASS_CUNNING, "mass cunning", 0, 0, 0, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_GROUPS, "The cunning spell fades away.", 5, 11, TRANSMUTATION);
  spello(SPELL_MASS_STRENGTH, "mass strength", 0, 0, 0, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_GROUPS, "You feel weaker.", 5, 11, TRANSMUTATION);
  spello(SPELL_MASS_GRACE, "mass grace", 0, 0, 0, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_GROUPS, "You feel less dextrous.", 5, 11, TRANSMUTATION);
  spello(SPELL_MASS_ENDURANCE, "mass endurance", 0, 0, 0, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_GROUPS, "Your magical endurance has faded away.", 5, 11, TRANSMUTATION);
  /**  end shared list **/


  //shared epic
  spello(SPELL_DRAGON_KNIGHT, "dragon knight", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 12, 1,
          NOSCHOOL);
  spello(SPELL_GREATER_RUIN, "greater ruin", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 6, 1,
          NOSCHOOL);
  spello(SPELL_HELLBALL, "hellball", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 6, 1,
          NOSCHOOL);
  spello(SPELL_MUMMY_DUST, "mummy dust", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 14, 1,
          NOSCHOOL);


  // paladin
  /* = =  4th circle  = = */
  spello(SPELL_HOLY_SWORD, "holy sword", 37, 22, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_CREATIONS,
          NULL, 2, 7, NOSCHOOL);

  // magical

  /* = =  cantrips  = = */
  /* evocation */
  spello(SPELL_ACID_SPLASH, "acid splash", 0, 0, 0, POS_SITTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 0, 1, EVOCATION);
  spello(SPELL_RAY_OF_FROST, "ray of frost", 0, 0, 0, POS_SITTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 0, 1, EVOCATION);

  /* = =  1st circle  = = */
  /* evocation */
  spello(SPELL_MAGIC_MISSILE, "magic missile", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 0, 5, EVOCATION);
  spello(SPELL_HORIZIKAULS_BOOM, "horizikauls boom", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_AFFECTS,
          NULL, 1, 5, EVOCATION);
  spello(SPELL_BURNING_HANDS, "burning hands", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 1, 5, EVOCATION);
  spello(SPELL_FAERIE_FIRE, "faerie fire", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          NULL, 1, 5, EVOCATION);
  spello(SPELL_PRODUCE_FLAME, "produce flame", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 3, 5, EVOCATION);
  /* conjuration */
  spello(SPELL_ICE_DAGGER, "ice dagger", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 1, 5, CONJURATION);
  spello(SPELL_MAGE_ARMOR, "mage armor", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "You feel less protected.", 4, 5,
          CONJURATION);
  spello(SPELL_OBSCURING_MIST, "obscuring mist", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "The obscuring mist begins to dissipate.", 3, 5, CONJURATION);
  spello(SPELL_SUMMON_NATURES_ALLY_1, "natures ally i", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 4, 5, CONJURATION);
  // summon creature 1 - shared
  /* necromancy */
  spello(SPELL_CHILL_TOUCH, "chill touch", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_AFFECTS,
          "You feel your strength return.", 1, 5, NECROMANCY);
  spello(SPELL_RAY_OF_ENFEEBLEMENT, "ray of enfeeblement", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel your strength return.", 1, 5, NECROMANCY);
  // negative energy ray - shared
  /* enchantment */
  spello(SPELL_CHARM_ANIMAL, "charm animal", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF, TRUE, MAG_MANUAL,
          "You feel more self-confident.", 4, 5, ENCHANTMENT);
  spello(SPELL_CHARM, "charm person", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF, TRUE, MAG_MANUAL,
          "You feel more self-confident.", 4, 5, ENCHANTMENT);
  spello(SPELL_ENCHANT_WEAPON, "enchant weapon", 0, 0, 0, POS_FIGHTING,
          TAR_OBJ_INV, FALSE, MAG_MANUAL,
          NULL, 5, 5, ENCHANTMENT);
  spello(SPELL_SLEEP, "sleep", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel less tired.", 4, 5, ENCHANTMENT);
  /* illusion */
  spello(SPELL_COLOR_SPRAY, "color spray", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_AFFECTS,
          NULL, 1, 5, ILLUSION);
  //scare - shared
  spello(SPELL_TRUE_STRIKE, "true strike", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel you are no longer able to strike true!", 0, 5, ILLUSION);
  /* divination */
  spello(SPELL_IDENTIFY, "identify", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE, MAG_MANUAL,
          NULL, 5, 5, DIVINATION);
  spello(SPELL_SHELGARNS_BLADE, "shelgarns blade", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 6, 5, DIVINATION);
  spello(SPELL_GREASE, "grease", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel the grease spell wear off.", 1, 5, DIVINATION);
  /* abjuration */
  // endure elements - shared
  // protect from evil - shared
  // protect from good - shared
  /* transmutation */
  spello(SPELL_EXPEDITIOUS_RETREAT, "expeditious retreat", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less expeditious.", 0, 5,
          TRANSMUTATION);
  spello(SPELL_GOODBERRY, "goodberry", 0, 0, 0, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_CREATIONS,
          NULL, 3, 5, TRANSMUTATION);
  spello(SPELL_IRON_GUTS, "iron guts", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your guts feel less resillient.", 3, 5, TRANSMUTATION);
  spello(SPELL_JUMP, "jump", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your jumping ability return to normal.", 3, 5, TRANSMUTATION);
  spello(SPELL_MAGIC_FANG, "magic fang", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your magic fang wears off.", 3, 5, TRANSMUTATION);
  spello(SPELL_MAGIC_STONE, "magic stone", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_CREATIONS,
          NULL, 3, 5, TRANSMUTATION);
  spello(SPELL_SHIELD, "shield", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your magical shield fades away.", 3, 5, TRANSMUTATION);


  /* = =  2nd circle  = = */
  /* evocation */
  spello(SPELL_ACID_ARROW, "acid arrow", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_MANUAL,
          NULL, 2, 6, EVOCATION);
  spello(SPELL_SHOCKING_GRASP, "shocking grasp", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 2, 6, EVOCATION);
  spello(SPELL_SCORCHING_RAY, "scorching ray", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 2, 6, EVOCATION);
  spello(SPELL_CONTINUAL_FLAME, "continual flame", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_CREATIONS,
          NULL, 5, 6, EVOCATION);
  spello(SPELL_FLAME_BLADE, "flame blade", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 2, 6, EVOCATION);
  spello(SPELL_FLAMING_SPHERE, "flaming sphere", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 5, 6, EVOCATION);
  /* conjuration */
  //summon creature 2 - shared
  spello(SPELL_SUMMON_NATURES_ALLY_2, "natures ally ii", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 4, 7, CONJURATION);
  spello(SPELL_SUMMON_SWARM, "summon swarm", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 5, 8, CONJURATION);
  spello(SPELL_WEB, "web", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF, TRUE, MAG_AFFECTS,
          "You feel the sticky strands of the magical web dissolve.", 2, 6,
          CONJURATION);
  /* necromancy */
  //blindness - shared
  spello(SPELL_FALSE_LIFE, "false life", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your necromantic-life drain away.", 4, 6, ILLUSION);
  /* enchantment */
  spello(SPELL_DAZE_MONSTER, "daze monster", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You no longer feel dazed.", 2, 6,
          ENCHANTMENT);
  spello(SPELL_HIDEOUS_LAUGHTER, "hideous laughter", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel able to control your laughter again.", 2, 6,
          ENCHANTMENT);
  spello(SPELL_HOLD_ANIMAL, "hold animal", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel able to control yourself again.", 3, 7,
          ENCHANTMENT);
  spello(SPELL_TOUCH_OF_IDIOCY, "touch of idiocy", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You begin to feel less incompetent.", 2, 6,
          ENCHANTMENT);
  /* illusion */
  spello(SPELL_BLUR, "blur", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your blur spell wear off.", 2, 6, ILLUSION);
  spello(SPELL_INVISIBLE, "invisibility", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE, MAG_AFFECTS | MAG_ALTER_OBJS,
          "You feel yourself exposed.", 4, 6, ILLUSION);
  spello(SPELL_MIRROR_IMAGE, "mirror image", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You watch as your images vanish.", 3, 6, ILLUSION);
  /* divination */
  spello(SPELL_DETECT_INVIS, "detect invisibility", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your eyes stop tingling.", 1, 6, DIVINATION);
  //detect magic - shared
  //darkness - shared
  /* abjuration */
  //resist energy
  spello(SPELL_ENERGY_SPHERE, "energy sphere", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 2, 6, ABJURATION);
  /* transmutation */
  spello(SPELL_BARKSKIN, "barkskin", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your barkskin wear off.", 3, 6, TRANSMUTATION);
  //endurance - shared
  //strengrth - shared
  //grace - shared


  // 3rd cricle
  /* evocation */
  spello(SPELL_LIGHTNING_BOLT, "lightning bolt", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 3, 7, EVOCATION);
  spello(SPELL_FIREBALL, "fireball", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 3, 7, EVOCATION);
  spello(SPELL_WATER_BREATHE, "water breathe", 79, 64, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your magical gears fade away.", 7, 7, EVOCATION);
  /* conjuration */
  //summon creature 3 - shared
  spello(SPELL_PHANTOM_STEED, "phantom steed", 95, 80, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 7, 7, CONJURATION);
  spello(SPELL_STINKING_CLOUD, "stinking cloud", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "You watch as the noxious gasses fade away.", 4, 7,
          CONJURATION);
  /* necromancy */
  spello(SPELL_BLIGHT, "blight", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 4, 7, NECROMANCY);
  spello(SPELL_CONTAGION, "contagion", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel much better as your disease wears off.", 5, 7, NECROMANCY);
  spello(SPELL_HALT_UNDEAD, "halt undead", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          "You feel the necromantic halt spell fade away.", 5, 7,
          NECROMANCY);
  spello(SPELL_VAMPIRIC_TOUCH, "vampiric touch", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_POINTS,
          NULL, 3, 7, NECROMANCY);
  spello(SPELL_HEROISM, "deathly heroism", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your deathly heroism fades away.", 4, 7,
          NECROMANCY);
  /* enchantment */
  spello(SPELL_FLY, "fly", 37, 22, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You drift slowly to the ground.", 3, 7, ENCHANTMENT);
  spello(SPELL_HOLD_PERSON, "hold person", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel able to control your laughter again.", 3, 7,
          ENCHANTMENT);
  spello(SPELL_DEEP_SLUMBER, "deep slumber", 58, 43, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel less tired.", 4, 7, ENCHANTMENT);
  /* illusion */
  spello(SPELL_WALL_OF_FOG, "wall of fog", 50, 25, 5, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "The wall of fog blows away.", 6, 7, ILLUSION);
  spello(SPELL_INVISIBILITY_SPHERE, "invisibility sphere", 58, 43, 1,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 7, 7, ILLUSION);
  //daylight - shared
  /* divination */
  spello(SPELL_CLAIRVOYANCE, "clairvoyance", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_WORLD | TAR_NOT_SELF, FALSE, MAG_MANUAL,
          NULL, 5, 7,
          DIVINATION);
  spello(SPELL_NON_DETECTION, "nondetection", 37, 22, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your non-detection spell wore off.", 6, 7, DIVINATION);
  //dispel magic - shared
  /* abjuration */
  spello(SPELL_HASTE, "haste", 37, 22, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your haste spell wear off.", 4, 7, ABJURATION);
  spello(SPELL_SLOW, "slow", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel the slow spell wear off.", 4, 7,
          ABJURATION);
  //circle against evil - shared
  //circle against good - shared
  /* transmutation */
  spello(SPELL_GREATER_MAGIC_FANG, "greater magic fang", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your greater magic fang wears off.", 4, 7, TRANSMUTATION);
  spello(SPELL_SPIKE_GROWTH, "spike growth", 0, 0, 0, POS_STANDING,
        TAR_IGNORE, FALSE, MAG_ROOM,
        "The large spikes retract back into the earth.", 5, 8, TRANSMUTATION);
  //cunning - shared
  //wisdom - shared
  //charisma - shared

  // 4th circle
  /* evocation */
  spello(SPELL_ICE_STORM, "ice storm", 58, 43, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 5, 8, EVOCATION);
  spello(SPELL_FIRE_SHIELD, "fire shield", 37, 22, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "You watch your fire shield fade away.", 5, 8, EVOCATION);
  spello(SPELL_COLD_SHIELD, "cold shield", 37, 22, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "You watch your cold shield fade away.", 5, 8, EVOCATION);
  /* conjuration */
  spello(SPELL_BILLOWING_CLOUD, "billowing cloud", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "You watch as the thick billowing cloud dissipates.", 7, 8,
          CONJURATION);
  //summon creature 4 - shared
  /* necromancy */
  //curse - shared
  /* enchantment */
  //infra - shared
  //poison - shared
  /* illusion */
  spello(SPELL_GREATER_INVIS, "greater invisibility", 58, 43, 1,
          POS_FIGHTING, TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE,
          MAG_AFFECTS | MAG_ALTER_OBJS, "You feel yourself exposed.", 8, 8,
          ILLUSION);
  spello(SPELL_RAINBOW_PATTERN, "rainbow pattern", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You no longer feel dazed.", 5, 8,
          ILLUSION);
  /* divination */
  spello(SPELL_WIZARD_EYE, "wizard eye", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL,
          NULL, 6, 8, DIVINATION);
  spello(SPELL_LOCATE_CREATURE, "locate creature", 58, 43, 1, POS_FIGHTING,
          TAR_CHAR_WORLD, FALSE, MAG_MANUAL,
          NULL, 12, 8, DIVINATION);
  /* abjuration */
  spello(SPELL_FREE_MOVEMENT, "freedom of movement(inc)", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You are no longer able to move freely.", 3, 8, ABJURATION);
  spello(SPELL_STONESKIN, "stone skin", 51, 36, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your skin returns to its normal texture.", 3, 8, ABJURATION);
  spello(SPELL_MINOR_GLOBE, "minor globe", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "Your minor globe has faded away.", 8,
          8, ABJURATION);
  //remove curse
  /* transmutation */
  spello(SPELL_ENLARGE_PERSON, "enlarge person", 37, 22, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your enlargement spell wear off.", 8, 8, TRANSMUTATION);
  spello(SPELL_SHRINK_PERSON, "shrink person", 37, 22, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your shrink spell wear off.", 8, 8, TRANSMUTATION);
  spello(SPELL_SPIKE_STONES, "spike stone", 0, 0, 0, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "The large spike stones morph back into their natural form.", 8, 8, TRANSMUTATION);

  // 5th circle
  /* evocation */
  spello(SPELL_BALL_OF_LIGHTNING, "ball of lightning", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL, 5, 9, EVOCATION);
  spello(SPELL_CALL_LIGHTNING_STORM, "call lightning storm", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, NULL, 8, 9, EVOCATION);
  spello(SPELL_HALLOW, "hallow", 0, 0, 0, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_ROOM, NULL, 8, 9, EVOCATION);
  spello(SPELL_INTERPOSING_HAND, "interposing hand", 80, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel more optimistic.", 7, 9, EVOCATION);
  spello(SPELL_UNHALLOW, "unhallow", 0, 0, 0, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_ROOM, NULL, 8, 9, EVOCATION);
  spello(SPELL_WALL_OF_FIRE, "wall of fire", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_CREATIONS, NULL, 7, 9, EVOCATION);
  spello(SPELL_WALL_OF_FORCE, "wall of force", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL, NULL, 6, 9, EVOCATION);
  /* conjuration */
  spello(SPELL_CLOUDKILL, "cloudkill", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL, NULL, 8, 9, CONJURATION);
  spello(SPELL_INSECT_PLAGUE, "insect plague", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 9, 9, CONJURATION);
  spello(SPELL_SUMMON_CREATURE_5, "summon creature v", 95, 80, 1,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 9, 9, CONJURATION);
  spello(SPELL_WALL_OF_THORNS, "wall of thorns", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_CREATIONS, NULL, 7, 9, CONJURATION);
  /* necromancy */
  spello(SPELL_DEATH_WARD, "death ward", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "You are no longer warded against the effects of death magic.", 7, 9, NECROMANCY);
  spello(SPELL_SYMBOL_OF_PAIN, "symbol of pain", 58, 43, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, NULL, 8, 9, NECROMANCY);
  spello(SPELL_WAVES_OF_FATIGUE, "waves of fatigue", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, "You feel the magical fatigue fade away.", 7,
          9, NECROMANCY);
  /* enchantment */
  spello(SPELL_DOMINATE_PERSON, "dominate person", 51, 36, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF, TRUE, MAG_MANUAL,
          "You feel the domination effects wear off.", 10, 9, ENCHANTMENT);
  spello(SPELL_FEEBLEMIND, "feeblemind", 80, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "Your enfeebled mind returns to normal.", 7, 9, ENCHANTMENT);
  /* illusion */
  spello(SPELL_NIGHTMARE, "nightmare", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_AFFECTS,
          "You are able to shake the horrid nightmare.", 7, 9, ILLUSION);
  spello(SPELL_MIND_FOG, "mind fog", 80, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "Your fogged mind returns to normal.", 7, 9, ILLUSION);
  /* divination */
  spello(SPELL_FAITHFUL_HOUND, "faithful hound", 95, 80, 1,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 9, 9, DIVINATION);
  spello(SPELL_ACID_SHEATH, "acid sheath", 37, 22, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "You watch your acid sheath fade away.", 6, 9, DIVINATION);
  /* abjuration */
  spello(SPELL_DISMISSAL, "dismissal", 51, 36, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_MANUAL, NULL, 7, 9,
          ABJURATION);
  spello(SPELL_CONE_OF_COLD, "cone of cold", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL, 9, 9, ABJURATION);
  /* transmutation */
  spello(SPELL_TELEKINESIS, "telekinesis", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL, 9, 9,
          TRANSMUTATION);
  spello(SPELL_FIREBRAND, "firebrand", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL, 9, 9,
          TRANSMUTATION);


  // 6th circle
  /* evocation */
  spello(SPELL_FREEZING_SPHERE, "freezing sphere", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL, 5, 10, EVOCATION);
  /* conjuration */
  spello(SPELL_ACID_FOG, "acid fog", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "You watch as the acid fog dissipates.", 7, 8,
          CONJURATION);
  spello(SPELL_FIRE_SEEDS, "fire seeds", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_CREATIONS, NULL, 7, 8, CONJURATION);
  spello(SPELL_TRANSPORT_VIA_PLANTS, "transport via plants", 0, 0, 0, POS_STANDING,
        TAR_OBJ_ROOM, FALSE, MAG_MANUAL, NULL, 8, 10, CONJURATION);
  //summon creature 6 - shared
  /* necromancy */
  spello(SPELL_TRANSFORMATION, "transformation", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "You feel your transformation fade.", 5, 10, NECROMANCY);
  //eyebite - shared
  /* enchantment */
  spello(SPELL_MASS_HASTE, "mass haste", 0, 0, 0, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_GROUPS, "The haste spell fades away.", 8, 10, ENCHANTMENT);
  spello(SPELL_GREATER_HEROISM, "greater heroism", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS, "Your greater heroism fades away.",
          6, 10, ENCHANTMENT);
  /* illusion */
  spello(SPELL_ANTI_MAGIC_FIELD, "anti magic field", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "You watch as the shimmering anti-magic field dissipates.", 7, 10,
          ILLUSION);
  spello(SPELL_GREATER_MIRROR_IMAGE, "greater mirror image", 0, 0, 0,
          POS_FIGHTING, TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "You watch as your images vanish.", 5, 10, ILLUSION);
  /* divination */
  spello(SPELL_LOCATE_OBJECT, "locate object", 0, 0, 0, POS_FIGHTING,
          TAR_OBJ_WORLD, FALSE, MAG_MANUAL,
          NULL, 10, 10, DIVINATION);
  spello(SPELL_TRUE_SEEING, "true seeing", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS, "Your eyes stop seeing true.", 5, 10,
          DIVINATION);
  /* abjuration */
  spello(SPELL_GLOBE_OF_INVULN, "globe of invuln", 0, 0, 0,
          POS_FIGHTING, TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your globe of invulnerability has faded away.", 6, 10, ABJURATION);
  spello(SPELL_GREATER_DISPELLING, "greater dispelling", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_MANUAL, NULL, 4, 7, ABJURATION);
  /* transmutation */
  spello(SPELL_CLONE, "clone", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_SUMMONS,
          NULL, 9, 10, TRANSMUTATION);
  spello(SPELL_SPELLSTAFF, "spellstaff", 0, 0, 0, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_MANUAL, NULL, 9, 10, TRANSMUTATION);
  spello(SPELL_WATERWALK, "waterwalk", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your feet seem less buoyant.", 7, 10, TRANSMUTATION);


  // 7th circle
  /* evocation */
  spello(SPELL_FIRE_STORM, "fire storm", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, NULL, 7, 11, EVOCATION);
  spello(SPELL_GRASPING_HAND, "grasping hand", 72, 57, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_AFFECTS,
          NULL, 6, 11, EVOCATION); //grapples opponent
  spello(SPELL_MISSILE_STORM, "missile storm", 72, 57, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 6, 11, EVOCATION);
  spello(SPELL_SUNBEAM, "sunbeam", 0, 0, 0,
          POS_FIGHTING, TAR_IGNORE, TRUE, MAG_AREAS | MAG_ROOM,
          "You feel a cloak of blindness dissolve.", 6, 11, EVOCATION);
  /* conjuration */
  spello(SPELL_CREEPING_DOOM, "creeping doom", 0, 0, 0, POS_FIGHTING,
        TAR_IGNORE, FALSE, MAG_MANUAL, NULL, 10, 11, CONJURATION);
  spello(SPELL_SUMMON_CREATURE_7, "summon creature vii", 0, 0, 0,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 10, 11, CONJURATION);
  //control weather, enhances some spells (shared)
  /* necromancy */
  spello(SPELL_POWER_WORD_BLIND, "power word blind", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel a cloak of blindness dissolve.", 0, 11,
          NECROMANCY);
  spello(SPELL_WAVES_OF_EXHAUSTION, "waves of exhaustion", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, "You feel the magical exhaustion fade away.", 8,
          11, NECROMANCY); //like waves of fatigue, but no save?
  /* enchantment */
  spello(SPELL_MASS_HOLD_PERSON, "mass hold person", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, "You feel the magical hold fade away.", 8,
          11, ENCHANTMENT);
  spello(SPELL_MASS_FLY, "mass fly", 0, 0, 0, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_GROUPS, "The fly spell fades away.", 7, 11, ENCHANTMENT);
  /* illusion */
  spello(SPELL_DISPLACEMENT, "displacement", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your displacement spell wear off.", 6, 11, ILLUSION);
  spello(SPELL_PRISMATIC_SPRAY, "prismatic spray", 79, 64, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 7, 11, ILLUSION);
  /* divination */
  spello(SPELL_POWER_WORD_STUN, "power word stun", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You no longer feel stunned.", 0, 11,
          DIVINATION);
  spello(SPELL_PROTECT_FROM_SPELLS, "protection from spells", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your spell protection wear off.", 6, 11, DIVINATION);
  //detect poison - shared
  /* abjuration */
  spello(SPELL_THUNDERCLAP, "thunderclap", 79, 64, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 7, 11, ABJURATION); //aoe damage and affect
  spello(SPELL_SPELL_MANTLE, "spell mantle", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your spell mantle wear off.", 6, 11, ABJURATION);
  /* transmutation */
  spello(SPELL_TELEPORT, "teleport", 72, 57, 1, POS_FIGHTING,
          TAR_CHAR_WORLD | TAR_NOT_SELF, FALSE, MAG_MANUAL,
          NULL, 2, 11, TRANSMUTATION);
  //mass wisdom - shared
  //mass charisma - shared
  //mass cunning - shared


  // 8th circle
  /* evocation */
  spello(SPELL_CLENCHED_FIST, "clenched fist", 72, 57, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 7, 12, EVOCATION);
  spello(SPELL_CHAIN_LIGHTNING, "chain lightning", 79, 64, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 8, 12, EVOCATION);
  spello(SPELL_WHIRLWIND, "whirlwind", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, NULL, 8, 12, EVOCATION);
  /* conjuration */
  spello(SPELL_INCENDIARY_CLOUD, "incendiary cloud", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL, NULL, 9, 12, CONJURATION);
  spello(SPELL_SUMMON_CREATURE_8, "summon creature viii", 0, 0, 0,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 11, 12, CONJURATION);
  /* necromancy */
  spello(SPELL_FINGER_OF_DEATH, "finger of death", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 8, 12, NECROMANCY);
  spello(SPELL_GREATER_ANIMATION, "greater animation", 72, 57, 1, POS_FIGHTING,
          TAR_OBJ_ROOM, FALSE, MAG_SUMMONS,
          NULL, 11, 12, NECROMANCY);
  spello(SPELL_HORRID_WILTING, "horrid wilting", 79, 64, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 9, 12, NECROMANCY);
  /* enchantment */
  spello(SPELL_IRRESISTIBLE_DANCE, "irresistible dance", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You no longer feel the urge to moonwalk.", 5, 12,
          ENCHANTMENT);
  spello(SPELL_MASS_DOMINATION, "mass domination", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_MANUAL, "You no longer feel dominated.", 6, 12, ENCHANTMENT);
  /* illusion */
  spello(SPELL_SCINT_PATTERN, "scint pattern", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "The pattern no longer traps you.", 5, 12,
          ILLUSION);
  spello(SPELL_REFUGE, "refuge", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL, NULL, 6, 12, ILLUSION);
  /* divination */
  spello(SPELL_BANISH, "banish", 51, 36, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_MANUAL, NULL, 8, 12,
          DIVINATION);
  spello(SPELL_SUNBURST, "sunburst", 72, 57, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS | MAG_ROOM,
          NULL, 7, 12, DIVINATION);
  /* abjuration */
  spello(SPELL_SPELL_TURNING, "spell turning", 79, 64, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your spell-turning field fades away.", 8, 12, ABJURATION);
  spello(SPELL_MIND_BLANK, "mind blank", 79, 64, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your mind-blank fades.", 8, 12, ABJURATION);
  /* transmutation */
  spello(SPELL_CONTROL_PLANTS, "control plants", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF, TRUE, MAG_MANUAL,
          "You are able to control yourself again.", 7, 12, TRANSMUTATION);
  spello(SPELL_IRONSKIN, "iron skin", 51, 36, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your skin loses its iron-like texture.", 4, 12, TRANSMUTATION);
  spello(SPELL_PORTAL, "portal", 37, 22, 1, POS_FIGHTING, TAR_CHAR_WORLD |
          TAR_NOT_SELF, FALSE, MAG_CREATIONS, NULL, 12, 12, TRANSMUTATION);


  // 9th circle
  /* evocation */
  spello(SPELL_METEOR_SWARM, "meteor swarm", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, NULL, 9, 13, EVOCATION);
  spello(SPELL_BLADE_OF_DISASTER, "blade of disaster", 0, 0, 0,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 14, 13, EVOCATION);
  /* conjuration */
  spello(SPELL_ELEMENTAL_SWARM, "elemental swarm", 0, 0, 0, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_SUMMONS, NULL, 12, 13, CONJURATION);
  spello(SPELL_GATE, "gate", 51, 36, 1, POS_FIGHTING, TAR_IGNORE, FALSE,
          MAG_CREATIONS, NULL, 9, 13, CONJURATION);
  spello(SPELL_SHAMBLER, "shambler", 0, 0, 0, POS_FIGHTING, TAR_IGNORE, FALSE,
          MAG_SUMMONS, NULL, 9, 13, CONJURATION);
  spello(SPELL_SUMMON_CREATURE_9, "summon creature ix", 0, 0, 0,
          POS_FIGHTING, TAR_IGNORE, FALSE, MAG_SUMMONS, NULL, 12, 13, CONJURATION);
  /* necromancy */
  //*energy drain, shared
  spello(SPELL_WAIL_OF_THE_BANSHEE, "wail of the banshee", 85, 70, 1,
          POS_FIGHTING, TAR_IGNORE, TRUE, MAG_AREAS, NULL, 10, 13, NECROMANCY);
  /* enchantment */
  spello(SPELL_POWER_WORD_KILL, "power word kill", 72, 57, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 0, 13, EVOCATION);
  spello(SPELL_ENFEEBLEMENT, "enfeeblement", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, "You no longer feel enfeebled.", 4, 13,
          ENCHANTMENT);
  /* illusion */
  spello(SPELL_WEIRD, "weird", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE | MAG_AFFECTS,
          "The phantasmal killer stop chasing you.", 4, 13,
          ENCHANTMENT);
  spello(SPELL_SHADOW_SHIELD, "shadow shield", 95, 80, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel the shadow shield dissipate.", 5, 13, ILLUSION);
  /* divination */
  spello(SPELL_PRISMATIC_SPHERE, "prismatic sphere", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL, NULL, 8, 13, DIVINATION);
  spello(SPELL_IMPLODE, "implode", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_MANUAL,
          NULL, 3, 13, DIVINATION);
  /* abjuration */
  spello(SPELL_TIMESTOP, "timestop", 95, 80, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "Time begins to move again.", 0, 13, ABJURATION);
  spello(SPELL_GREATER_SPELL_MANTLE, "greater spell mantle", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "You feel your greater spell mantle wear off.", 8, 13, ABJURATION);
  /* transmutation */
  spello(SPELL_POLYMORPH, "polymorph self", 58, 43, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL, NULL, 9, 13, TRANSMUTATION);
  spello(SPELL_MASS_ENHANCE, "mass enhance", 0, 0, 0, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_GROUPS, "The physical enhancement spell wears off.", 2, 13,
          TRANSMUTATION);


  // epic magical
  spello(SPELL_EPIC_MAGE_ARMOR, "epic mage armor", 95, 80, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less protected.", 4, 14, ABJURATION);
  spello(SPELL_EPIC_WARDING, "epic warding", 95, 80, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "Your massive magical ward dissipates.", 4, 14, ABJURATION);
  // end magical



  // divine spells
  // 1st circle
  spello(SPELL_CURE_LIGHT, "cure light", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_POINTS,
          NULL, 1, 6, NOSCHOOL);
  spello(SPELL_CAUSE_LIGHT_WOUNDS, "cause light wound", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE, NULL, 2, 6, NOSCHOOL);
  spello(SPELL_ARMOR, "armor", 30, 15, 1, POS_FIGHTING, TAR_CHAR_ROOM, FALSE,
          MAG_AFFECTS, "You feel less protected.", 4, 6, CONJURATION);
  spello(SPELL_REMOVE_FEAR, "remove fear", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_UNAFFECTS,
          NULL, 3, 6, NOSCHOOL);
  //endurance - shared
  //negative energy ray - shared
  //endure elements - shared
  //protect from evil - shared
  //protect from good - shared
  //summon creature i - shared
  //strength - shared
  //grace - shared


  // 2nd circle
  spello(SPELL_CREATE_FOOD, "create food", 37, 22, 1, POS_FIGHTING, TAR_IGNORE,
          FALSE, MAG_CREATIONS, NULL, 2, 7, NOSCHOOL);
  spello(SPELL_CREATE_WATER, "create water", 37, 22, 1, POS_FIGHTING,
          TAR_OBJ_INV | TAR_OBJ_EQUIP, FALSE, MAG_MANUAL, NULL, 2, 7, NOSCHOOL);
  spello(SPELL_CURE_MODERATE, "cure moderate", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_POINTS, NULL, 2, 7, NOSCHOOL);
  spello(SPELL_CAUSE_MODERATE_WOUNDS, "cause moderate wound", 37, 22, 1,
          POS_FIGHTING, TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 3, 7, NOSCHOOL);
  //detect poison - shared
  //scare - shared
  //summon creature ii - shared
  //detect magic - shared
  //darkness - shared
  //resist energy - shared
  //wisdom - shared
  //charisma - shared

  // 3rd circle
  spello(SPELL_DETECT_ALIGN, "detect alignment", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less aware.", 3, 8, NOSCHOOL);
  spello(SPELL_CURE_BLIND, "cure blind", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_UNAFFECTS,
          NULL, 3, 8, NOSCHOOL);
  spello(SPELL_BLESS, "bless", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_OBJ_INV, FALSE, MAG_AFFECTS | MAG_ALTER_OBJS,
          "You feel less righteous.", 3, 8, NOSCHOOL);
  spello(SPELL_CURE_SERIOUS, "cure serious", 30, 15, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_POINTS, NULL, 3, 8, NOSCHOOL);
  spello(SPELL_CAUSE_SERIOUS_WOUNDS, "cause serious wound", 44, 29, 1,
          POS_FIGHTING, TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 4, 9, NOSCHOOL);
  spello(SPELL_CURE_DEAFNESS, "cure deafness", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_UNAFFECTS,
          NULL, 5, 9, NOSCHOOL);
  spello(SPELL_FAERIE_FOG, "faerie fog", 65, 50, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, NULL, 4, 7, NOSCHOOL);
  //summon creature 3 - shared
  //deafness - shared
  //cunning - shared
  //dispel magic - shared
  //animate dead - shared

  // 4th circle
  spello(SPELL_CURE_CRITIC, "cure critic", 51, 36, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_POINTS,
          NULL, 3, 10, NOSCHOOL);
  spello(SPELL_CAUSE_CRITICAL_WOUNDS, "cause critical wound", 51, 36, 1,
          POS_FIGHTING, TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 5, 10, NOSCHOOL);
  spello(SPELL_MASS_CURE_LIGHT, "mass cure light", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 5, 10, NOSCHOOL);
  spello(SPELL_AID, "aid", 44, 29, 1, POS_FIGHTING, TAR_IGNORE, FALSE,
          MAG_GROUPS, "You feel the aid spell fade away.", 5, 10, NOSCHOOL);
  spello(SPELL_BRAVERY, "bravery", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_UNAFFECTS | MAG_AFFECTS,
          "You feel your bravery spell wear off.", 8, 10, NOSCHOOL);
  //summon creature iv - shared
  //remove curse - shared
  //infravision - shared
  //circle against evil - shared
  //circle against good - shared
  //curse - shared
  //daylight - shared

  // 5th circle
  spello(SPELL_REMOVE_POISON, "remove poison", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE, MAG_UNAFFECTS | MAG_ALTER_OBJS,
          NULL, 7, 12, NOSCHOOL);
  spello(SPELL_PROT_FROM_EVIL, "protection from evil", 58, 43, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less protected from evil.", 5, 11, NOSCHOOL);
  spello(SPELL_GROUP_ARMOR, "group armor", 58, 43, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 5, 11, NOSCHOOL);
  spello(SPELL_FLAME_STRIKE, "flame strike", 58, 43, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 6, 11, NOSCHOOL);
  spello(SPELL_PROT_FROM_GOOD, "protection from good", 58, 43, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less protected from good.", 5, 11, NOSCHOOL);
  spello(SPELL_MASS_CURE_MODERATE, "mass cure moderate", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 6, 11, NOSCHOOL);
  spello(SPELL_REGENERATION, "regeneration", 58, 43, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS | MAG_POINTS,
          "You feel the regeneration spell wear off.", 5, 11, NOSCHOOL);
  spello(SPELL_FREE_MOVEMENT, "free movement", 58, 43, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS | MAG_UNAFFECTS,
          "You feel the free movement spell wear off.", 5, 11, NOSCHOOL);
  spello(SPELL_STRENGTHEN_BONE, "strengthen bones", 58, 43, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your undead bones weaken again.", 5, 11, NOSCHOOL);
  //poison - shared
  //summon creature 5 - shared
  //waterbreath - shared
  //waterwalk - shared

  // 6th circle
  spello(SPELL_DISPEL_EVIL, "dispel evil", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 5, 12, NOSCHOOL);
  spello(SPELL_HARM, "harm", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 7, 12, NOSCHOOL);
  spello(SPELL_HEAL, "heal", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_POINTS | MAG_UNAFFECTS,
          NULL, 5, 12, NOSCHOOL);
  spello(SPELL_DISPEL_GOOD, "dispel good", 65, 50, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 5, 12, NOSCHOOL);
  spello(SPELL_MASS_CURE_SERIOUS, "mass cure serious", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 6, 12, NOSCHOOL);
  spello(SPELL_PRAYER, "prayer", 44, 29, 1, POS_FIGHTING, TAR_IGNORE, FALSE,
          MAG_GROUPS, "You feel the aid spell fade away.", 8, 12, NOSCHOOL);
  spello(SPELL_REMOVE_DISEASE, "remove disease", 44, 29, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_UNAFFECTS,
          NULL, 7, 12, NOSCHOOL);
  //summon creature 6 - shared
  //eyebite - shared
  //mass wisdom - shared
  //mass charisma - shared
  //mass cunning - shared

  // 7th circle
  spello(SPELL_CALL_LIGHTNING, "call lightning", 72, 57, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 8, 13, NOSCHOOL);
  spello(SPELL_SUMMON, "summon", 72, 57, 1, POS_FIGHTING,
          TAR_CHAR_WORLD | TAR_NOT_SELF, FALSE, MAG_MANUAL,
          NULL, 10, 13, NOSCHOOL);
  spello(SPELL_WORD_OF_RECALL, "word of recall", 72, 57, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_MANUAL,
          NULL, 0, 13, NOSCHOOL);
  spello(SPELL_MASS_CURE_CRIT, "mass cure critic", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 7, 13, NOSCHOOL);
  spello(SPELL_SENSE_LIFE, "sense life", 79, 64, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel less aware of your surroundings.", 8, 14, NOSCHOOL);
  spello(SPELL_BLADE_BARRIER, "blade barrier", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "You watch as the barrier of blades dissipates.", 7, 13,
          CONJURATION);
  spello(SPELL_BATTLETIDE, "battletide", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_SELF_ONLY, FALSE, MAG_AFFECTS,
          "You feel the battletide fade.", 10, 13, NOSCHOOL);
  spello(SPELL_SPELL_RESISTANCE, "spell resistance", 79, 64, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "You feel your spell resistance fade.", 8, 14, NOSCHOOL);
  //control weather - shared
  //summon creature 7 - shared
  //greater dispelling - shared
  //mass enhance - shared

  // 8th circle
  spello(SPELL_SANCTUARY, "sanctuary", 79, 64, 1, POS_FIGHTING,
          TAR_CHAR_ROOM, FALSE, MAG_AFFECTS,
          "The white aura around your body fades.", 8, 14, NOSCHOOL);
  spello(SPELL_DESTRUCTION, "destruction", 79, 64, 1, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_FIGHT_VICT, TRUE, MAG_DAMAGE,
          NULL, 9, 14, NOSCHOOL);
  spello(SPELL_WORD_OF_FAITH, "word of faith", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You no longer feel divinely inflicted.", 0, 14,
          NOSCHOOL);
  spello(SPELL_DIMENSIONAL_LOCK, "dimensional lock", 0, 0, 0, POS_FIGHTING,
          TAR_CHAR_ROOM | TAR_NOT_SELF | TAR_FIGHT_VICT, TRUE, MAG_AFFECTS,
          "You feel locked to this dimension.", 0, 14,
          NOSCHOOL);
  spello(SPELL_SALVATION, "salvation", 79, 64, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL,
          NULL, 8, 14, NOSCHOOL);
  spello(SPELL_SPRING_OF_LIFE, "spring of life", 37, 22, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_CREATIONS, NULL, 14, 14, NOSCHOOL);
  //druid spell
  spello(SPELL_ANIMAL_SHAPES, "animal shapes", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_GROUPS,
          "The primal spell wears off!", 5, 15, NOSCHOOL);

  // 9th circle
  spello(SPELL_EARTHQUAKE, "earthquake", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 10, 15, NOSCHOOL);
  spello(SPELL_PLANE_SHIFT, "plane shift", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_MANUAL, NULL, 2, 15, NOSCHOOL);
  spello(SPELL_GROUP_HEAL, "group heal", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_GROUPS,
          NULL, 5, 15, NOSCHOOL);
  spello(SPELL_GROUP_SUMMON, "group summon", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL,
          NULL, 5, 15, NOSCHOOL);
  spello(SPELL_STORM_OF_VENGEANCE, "storm of vengeance", 85, 70, 1, POS_FIGHTING,
          TAR_IGNORE, FALSE, MAG_MANUAL,
          NULL, 12, 15, NOSCHOOL);
  //energy drain - shared


  // epic divine
  // end divine


  /* NON-castable spells should appear below here. */
  spello(SPELL_ACID, "_acid_", 79, 64, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_MASSES,
          NULL, 8, 12, EVOCATION);
  spello(SPELL_ASHIELD_DAM, "_acid sheath dam_", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AFFECTS,
          NULL, 0, 0, NOSCHOOL);
  spello(SPELL_BLADES, "_blades_", 79, 64, 1, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_MASSES,
          NULL, 8, 12, NOSCHOOL);
  spello(SPELL_CSHIELD_DAM, "_cold shield dam_", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AFFECTS,
          NULL, 0, 0, NOSCHOOL);
  spello(SPELL_DOOM, "_doom_", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS, NULL, 0, 0, NOSCHOOL);
  spello(SPELL_DEATHCLOUD, "_deathcloud_", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 0, 0, NOSCHOOL);
  spello(SPELL_FIRE_BREATHE, "_fire breathe_", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 0, 0, NOSCHOOL);
  spello(SPELL_FSHIELD_DAM, "_fire shield dam_", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AFFECTS,
          NULL, 0, 0, NOSCHOOL);
  /* innate darkness spell, room events testing spell as well */
  spello(SPELL_I_DARKNESS, "innate darkness", 0, 0, 0, POS_STANDING,
          TAR_IGNORE, FALSE, MAG_ROOM,
          "The cloak of darkness in the area dissolves.", 5, 6, NOSCHOOL);
  spello(SPELL_IDENTIFY, "identify", 0, 0, 0, 0,
          TAR_CHAR_ROOM | TAR_OBJ_INV | TAR_OBJ_ROOM, FALSE, MAG_MANUAL,
          NULL, 0, 0, NOSCHOOL);
  spello(SPELL_INCENDIARY, "_incendiary_", 0, 0, 0, POS_FIGHTING,
          TAR_IGNORE, TRUE, MAG_AREAS,
          NULL, 0, 0, NOSCHOOL);
  spello(SPELL_STENCH, "stench", 65, 50, 1, POS_DEAD,
          TAR_IGNORE, FALSE, MAG_MASSES,
          "Your nausea from the noxious gas passes.", 4, 7,
          CONJURATION);


  spello(SPELL_DG_AFFECT, "Afflicted", 0, 0, 0, POS_SITTING,
          TAR_IGNORE, TRUE, 0,
          NULL, 0, 0, NOSCHOOL);


  /* Declaration of skills - this actually doesn't do anything except set it up
   * so that immortals can use these skills by default.  The min level to use
   * the skill for other classes is set up in class.c. */
  skillo(SKILL_BACKSTAB, "backstab", ACTIVE_SKILL); //401
  skillo(SKILL_BASH, "bash", ACTIVE_SKILL);
  skillo(SKILL_MUMMY_DUST, "es mummy dust", CASTER_SKILL);
  skillo(SKILL_KICK, "kick", ACTIVE_SKILL);
  skillo(SKILL_WEAPON_SPECIALIST, "weapon specialist", PASSIVE_SKILL); //405
  skillo(SKILL_WHIRLWIND, "whirlwind", ACTIVE_SKILL);
  skillo(SKILL_RESCUE, "rescue", ACTIVE_SKILL);
  skillo(SKILL_DRAGON_KNIGHT, "es dragon knight", CASTER_SKILL);
  skillo(SKILL_LUCK_OF_HEROES, "luck of heroes", PASSIVE_SKILL);
  skillo(SKILL_TRACK, "track", ACTIVE_SKILL); //410
  skillo(SKILL_QUICK_CHANT, "quick chant", CASTER_SKILL);
  skillo(SKILL_AMBIDEXTERITY, "ambidexterity", PASSIVE_SKILL);
  skillo(SKILL_DIRTY_FIGHTING, "dirty fighting", PASSIVE_SKILL);
  skillo(SKILL_DODGE, "dodge", PASSIVE_SKILL);
  skillo(SKILL_IMPROVED_CRITICAL, "improved critical", PASSIVE_SKILL); //415
  skillo(SKILL_MOBILITY, "mobility", PASSIVE_SKILL);
  skillo(SKILL_SPRING_ATTACK, "spring attack", PASSIVE_SKILL);
  skillo(SKILL_TOUGHNESS, "toughness", PASSIVE_SKILL);
  skillo(SKILL_TWO_WEAPON_FIGHT, "two weapon fighting", PASSIVE_SKILL);
  skillo(SKILL_FINESSE, "finesse", PASSIVE_SKILL); //420
  skillo(SKILL_ARMOR_SKIN, "armor skin", PASSIVE_SKILL);
  skillo(SKILL_BLINDING_SPEED, "blinding speed", PASSIVE_SKILL);
  skillo(SKILL_DAMAGE_REDUC_1, "damage reduction", PASSIVE_SKILL);
  skillo(SKILL_DAMAGE_REDUC_2, "greater damage reduction", PASSIVE_SKILL);
  skillo(SKILL_DAMAGE_REDUC_3, "epic damage reduction", PASSIVE_SKILL); //425
  skillo(SKILL_EPIC_TOUGHNESS, "epic toughness", PASSIVE_SKILL);
  skillo(SKILL_OVERWHELMING_CRIT, "overwhelming critical", PASSIVE_SKILL);
  skillo(SKILL_SELF_CONCEAL_1, "self concealment", PASSIVE_SKILL);
  skillo(SKILL_SELF_CONCEAL_2, "greater concealment", PASSIVE_SKILL);
  skillo(SKILL_SELF_CONCEAL_3, "epic concealment", PASSIVE_SKILL); //430
  skillo(SKILL_TRIP, "trip", ACTIVE_SKILL);
  skillo(SKILL_IMPROVED_WHIRL, "improved whirlwind", ACTIVE_SKILL);
  skillo(SKILL_CLEAVE, "cleave (inc)", PASSIVE_SKILL);
  skillo(SKILL_GREAT_CLEAVE, "great_cleave (inc)", PASSIVE_SKILL);
  skillo(SKILL_SPELLPENETRATE, "spell penetration", CASTER_SKILL); //435
  skillo(SKILL_SPELLPENETRATE_2, "greater spell penetrate", CASTER_SKILL);
  skillo(SKILL_PROWESS, "prowess", PASSIVE_SKILL);
  skillo(SKILL_EPIC_PROWESS, "epic prowess", PASSIVE_SKILL);
  skillo(SKILL_EPIC_2_WEAPON, "epic two weapon fighting", PASSIVE_SKILL);
  skillo(SKILL_SPELLPENETRATE_3, "epic spell penetrate", CASTER_SKILL); //440
  skillo(SKILL_SPELL_RESIST_1, "spell resistance", CASTER_SKILL);
  skillo(SKILL_SPELL_RESIST_2, "improved spell resist", CASTER_SKILL);
  skillo(SKILL_SPELL_RESIST_3, "greater spell resist", CASTER_SKILL);
  skillo(SKILL_SPELL_RESIST_4, "epic spell resist", CASTER_SKILL);
  skillo(SKILL_SPELL_RESIST_5, "supreme spell resist", CASTER_SKILL); //445
  skillo(SKILL_INITIATIVE, "initiative", PASSIVE_SKILL);
  skillo(SKILL_EPIC_CRIT, "epic critical", PASSIVE_SKILL);
  skillo(SKILL_IMPROVED_BASH, "improved bash", ACTIVE_SKILL);
  skillo(SKILL_IMPROVED_TRIP, "improved trip", ACTIVE_SKILL);
  skillo(SKILL_POWER_ATTACK, "power attack", ACTIVE_SKILL); //450
  skillo(SKILL_EXPERTISE, "combat expertise", ACTIVE_SKILL);
  skillo(SKILL_GREATER_RUIN, "es greater ruin", CASTER_SKILL);
  skillo(SKILL_HELLBALL, "es hellball", CASTER_SKILL);
  skillo(SKILL_EPIC_MAGE_ARMOR, "es epic mage armor", CASTER_SKILL);
  skillo(SKILL_EPIC_WARDING, "es epic warding", CASTER_SKILL); //455
  skillo(SKILL_RAGE, "rage", ACTIVE_SKILL); //456
  skillo(SKILL_PROF_MINIMAL, "minimal weapon prof", PASSIVE_SKILL); //457
  skillo(SKILL_PROF_BASIC, "basic weapon prof", PASSIVE_SKILL); //458
  skillo(SKILL_PROF_ADVANCED, "advanced weapon prof", PASSIVE_SKILL); //459
  skillo(SKILL_PROF_MASTER, "master weapon prof", PASSIVE_SKILL); //460
  skillo(SKILL_PROF_EXOTIC, "exotic weapon prof", PASSIVE_SKILL); //461
  skillo(SKILL_PROF_LIGHT_A, "light armor prof", PASSIVE_SKILL); //462
  skillo(SKILL_PROF_MEDIUM_A, "medium armor prof", PASSIVE_SKILL); //463
  skillo(SKILL_PROF_HEAVY_A, "heavy armor prof", PASSIVE_SKILL); //464
  skillo(SKILL_PROF_SHIELDS, "shield prof", PASSIVE_SKILL); //465
  skillo(SKILL_PROF_T_SHIELDS, "tower shield prof", PASSIVE_SKILL); //466
  skillo(SKILL_MURMUR, "murmur(inc)", UNCATEGORIZED); //467
  skillo(SKILL_PROPAGANDA, "propaganda(inc)", UNCATEGORIZED); //468
  skillo(SKILL_LOBBY, "lobby(inc)", UNCATEGORIZED); //469
  skillo(SKILL_STUNNING_FIST, "stunning fist", ACTIVE_SKILL); //470
  skillo(SKILL_MINING, "mining", CRAFTING_SKILL); //471
  skillo(SKILL_HUNTING, "hunting", CRAFTING_SKILL); //472
  skillo(SKILL_FORESTING, "foresting", CRAFTING_SKILL); //473
  skillo(SKILL_KNITTING, "knitting", CRAFTING_SKILL); //474
  skillo(SKILL_CHEMISTRY, "chemistry", CRAFTING_SKILL); //475
  skillo(SKILL_ARMOR_SMITHING, "armor smithing", CRAFTING_SKILL); //476
  skillo(SKILL_WEAPON_SMITHING, "weapon smithing", CRAFTING_SKILL); //477
  skillo(SKILL_JEWELRY_MAKING, "jewelry making", CRAFTING_SKILL); //478
  skillo(SKILL_LEATHER_WORKING, "leather working", CRAFTING_SKILL); //479
  skillo(SKILL_FAST_CRAFTER, "fast crafter", CRAFTING_SKILL); //480
  skillo(SKILL_BONE_ARMOR, "bone armor(inc)", CRAFTING_SKILL); //481
  skillo(SKILL_ELVEN_CRAFTING, "elven crafting(inc)", CRAFTING_SKILL); //482
  skillo(SKILL_MASTERWORK_CRAFTING, "masterwork craft(inc)", CRAFTING_SKILL); //483
  skillo(SKILL_DRACONIC_CRAFTING, "draconic crafting(inc)", CRAFTING_SKILL); //484
  skillo(SKILL_DWARVEN_CRAFTING, "dwarven crafting(inc)", CRAFTING_SKILL); //485
  skillo(SKILL_LIGHTNING_REFLEXES, "lightning reflexes", PASSIVE_SKILL); //486
  skillo(SKILL_GREAT_FORTITUDE, "great fortitude", PASSIVE_SKILL); //487
  skillo(SKILL_IRON_WILL, "iron will", PASSIVE_SKILL); //488
  skillo(SKILL_EPIC_REFLEXES, "epic reflexes", PASSIVE_SKILL); //489
  skillo(SKILL_EPIC_FORTITUDE, "epic fortitude", PASSIVE_SKILL); //490
  skillo(SKILL_EPIC_WILL, "epic will", PASSIVE_SKILL); //491
  skillo(SKILL_SHIELD_SPECIALIST, "shield specialist", PASSIVE_SKILL); //492
  skillo(SKILL_USE_MAGIC, "use magic", ACTIVE_SKILL); //493
  skillo(SKILL_EVASION, "evasion", PASSIVE_SKILL); //494
  skillo(SKILL_IMP_EVASION, "improved evasion", PASSIVE_SKILL); //495
  skillo(SKILL_CRIP_STRIKE, "crippling strike", PASSIVE_SKILL); //496
  skillo(SKILL_SLIPPERY_MIND, "slippery mind", PASSIVE_SKILL); //497
  skillo(SKILL_DEFENSE_ROLL, "defensive roll", PASSIVE_SKILL); //498
  skillo(SKILL_GRACE, "divine grace", PASSIVE_SKILL); //499
  skillo(SKILL_DIVINE_HEALTH, "divine health", PASSIVE_SKILL); //500
  skillo(SKILL_LAY_ON_HANDS, "lay on hands", ACTIVE_SKILL); //501
  skillo(SKILL_COURAGE, "courage", PASSIVE_SKILL); //502
  skillo(SKILL_SMITE, "smite", ACTIVE_SKILL); //503
  skillo(SKILL_REMOVE_DISEASE, "purify", ACTIVE_SKILL); //504
  skillo(SKILL_RECHARGE, "recharge", CASTER_SKILL); //505
  skillo(SKILL_STEALTHY, "stealthy", PASSIVE_SKILL); //506
  skillo(SKILL_NATURE_STEP, "nature step", PASSIVE_SKILL); //507
  skillo(SKILL_FAVORED_ENEMY, "favored enemy", PASSIVE_SKILL); //508
  skillo(SKILL_DUAL_WEAPONS, "dual weapons", PASSIVE_SKILL); //509
  skillo(SKILL_ANIMAL_COMPANION, "animal companion", ACTIVE_SKILL); //510
  skillo(SKILL_PALADIN_MOUNT, "paladin mount", ACTIVE_SKILL); //511
  skillo(SKILL_CALL_FAMILIAR, "call familiar", ACTIVE_SKILL); //512
  skillo(SKILL_PERFORM, "perform", ACTIVE_SKILL); //513
  skillo(SKILL_SCRIBE, "scribe", ACTIVE_SKILL); //514
  skillo(SKILL_TURN_UNDEAD, "turn undead", ACTIVE_SKILL); //515
  skillo(SKILL_WILDSHAPE, "wildshape", ACTIVE_SKILL); //516

  /****note weapon specialist and luck of heroes inserted in free slots ***/

}

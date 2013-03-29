/**************************************************************************
 *  File: act.informative.c                                 Part of tbaMUD *
 *  Usage: Player-level commands of an informative nature.                 *
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
#include "interpreter.h"
#include "handler.h"
#include "db.h"
#include "spells.h"
#include "screen.h"
#include "constants.h"
#include "dg_scripts.h"
#include "mud_event.h"
#include "mail.h"         /**< For the has_mail function */
#include "act.h"
#include "class.h"
#include "fight.h"
#include "modify.h"
#include "asciimap.h"
#include "spells.h"
#include "clan.h"
#include "craft.h"  // auto crafting quest

/* prototypes of local functions */
/* do_diagnose utility functions */
static void diag_char_to_char(struct char_data *i, struct char_data *ch);
/* do_look and do_examine utility functions */
static void do_auto_exits(struct char_data *ch);
static void list_char_to_char(struct char_data *list, struct char_data *ch);
static void list_one_char(struct char_data *i, struct char_data *ch);
static void look_at_char(struct char_data *i, struct char_data *ch);
static void look_at_target(struct char_data *ch, char *arg);
static void look_in_direction(struct char_data *ch, int dir);
static void look_in_obj(struct char_data *ch, char *arg);
/* do_look, do_inventory utility functions */
static void list_obj_to_char(struct obj_data *list, struct char_data *ch, int mode, int show, int mxp_type);
/* do_look, do_equipment, do_examine, do_inventory */
static void show_obj_modifiers(struct obj_data *obj, struct char_data *ch);
/* do_where utility functions */
static void perform_immort_where(struct char_data *ch, char *arg);
static void perform_mortal_where(struct char_data *ch, char *arg);
static void print_object_location(int num, struct obj_data *obj, struct char_data *ch, int recur);

int boot_high = 0;

/* Subcommands */
/* For show_obj_to_char 'mode'.	/-- arbitrary */
#define SHOW_OBJ_LONG     0
#define SHOW_OBJ_SHORT    1
#define SHOW_OBJ_ACTION   2

void show_obj_to_char(struct obj_data *obj, struct char_data *ch, int mode, int mxp_type) {
  char keyword[100], keyword1[100], sendcmd[20];
  int found = 0, item_num = 0;
  struct char_data *temp;
  struct obj_data *temp_obj;

  // mxp_type 1 = do_inventory
  // mxp_type 2 = do_equipment
  // maybe change these to defines in protocol.h or something
  // these will be used to give click options i.e. click an item in inventory
  // to equip it, or right click with context menu, to equip/drop/lore/etc.
  
  if (!obj || !ch) {
    log("SYSERR: NULL pointer in show_obj_to_char(): obj=%p ch=%p", obj, ch);
    /*  SYSERR_DESC: Somehow a NULL pointer was sent to show_obj_to_char() in
     *  either the 'obj' or the 'ch' variable.  The error will indicate which
     *  was NULL by listing both of the pointers passed to it.  This is often a
     *  difficult one to trace, and may require stepping through a debugger. */
    return;
  }

  if ((mode == 0) && obj->description) {
    if (!GET_OBJ_VAL(obj, 1) == 0 || OBJ_SAT_IN_BY(obj)) {
      temp = OBJ_SAT_IN_BY(obj);
      for (temp = OBJ_SAT_IN_BY(obj); temp; temp = NEXT_SITTING(temp)) {
        if (temp == ch)
          found++;
      }
      if (found) {
        send_to_char(ch, "You are %s upon %s.", GET_POS(ch) == POS_SITTING ? "sitting" :
                "resting", obj->short_description);
        goto end;
      }
    }
  }

  switch (mode) {
    case SHOW_OBJ_LONG:
      /* Hide objects starting with . from non-holylighted people. - Elaseth */
      if (*obj->description == '.' && (IS_NPC(ch) || !PRF_FLAGGED(ch, PRF_HOLYLIGHT)))
        return;

      if (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_SHOWVNUMS)) {
        send_to_char(ch, "[%d] ", GET_OBJ_VNUM(obj));
        if (SCRIPT(obj)) {
          if (!TRIGGERS(SCRIPT(obj))->next)
            send_to_char(ch, "[T%d] ", GET_TRIG_VNUM(TRIGGERS(SCRIPT(obj))));
          else
            send_to_char(ch, "[TRIGS] ");
        }
      }
      send_to_char(ch, "%s", CCGRN(ch, C_NRM));
      send_to_char(ch, "%s", obj->description);
      break;

    case SHOW_OBJ_SHORT:
      if (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_SHOWVNUMS)) {
        send_to_char(ch, "[%d] ", GET_OBJ_VNUM(obj));
        if (SCRIPT(obj)) {
          if (!TRIGGERS(SCRIPT(obj))->next)
            send_to_char(ch, "[T%d] ", GET_TRIG_VNUM(TRIGGERS(SCRIPT(obj))));
          else
            send_to_char(ch, "[TRIGS] ");
        }
      }

      if (mxp_type != 0) {
        one_argument(obj->name, keyword);

        switch (mxp_type) {
          case 1: // inventory
            // loop through to ensure correct item, i.e. 2.dagger, 3.armor, etc.
            for (temp_obj = ch->carrying; temp_obj; temp_obj = temp_obj->next_content) {
              // check if the temp_obj contains keyword in the name list
              if (isname(keyword, temp_obj->name)) {
                if (temp_obj->short_description == obj->short_description)
                  // this is the item they are trying to interact with
                  // or at least has the same short description
                  break;
                else
                  item_num++;
              }
            }
            if (item_num > 0) {
              sprintf(keyword1, "%d.%s", (item_num + 1), keyword);
              strcpy(keyword, keyword1);
            }
            if (GET_OBJ_TYPE(obj) == ITEM_WEAPON)
              strcpy(sendcmd, "wield");
            else if (GET_OBJ_TYPE(obj) == ITEM_SCROLL)
              strcpy(sendcmd, "recite");
            else if (GET_OBJ_TYPE(obj) == ITEM_POTION)
              strcpy(sendcmd, "quaff");
            else if (GET_OBJ_TYPE(obj) == ITEM_ARMOR)
              strcpy(sendcmd, "wear");
            else if (GET_OBJ_TYPE(obj) == ITEM_WORN)
              strcpy(sendcmd, "wear");
            else if (GET_OBJ_TYPE(obj) == ITEM_FOOD)
              strcpy(sendcmd, "eat");
            else if (GET_OBJ_TYPE(obj) == ITEM_DRINKCON)
              strcpy(sendcmd, "drink");
            else if (GET_OBJ_TYPE(obj) == ITEM_NOTE)
              strcpy(sendcmd, "read");
            else if (GET_OBJ_TYPE(obj) == ITEM_SPELLBOOK)
              strcpy(sendcmd, "look in");
            else
              strcpy(sendcmd, "hold");
            send_to_char(ch, "\t<send href='%s %s|drop %s|eat %s|hold %s|lore %s' hint='use/equip %s|drop %s|eat %s|hold %s|lore %s'>%s\t</send>", sendcmd, keyword,
                    keyword, keyword, keyword, keyword, keyword, keyword, keyword, keyword, keyword, obj->short_description);
            break;
          case 2: // equipment
            send_to_char(ch, "\t<send href='remove %s'>%s\t</send>", keyword, obj->short_description);
            break;
        }
      } else {
        send_to_char(ch, "%s", obj->short_description);
      }
      break;

    case SHOW_OBJ_ACTION:
      switch (GET_OBJ_TYPE(obj)) {
        case ITEM_NOTE:
          if (obj->action_description) {
            char notebuf[MAX_NOTE_LENGTH + 64];

            snprintf(notebuf, sizeof (notebuf), "There is something written on it:\r\n\r\n%s", obj->action_description);
            page_string(ch->desc, notebuf, TRUE);
          } else
            send_to_char(ch, "It's blank.\r\n");
          return;

        case ITEM_DRINKCON:
          send_to_char(ch, "It looks like a drink container.");
          break;

        default:
          send_to_char(ch, "You see nothing special..");
          break;
      }
      break;

    default:
      log("SYSERR: Bad display mode (%d) in show_obj_to_char().", mode);
      /*  SYSERR_DESC:  show_obj_to_char() has some predefined 'mode's (argument
       *  #3) to tell it what to display to the character when it is called.  If
       *  the mode is not one of these, it will output this error, and indicate
       *  what mode was passed to it.  To correct it, you will need to find the
       *  call with the incorrect mode and change it to an acceptable mode. */
      return;
  }
end:

  show_obj_modifiers(obj, ch);
  send_to_char(ch, "\r\n");
}

static void show_obj_modifiers(struct obj_data *obj, struct char_data *ch) {
  if (OBJ_FLAGGED(obj, ITEM_INVISIBLE))
    send_to_char(ch, " \tw(invisible)\tn");

  if (OBJ_FLAGGED(obj, ITEM_BLESS) && AFF_FLAGGED(ch, AFF_DETECT_ALIGN))
    send_to_char(ch, " \tn..It glows \tBblue\tn!");

  if (OBJ_FLAGGED(obj, ITEM_MAGIC) && AFF_FLAGGED(ch, AFF_DETECT_MAGIC))
    send_to_char(ch, " \tn..It glows \tYyellow\tn!");

  if (OBJ_FLAGGED(obj, ITEM_GLOW))
    send_to_char(ch, " \tW..It has a soft glowing aura!\tn");

  if (OBJ_FLAGGED(obj, ITEM_HUM))
    send_to_char(ch, " \tn..It emits a faint \tChumming\tn sound!");
}

static void list_obj_to_char(struct obj_data *list, struct char_data *ch, int mode, int show, int mxp_type) {
  struct obj_data *i, *j, *display;
  bool found;
  int num;

  found = FALSE;

  /* Loop through the list of objects */
  for (i = list; i; i = i->next_content) {
    num = 0;

    /* Check the list to see if we've already counted this object */
    for (j = list; j != i; j = j->next_content)
      if ((j->short_description == i->short_description && j->name == i->name) ||
              (!strcmp(j->short_description, i->short_description) && !strcmp(j->name, i->name)))
        break; /* found a matching object */
    if (j != i)
      continue; /* we counted object i earlier in the list */

    /* Count matching objects, including this one */
    for (display = j = i; j; j = j->next_content)
      /* This if-clause should be exactly the same as the one in the loop above */
      if ((j->short_description == i->short_description && j->name == i->name) ||
              (!strcmp(j->short_description, i->short_description) && !strcmp(j->name, i->name)))
        if (CAN_SEE_OBJ(ch, j)) {
          ++num;
          /* If the original item can't be seen, switch it for this one */
          if (display == i && !CAN_SEE_OBJ(ch, display))
            display = j;
        }

    /* When looking in room, hide objects starting with '.', except for holylight */
    if (num > 0 && (mode != SHOW_OBJ_LONG || *display->description != '.' ||
            (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_HOLYLIGHT)))) {
      if (mode == SHOW_OBJ_LONG)
        send_to_char(ch, "%s", CCGRN(ch, C_NRM));
      if (num != 1)
        send_to_char(ch, "(%2i) ", num);
      show_obj_to_char(display, ch, mode, mxp_type);
      send_to_char(ch, "%s", CCNRM(ch, C_NRM));
      found = TRUE;
    }
  }
  if (!found && show)
    send_to_char(ch, "  Nothing.\r\n");
}

static void diag_char_to_char(struct char_data *i, struct char_data *ch) {

  struct {
    byte percent;
    const char *text;
  } diagnosis[] = {
    { 100, "is in excellent condition."},
    { 90, "has a few scratches."},
    { 75, "has some small wounds and bruises."},
    { 50, "has quite a few wounds."},
    { 30, "has some big nasty wounds and scratches."},
    { 15, "looks pretty hurt."},
    { 0, "is in awful condition."},
    { -1, "is bleeding awfully from big wounds."},
  };
  int percent, ar_index;
  char *pers = strdup(PERS(i, ch));

  if (GET_MAX_HIT(i) > 0)
    percent = (100 * GET_HIT(i)) / GET_MAX_HIT(i);
  else
    percent = -1; /* How could MAX_HIT be < 1?? */

  for (ar_index = 0; diagnosis[ar_index].percent >= 0; ar_index++)
    if (percent >= diagnosis[ar_index].percent)
      break;

  if (!IS_NPC(i))
    send_to_char(ch, "%s \tn[%s %s\tn] %s\r\n", CAP(pers), size_names[GET_SIZE(i)],
          RACE_ABBR(i), diagnosis[ar_index].text);
  else if (IS_NPC(i) && GET_RACE(i) <= NPCRACE_UNKNOWN)
    send_to_char(ch, "%s %s\r\n", CAP(pers),
          diagnosis[ar_index].text);
  else if (IS_NPC(i) && GET_SUBRACE(i, 0) <= SUBRACE_UNKNOWN
          && GET_SUBRACE(i, 1) <= SUBRACE_UNKNOWN
          && GET_SUBRACE(i, 2) <= SUBRACE_UNKNOWN)
    send_to_char(ch, "%s \tn[%s %s\tn] %s\r\n", CAP(pers),
          size_names[GET_SIZE(i)], RACE_ABBR(i), diagnosis[ar_index].text);
  else
    send_to_char(ch, "%s \tn[%s %s/%s/%s %s\tn] %s\r\n", CAP(pers),
          size_names[GET_SIZE(i)], npc_subrace_abbrevs[GET_SUBRACE(i, 0)],
          npc_subrace_abbrevs[GET_SUBRACE(i, 1)],
          npc_subrace_abbrevs[GET_SUBRACE(i, 2)],
          RACE_ABBR(i), diagnosis[ar_index].text);

  if (affected_by_spell(i, SPELL_BARKSKIN))
    act("$s skin appears to be made of bark.", FALSE, i, 0, ch, TO_VICT);
  if (affected_by_spell(i, SPELL_STONESKIN))
    act("$s skin appears to be made of stone.", FALSE, i, 0, ch, TO_VICT);
  if (affected_by_spell(i, SPELL_IRONSKIN))
    act("$s skin appears to be made of iron.", FALSE, i, 0, ch, TO_VICT);
  free(pers);
  pers = NULL;
}

static void look_at_char(struct char_data *i, struct char_data *ch) {
  int j, found;
  char buf[MAX_INPUT_LENGTH];

  if (!ch->desc)
    return;

  if (i->player.description)
    send_to_char(ch, "%s", i->player.description);
  else
    act("You see nothing special about $m.", FALSE, i, 0, ch, TO_VICT);

  diag_char_to_char(i, ch);

  // mounted
  if (RIDING(i) && RIDING(i)->in_room == i->in_room) {
    if (RIDING(i) == ch)
      act("$e is mounted on you.", FALSE, i, 0, ch, TO_VICT);
    else {
      snprintf(buf, sizeof (buf), "$e is mounted upon %s.", PERS(RIDING(i), ch));
      act(buf, FALSE, i, 0, ch, TO_VICT);
    }
  } else if (RIDDEN_BY(i) && RIDDEN_BY(i)->in_room == i->in_room) {
    if (RIDDEN_BY(i) == ch)
      act("You are mounted upon $m.", FALSE, i, 0, ch, TO_VICT);
    else {
      snprintf(buf, sizeof (buf), "$e is mounted by %s.", PERS(RIDDEN_BY(i), ch));
      act(buf, FALSE, i, 0, ch, TO_VICT);
    }
  }

  found = FALSE;
  for (j = 0; !found && j < NUM_WEARS; j++)
    if (GET_EQ(i, j) && CAN_SEE_OBJ(ch, GET_EQ(i, j)))
      found = TRUE;

  if (found) {
    send_to_char(ch, "\r\n"); /* act() does capitalization. */
    act("$n is using:", FALSE, i, 0, ch, TO_VICT);
    for (j = 0; j < NUM_WEARS; j++)
      if (GET_EQ(i, j) && CAN_SEE_OBJ(ch, GET_EQ(i, j))) {
        send_to_char(ch, "%s", wear_where[j]);
        show_obj_to_char(GET_EQ(i, j), ch, SHOW_OBJ_SHORT, 0);
      }
  }
  if (ch != i && (IS_ROGUE(ch) || GET_LEVEL(ch) >= LVL_IMMORT)) {
    act("\r\nYou attempt to peek at $s inventory:", FALSE, i, 0, ch, TO_VICT);
    list_obj_to_char(i->carrying, ch, SHOW_OBJ_SHORT, TRUE, 0);
  }
}

static void list_one_char(struct char_data *i, struct char_data *ch) {
  struct obj_data *furniture;
  char *short_descr;
  const char *positions[] = {
    " is lying here, dead.",
    " is lying here, mortally wounded.",
    " is lying here, incapacitated.",
    " is lying here, stunned.",
    " is sleeping here.",
    " is resting here.",
    " is sitting here.",
    "!FIGHTING!",
    " is standing here."
  };

  if (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_SHOWVNUMS)) {
    if (IS_NPC(i))
      send_to_char(ch, "[%d] ", GET_MOB_VNUM(i));
    if (SCRIPT(i) && TRIGGERS(SCRIPT(i))) {
      if (!TRIGGERS(SCRIPT(i))->next)
        send_to_char(ch, "[T%d] ", GET_TRIG_VNUM(TRIGGERS(SCRIPT(i))));
      else
        send_to_char(ch, "[TRIGS] ");
    }
  }

  if (GROUP(i)) {
    if (GROUP(i) == GROUP(ch))
      send_to_char(ch, "(%s) ",
            GROUP_LEADER(GROUP(i)) == i ? "leader" : "group");
    else
      send_to_char(ch, "%s(%s%s%s) ", CCNRM(ch, C_NRM), CBRED(ch, C_NRM),
            GROUP_LEADER(GROUP(i)) == i ? "leader" : "group",
            CCNRM(ch, C_NRM));
  }

  if (IS_NPC(i) && i->player.long_descr && GET_POS(i) == GET_DEFAULT_POS(i) &&
          !FIGHTING(i)) {
    if (AFF_FLAGGED(i, AFF_INVISIBLE))
      send_to_char(ch, "*");

    if (AFF_FLAGGED(ch, AFF_DETECT_ALIGN) || CLASS_LEVEL(ch, CLASS_PALADIN)) {
      if (IS_EVIL(i))
        send_to_char(ch, "\tR(Red Aura)\tn ");
      else if (AFF_FLAGGED(ch, AFF_DETECT_ALIGN) && IS_GOOD(i))
        send_to_char(ch, "\tB(Blue Aura)\tn ");
    }

    send_to_char(ch, "%s", i->player.long_descr);

    if (AFF_FLAGGED(i, AFF_SANCTUARY))
      act("...$e glows with a bright light!", FALSE, i, 0, ch, TO_VICT);
    if (AFF_FLAGGED(i, AFF_BLIND) && GET_LEVEL(i) < LVL_IMMORT)
      act("...$e is groping around blindly!", FALSE, i, 0, ch, TO_VICT);
    if (AFF_FLAGGED(i, AFF_FAERIE_FIRE))
      act("...$e is surrounded by a pale blue light!", FALSE, i, 0, ch, TO_VICT);

    return;

    /* for non fighting mobiles */
  } else if (IS_NPC(i) && i->player.long_descr &&
          MOB_FLAGGED(i, MOB_NOFIGHT)) {

    if (AFF_FLAGGED(i, AFF_INVISIBLE))
      send_to_char(ch, "*");

    if (AFF_FLAGGED(ch, AFF_DETECT_ALIGN) || CLASS_LEVEL(ch, CLASS_PALADIN)) {
      if (IS_EVIL(i))
        send_to_char(ch, "\tR(Red Aura)\tn ");
      else if (AFF_FLAGGED(ch, AFF_DETECT_ALIGN) && IS_GOOD(i))
        send_to_char(ch, "\tB(Blue Aura)\tn ");
    }

    send_to_char(ch, "%s", i->player.long_descr);

    if (AFF_FLAGGED(i, AFF_SANCTUARY))
      act("...$e glows with a bright light!", FALSE, i, 0, ch, TO_VICT);
    if (AFF_FLAGGED(i, AFF_BLIND) && GET_LEVEL(i) < LVL_IMMORT)
      act("...$e is groping around blindly!", FALSE, i, 0, ch, TO_VICT);
    if (AFF_FLAGGED(i, AFF_FAERIE_FIRE))
      act("...$e is surrounded by a pale blue light!", FALSE, i, 0, ch, TO_VICT);

    return;
  }

  if (IS_NPC(i)) {
    short_descr = strdup(i->player.short_descr);
    send_to_char(ch, "%s", CAP(short_descr));
    free(short_descr);
    short_descr = NULL;
  } else
    send_to_char(ch, "\tn[%s] %s%s%s", RACE_ABBR(i), i->player.name,
          *GET_TITLE(i) ? " " : "", GET_TITLE(i));

  if (AFF_FLAGGED(i, AFF_INVISIBLE))
    send_to_char(ch, " (invisible)");
  if (AFF_FLAGGED(i, AFF_HIDE))
    send_to_char(ch, " (hidden)");
  if (!IS_NPC(i) && !i->desc)
    send_to_char(ch, " (linkless)");
  if (!IS_NPC(i) && PLR_FLAGGED(i, PLR_WRITING))
    send_to_char(ch, " (writing)");
  if (!IS_NPC(i) && PRF_FLAGGED(i, PRF_BUILDWALK))
    send_to_char(ch, " (buildwalk)");
  if (!IS_NPC(i) && PRF_FLAGGED(i, PRF_AFK))
    send_to_char(ch, " (AFK)");

  if (RIDING(i) && RIDING(i)->in_room == i->in_room) {
    send_to_char(ch, " is here, mounted upon ");
    if (RIDING(i) == ch)
      send_to_char(ch, "you");
    else
      send_to_char(ch, "%s", PERS(RIDING(i), ch));
    send_to_char(ch, ".");
  } else if (!FIGHTING(i)) {
    if (!SITTING(i))
      send_to_char(ch, "%s", positions[(int) GET_POS(i)]);
    else {
      furniture = SITTING(i);
      send_to_char(ch, " is %s upon %s.", (GET_POS(i) == POS_SLEEPING ?
              "sleeping" : (GET_POS(i) == POS_RESTING ? "resting" : "sitting")),
              OBJS(furniture, ch));
    }
  } else {
    if (FIGHTING(i)) {
      send_to_char(ch, " is here, fighting ");
      if (FIGHTING(i) == ch)
        send_to_char(ch, "YOU!");
      else {
        if (IN_ROOM(i) == IN_ROOM(FIGHTING(i)))
          send_to_char(ch, "%s!", PERS(FIGHTING(i), ch));
        else
          send_to_char(ch, "someone who has already left!");
      }
    } else /* NIL fighting pointer */
      send_to_char(ch, " is here struggling with thin air.");
  }

  if (AFF_FLAGGED(ch, AFF_DETECT_ALIGN) || CLASS_LEVEL(ch, CLASS_PALADIN)) {
    if (IS_EVIL(i))
      send_to_char(ch, "\tR(Red Aura)\tn ");
    else if (AFF_FLAGGED(ch, AFF_DETECT_ALIGN) && IS_GOOD(i))
      send_to_char(ch, "\tB(Blue Aura)\tn ");
  }
  send_to_char(ch, "\r\n");

  if (AFF_FLAGGED(i, AFF_SANCTUARY))
    act("...$e glows with a bright light!", FALSE, i, 0, ch, TO_VICT);
  if (AFF_FLAGGED(i, AFF_BLIND) && GET_LEVEL(i) < LVL_IMMORT)
    act("...$e is groping around blindly!", FALSE, i, 0, ch, TO_VICT);
  if (AFF_FLAGGED(i, AFF_FAERIE_FIRE))
    act("...$e is surrounded by a pale blue light!", FALSE, i, 0, ch, TO_VICT);
}

/*  The CAN_SEE and CAN_INFRA macros are both going to do a hide-check
 *  so we added a rule that infra won't work for hidden targets
 *    -zusuk
 */
static void list_char_to_char(struct char_data *list, struct char_data *ch) {
  struct char_data *i;

  for (i = list; i; i = i->next_in_room)
    if (ch != i) {
      /* hide npcs whose description starts with a '.' from non-holylighted people - Idea from Elaseth of TBA */
      if (!IS_NPC(ch) && !PRF_FLAGGED(ch, PRF_HOLYLIGHT) &&
              IS_NPC(i) && i->player.long_descr && *i->player.long_descr == '.')
        continue;
      send_to_char(ch, "%s", CCYEL(ch, C_NRM));
      if (RIDDEN_BY(i) && RIDDEN_BY(i)->in_room == i->in_room)
        continue;
      if (CAN_SEE(ch, i))
        list_one_char(i, ch);
      else if (CAN_INFRA(ch, i) && !AFF_FLAGGED(i, AFF_HIDE))
        send_to_char(ch, "\tnYou see the \trred\tn outline of someone or something.\r\n");
      else if (IS_DARK(IN_ROOM(ch)) && !CAN_SEE_IN_DARK(ch) &&
              AFF_FLAGGED(i, AFF_INFRAVISION) && INVIS_OK(ch, i))
        send_to_char(ch, "You see a pair of glowing red eyes looking your way.\r\n");
      else if ((!IS_DARK(IN_ROOM(ch)) || CAN_SEE_IN_DARK(ch)) &&
              AFF_FLAGGED(ch, AFF_SENSE_LIFE))
        send_to_char(ch, "You sense a life-form.\r\n");
      send_to_char(ch, "%s", CCNRM(ch, C_NRM));
    }
}

static void do_auto_exits(struct char_data *ch) {
  int door, slen = 0;

  send_to_char(ch, "%s[ Exits: ", CCCYN(ch, C_NRM));

  for (door = 0; door < DIR_COUNT; door++) {
    if (!EXIT(ch, door) || EXIT(ch, door)->to_room == NOWHERE)
      continue;
    if (EXIT_FLAGGED(EXIT(ch, door), EX_CLOSED) && !CONFIG_DISP_CLOSED_DOORS)
      continue;
    if (EXIT_FLAGGED(EXIT(ch, door), EX_HIDDEN) &&
            !PRF_FLAGGED(ch, PRF_HOLYLIGHT))
      continue;
    if (EXIT_FLAGGED(EXIT(ch, door), EX_CLOSED))
      send_to_char(ch, "%s(%s)%s ", EXIT_FLAGGED(EXIT(ch, door), EX_HIDDEN) ?
            CCWHT(ch, C_NRM) : CCRED(ch, C_NRM), autoexits[door], CCCYN(ch, C_NRM));
    else if (EXIT_FLAGGED(EXIT(ch, door), EX_HIDDEN))
      send_to_char(ch, "%s%s%s ", CCWHT(ch, C_NRM), autoexits[door],
            CCCYN(ch, C_NRM));
    else
      send_to_char(ch, "\t(%s\t) ", autoexits[door]);
    slen++;
  }

  send_to_char(ch, "%s]%s\r\n", slen ? "" : "None!", CCNRM(ch, C_NRM));
}

ACMD(do_exits) {
  int door, len = 0;

  if (AFF_FLAGGED(ch, AFF_BLIND) && GET_LEVEL(ch) < LVL_IMMORT) {
    send_to_char(ch, "You can't see a damned thing, you're blind!\r\n");
    return;
  }

  send_to_char(ch, "Obvious exits:\r\n");

  for (door = 0; door < DIR_COUNT; door++) {
    if (!EXIT(ch, door) || EXIT(ch, door)->to_room == NOWHERE)
      continue;
    if (EXIT_FLAGGED(EXIT(ch, door), EX_CLOSED) && !CONFIG_DISP_CLOSED_DOORS)
      continue;
    if (EXIT_FLAGGED(EXIT(ch, door), EX_HIDDEN) && !PRF_FLAGGED(ch, PRF_HOLYLIGHT))
      continue;

    len++;

    if (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_SHOWVNUMS) && !EXIT_FLAGGED(EXIT(ch, door), EX_CLOSED))
      send_to_char(ch, "%-5s - [%5d]%s %s\r\n", dirs[door], GET_ROOM_VNUM(EXIT(ch, door)->to_room),
            EXIT_FLAGGED(EXIT(ch, door), EX_HIDDEN) ? " [HIDDEN]" : "", world[EXIT(ch, door)->to_room].name);
    else if (CONFIG_DISP_CLOSED_DOORS && EXIT_FLAGGED(EXIT(ch, door), EX_CLOSED)) {
      /* But we tell them the door is closed */
      send_to_char(ch, "%-5s - The %s is closed%s\r\n", dirs[door],
              (EXIT(ch, door)->keyword) ? fname(EXIT(ch, door)->keyword) : "opening",
              EXIT_FLAGGED(EXIT(ch, door), EX_HIDDEN) ? " and hidden." : ".");
    } else
      send_to_char(ch, "%-5s - %s\r\n", dirs[door], IS_DARK(EXIT(ch, door)->to_room) &&
            !CAN_SEE_IN_DARK(ch) ? "Too dark to tell." : world[EXIT(ch, door)->to_room].name);
  }

  if (!len)
    send_to_char(ch, " None.\r\n");
}

void look_at_room(struct char_data *ch, int ignore_brief) {
  trig_data *t;
  struct room_data *rm = &world[IN_ROOM(ch)];
  room_vnum target_room;
  int isDark = 0, canSee = 0, canInfra = 0, worldmapOn = 0;
  zone_rnum zn;
  char buf[MAX_STRING_LENGTH];

  if (!ch->desc)
    return;

  target_room = IN_ROOM(ch);
  zn = GET_ROOM_ZONE(target_room);

  if (IS_DARK(IN_ROOM(ch)))
    isDark = 1;
  if ((isDark && CAN_SEE_IN_DARK(ch)) || !isDark)
    canSee = 1;
  if (isDark && !canSee && CAN_INFRA_IN_DARK(ch)) {
    canInfra = 1;
    ignore_brief = 0;
  }

  if (isDark && !CAN_SEE_IN_DARK(ch) && !CAN_INFRA_IN_DARK(ch)) {
    send_to_char(ch, "It is pitch black...\r\n");
    return;
  } else if (AFF_FLAGGED(ch, AFF_BLIND) && GET_LEVEL(ch) < LVL_IMMORT) {
    send_to_char(ch, "You see nothing but infinite darkness...\r\n");
    return;
  } else if (ROOM_AFFECTED(ch->in_room, RAFF_FOG)) {
    send_to_char(ch, "Your view is obscured by a thick fog...\r\n");
    return;
  }

  if (!canInfra && (ROOM_FLAGGED(target_room, ROOM_WORLDMAP) ||
          ZONE_FLAGGED(zn, ZONE_WORLDMAP))) {
    worldmapOn = 1;
    /* if you want clear screen, uncomment this */
    //    send_to_char(ch, "\033[H\033[J");
  }

  // staff can see the vnums
  if (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_SHOWVNUMS)) {
    sprintbitarray(ROOM_FLAGS(IN_ROOM(ch)), room_bits, RF_ARRAY_MAX, buf);
    send_to_char(ch, "%s", CCCYN(ch, C_NRM));
    send_to_char(ch, "[%5d]%s ", GET_ROOM_VNUM(IN_ROOM(ch)), CCNRM(ch, C_NRM));
    send_to_char(ch, "%s %s[ %s] ", world[IN_ROOM(ch)].name, CCCYN(ch, C_NRM), buf);

    if (SCRIPT(rm)) {
      send_to_char(ch, "[T");
      for (t = TRIGGERS(SCRIPT(rm)); t; t = t->next)
        send_to_char(ch, " %d", GET_TRIG_VNUM(t));
      send_to_char(ch, "]");
    }
  } else
    send_to_char(ch, "%s", world[IN_ROOM(ch)].name);

  // room affections
  sprintbit((long) rm->room_affections, room_affections, buf, sizeof (buf));
  send_to_char(ch, " ( %s)", buf);

  // carrier return
  send_to_char(ch, "%s\r\n", CCNRM(ch, C_NRM));

  // !infra && worldmap, force seeing worldmap no descrip
  // !brief/infra && !worldmap display str_and_map or descrip
  // if infra/brief, you'll get a brief mapless output
  if (worldmapOn) {
    perform_map(ch, "", show_worldmap(ch));
  } else if ((!IS_NPC(ch) && !PRF_FLAGGED(ch, PRF_BRIEF) && !canInfra) || ignore_brief ||
          ROOM_FLAGGED(IN_ROOM(ch), ROOM_DEATH)) {
    if (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_AUTOMAP) && can_see_map(ch)) {
      str_and_map(world[target_room].description, ch, target_room);
    } else {
      send_to_char(ch, "%s", world[IN_ROOM(ch)].description);
    }
  } else if (canInfra) {
    send_to_char(ch, "\tDIt is hard to make out too much detail with just \trinfravision\tD.\r\n");
  }

  /* autoexits */
  if (!IS_NPC(ch) && PRF_FLAGGED(ch, PRF_AUTOEXIT))
    do_auto_exits(ch);

  /* now list characters & objects */
  list_obj_to_char(world[IN_ROOM(ch)].contents, ch, SHOW_OBJ_LONG, FALSE, 0);
  list_char_to_char(world[IN_ROOM(ch)].people, ch);
}

static void look_in_direction(struct char_data *ch, int dir) {
  if (EXIT(ch, dir)) {
    if (EXIT(ch, dir)->general_description)
      send_to_char(ch, "%s", EXIT(ch, dir)->general_description);
    else
      send_to_char(ch, "You do not see anything in that direction.\r\n");

    if (EXIT_FLAGGED(EXIT(ch, dir), EX_CLOSED) && EXIT(ch, dir)->keyword)
      send_to_char(ch, "The %s is closed.\r\n", fname(EXIT(ch, dir)->keyword));
    else if (EXIT_FLAGGED(EXIT(ch, dir), EX_ISDOOR) && EXIT(ch, dir)->keyword)
      send_to_char(ch, "The %s is open.\r\n", fname(EXIT(ch, dir)->keyword));

  } else
    send_to_char(ch, "You do not see anything special in that direction...\r\n");
}

static void look_in_obj(struct char_data *ch, char *arg) {
  struct obj_data *obj = NULL;
  struct char_data *dummy = NULL;
  int amt, bits;

  if (!*arg)
    send_to_char(ch, "Look in what?\r\n");
  else if (!(bits = generic_find(arg, FIND_OBJ_INV | FIND_OBJ_ROOM |
          FIND_OBJ_EQUIP, ch, &dummy, &obj))) {
    send_to_char(ch, "There doesn't seem to be %s %s here.\r\n", AN(arg), arg);
  } else if (GET_OBJ_TYPE(obj) == ITEM_SPELLBOOK) {
    display_spells(ch, obj);
  } else if (GET_OBJ_TYPE(obj) == ITEM_SCROLL) {
    display_scroll(ch, obj);
  } else if ((GET_OBJ_TYPE(obj) != ITEM_DRINKCON) &&
          (GET_OBJ_TYPE(obj) != ITEM_FOUNTAIN) &&
          (GET_OBJ_TYPE(obj) != ITEM_CONTAINER))
    send_to_char(ch, "There's nothing inside that!\r\n");
  else {
    if (GET_OBJ_TYPE(obj) == ITEM_CONTAINER) {
      if (OBJVAL_FLAGGED(obj, CONT_CLOSED) && (GET_LEVEL(ch) < LVL_IMMORT || !PRF_FLAGGED(ch, PRF_NOHASSLE)))
        send_to_char(ch, "It is closed.\r\n");
      else {
        send_to_char(ch, "%s", fname(obj->name));
        switch (bits) {
          case FIND_OBJ_INV:
            send_to_char(ch, " (carried): \r\n");
            break;
          case FIND_OBJ_ROOM:
            send_to_char(ch, " (here): \r\n");
            break;
          case FIND_OBJ_EQUIP:
            send_to_char(ch, " (used): \r\n");
            break;
        }

        list_obj_to_char(obj->contains, ch, SHOW_OBJ_SHORT, TRUE, 0);
      }
    } else { /* item must be a fountain or drink container */
      if ((GET_OBJ_VAL(obj, 1) == 0) && (GET_OBJ_VAL(obj, 0) != -1))
        send_to_char(ch, "It is empty.\r\n");
      else {
        if (GET_OBJ_VAL(obj, 0) < 0) {
          char buf2[MAX_STRING_LENGTH];
          sprinttype(GET_OBJ_VAL(obj, 2), color_liquid, buf2, sizeof (buf2));
          send_to_char(ch, "It's full of a %s liquid.\r\n", buf2);
        } else if (GET_OBJ_VAL(obj, 1) > GET_OBJ_VAL(obj, 0))
          send_to_char(ch, "Its contents seem somewhat murky.\r\n"); /* BUG */
        else {
          char buf2[MAX_STRING_LENGTH];
          amt = (GET_OBJ_VAL(obj, 1) * 3) / GET_OBJ_VAL(obj, 0);
          sprinttype(GET_OBJ_VAL(obj, 2), color_liquid, buf2, sizeof (buf2));
          send_to_char(ch, "It's %sfull of a %s liquid.\r\n", fullness[amt], buf2);
        }
      }
    }
  }
}

char *find_exdesc(char *word, struct extra_descr_data *list) {
  struct extra_descr_data *i;

  for (i = list; i; i = i->next)
    if (*i->keyword == '.' ? isname(word, i->keyword + 1) : isname(word, i->keyword))
      return (i->description);

  return (NULL);
}

/* Given the argument "look at <target>", figure out what object or char
 * matches the target.  First, see if there is another char in the room with
 * the name.  Then check local objs for exdescs. Thanks to Angus Mezick for
 * the suggested fix to this problem. */
static void look_at_target(struct char_data *ch, char *arg) {
  int bits, found = FALSE, j, fnum, i = 0;
  struct char_data *found_char = NULL;
  struct obj_data *obj, *found_obj = NULL;
  char *desc;

  if (!ch->desc)
    return;

  if (!*arg) {
    send_to_char(ch, "Look at what?\r\n");
    return;
  }

  bits = generic_find(arg, FIND_OBJ_INV | FIND_OBJ_ROOM | FIND_OBJ_EQUIP |
          FIND_CHAR_ROOM, ch, &found_char, &found_obj);

  /* Is the target a character? */
  if (found_char != NULL) {
    look_at_char(found_char, ch);
    if (ch != found_char) {
      if (CAN_SEE(found_char, ch))
        act("$n looks at you.", TRUE, ch, 0, found_char, TO_VICT);
      act("$n looks at $N.", TRUE, ch, 0, found_char, TO_NOTVICT);
    }
    return;
  }

  /* Strip off "number." from 2.foo and friends. */
  if (!(fnum = get_number(&arg))) {
    send_to_char(ch, "Look at what?\r\n");
    return;
  }

  /* Does the argument match an extra desc in the room? */
  if ((desc = find_exdesc(arg, world[IN_ROOM(ch)].ex_description)) != NULL && ++i == fnum) {
    page_string(ch->desc, desc, FALSE);
    return;
  }

  /* Does the argument match an extra desc in the char's equipment? */
  for (j = 0; j < NUM_WEARS && !found; j++)
    if (GET_EQ(ch, j) && CAN_SEE_OBJ(ch, GET_EQ(ch, j)))
      if ((desc = find_exdesc(arg, GET_EQ(ch, j)->ex_description)) != NULL && ++i == fnum) {
        send_to_char(ch, "%s", desc);
        found = TRUE;
      }

  /* Does the argument match an extra desc in the char's inventory? */
  for (obj = ch->carrying; obj && !found; obj = obj->next_content) {
    if (CAN_SEE_OBJ(ch, obj))
      if ((desc = find_exdesc(arg, obj->ex_description)) != NULL && ++i == fnum) {
        send_to_char(ch, "%s", desc);
        found = TRUE;
      }
  }

  /* Does the argument match an extra desc of an object in the room? */
  for (obj = world[IN_ROOM(ch)].contents; obj && !found; obj = obj->next_content)
    if (CAN_SEE_OBJ(ch, obj))
      if ((desc = find_exdesc(arg, obj->ex_description)) != NULL && ++i == fnum) {
        send_to_char(ch, "%s", desc);
        found = TRUE;
      }

  /* If an object was found back in generic_find */
  if (bits) {
    if (!found)
      show_obj_to_char(found_obj, ch, SHOW_OBJ_ACTION, 0);
    else {
      show_obj_modifiers(found_obj, ch);
      send_to_char(ch, "\r\n");
    }
  } else if (!found)
    send_to_char(ch, "You do not see that here.\r\n");
}

ACMD(do_look) {
  struct obj_data *tmp_object;
  struct char_data *tmp_char;
  int look_type;
  int found = 0;

  if (!ch->desc)
    return;

  if (GET_POS(ch) < POS_SLEEPING)
    send_to_char(ch, "You can't see anything but stars!\r\n");
  else if (AFF_FLAGGED(ch, AFF_BLIND) && GET_LEVEL(ch) < LVL_IMMORT)
    send_to_char(ch, "You can't see a damned thing, you're blind!\r\n");
  else if (IS_DARK(IN_ROOM(ch)) && !CAN_SEE_IN_DARK(ch) && !CAN_INFRA_IN_DARK(ch)) {
    send_to_char(ch, "It is pitch black...\r\n");
    list_char_to_char(world[IN_ROOM(ch)].people, ch); /* glowing red eyes */
  } else {
    char arg[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];

    half_chop(argument, arg, arg2);

    if (subcmd == SCMD_READ) {
      if (!*arg)
        send_to_char(ch, "Read what?\r\n");
      else {
        generic_find(arg, FIND_OBJ_INV | FIND_OBJ_ROOM | FIND_CHAR_ROOM |
                FIND_OBJ_EQUIP, ch, &tmp_char, &tmp_object);
        if (tmp_object)
          look_at_target(ch, arg);
        else
          send_to_char(ch, "You can't read that.\r\n");
      }
      return;
    }
    if (!*arg) /* "look" alone, without an argument at all */
      look_at_room(ch, 1);
    else if (is_abbrev(arg, "in"))
      look_in_obj(ch, arg2);
      /* did the char type 'look <direction>?' */
    else if ((look_type = search_block(arg, dirs, FALSE)) >= 0)
      look_in_direction(ch, look_type);
    else if (is_abbrev(arg, "at"))
      look_at_target(ch, arg2);
    else if (is_abbrev(arg, "around")) {
      struct extra_descr_data *i;

      for (i = world[IN_ROOM(ch)].ex_description; i; i = i->next) {
        if (*i->keyword != '.') {
          send_to_char(ch, "%s%s:\r\n%s",
                  (found ? "\r\n" : ""), i->keyword, i->description);
          found = 1;
        }
      }
      if (!found)
        send_to_char(ch, "You couldn't find anything noticeable.\r\n");
    } else
      look_at_target(ch, arg);
  }
}

ACMD(do_examine) {
  struct char_data *tmp_char;
  struct obj_data *tmp_object;
  char tempsave[MAX_INPUT_LENGTH], arg[MAX_INPUT_LENGTH];

  one_argument(argument, arg);

  if (!*arg) {
    send_to_char(ch, "Examine what?\r\n");
    return;
  }

  /* look_at_target() eats the number. */
  look_at_target(ch, strcpy(tempsave, arg)); /* strcpy: OK */

  generic_find(arg, FIND_OBJ_INV | FIND_OBJ_ROOM | FIND_CHAR_ROOM |
          FIND_OBJ_EQUIP, ch, &tmp_char, &tmp_object);

  if (tmp_object) {
    if ((GET_OBJ_TYPE(tmp_object) == ITEM_DRINKCON) ||
            (GET_OBJ_TYPE(tmp_object) == ITEM_FOUNTAIN) ||
            (GET_OBJ_TYPE(tmp_object) == ITEM_CONTAINER)) {
      send_to_char(ch, "When you look inside, you see:\r\n");
      look_in_obj(ch, arg);
    }
  }
}

ACMD(do_gold) {
  if (GET_GOLD(ch) == 0)
    send_to_char(ch, "You're broke!\r\n");
  else if (GET_GOLD(ch) == 1)
    send_to_char(ch, "You have one miserable little gold coin.\r\n");
  else
    send_to_char(ch, "You have %d gold coins.\r\n", GET_GOLD(ch));
}

ACMD(do_innates) {
  int race = -1;

  send_to_char(ch, "\tmAbility\tn Innates:\r\n\r\n");
  send_to_char(ch, "\tMRacial:\tn\r\n");
  if (IS_NPC(ch) || IS_MORPHED(ch)) {
    if (IS_MORPHED(ch))
      race = IS_MORPHED(ch);
    else
      race = GET_RACE(ch);
    switch (race) {
      case NPCRACE_DRAGON:
        send_to_char(ch, "tailsweep\r\n");
        send_to_char(ch, "breathe\r\n");
        send_to_char(ch, "frightful\r\n");
        break;
      case NPCRACE_ANIMAL:
        send_to_char(ch, "rage\r\n");
        break;
      default:
        send_to_char(ch, "None (yet)\r\n");
        break;
    }
  } else { // PC
    switch (GET_RACE(ch)) {
      case RACE_ELF:
        send_to_char(ch, "elven dexterity (+2 dex)\r\n");
        send_to_char(ch, "\tRelven constitution (-2 con)\tn\r\n");
        send_to_char(ch, "basic weapon proficiency (free skill)\r\n");
        send_to_char(ch, "sleep enchantment immunity\r\n");
        send_to_char(ch, "infravision\r\n");
        send_to_char(ch, "keen senses (+2 listen/spot ability)\r\n");
        send_to_char(ch, "resistance to enchantments (+2 save bonus)\r\n");
        break;
      case RACE_DWARF:
        send_to_char(ch, "dwarven constitution (+2 con)\r\n");
        send_to_char(ch, "\tRdwarven charisma (-2 cha)\tn\r\n");
        send_to_char(ch, "infravision\r\n");
        send_to_char(ch, "poison resist (+2 poison save)\r\n");
        send_to_char(ch, "stability (+4 resist bash/trip)\r\n");
        send_to_char(ch, "spell hardiness (+2 spell save vs. "
                "damaging spells)\r\n");
        send_to_char(ch, "combat training versus giants "
                "(+1 size bonus vs. larger opponents)\r\n");
        break;
      case RACE_H_ELF:
        send_to_char(ch, "basic weapon proficiency (free skill)\r\n");
        send_to_char(ch, "infravision\r\n");
        send_to_char(ch, "resistance to enchantments (+2 save bbnus)\r\n");
        send_to_char(ch, "half-blood (+2 discipline/lore)");
        break;
      case RACE_H_ORC:
        send_to_char(ch, "half orc strength (+2 str)\r\n");
        send_to_char(ch, "\tRhalf orc charisma (-2 cha)\tn\r\n");
        send_to_char(ch, "\tRhalf orc intelligence (-2 int)\tn\r\n");
        send_to_char(ch, "ultravision\r\n");
        break;
      case RACE_HALFLING:
        send_to_char(ch, "halfling dexterity (+2 dex)\r\n");
        send_to_char(ch, "\tRhalfling strength (-2 str)\tn\r\n");
        send_to_char(ch, "infravision\r\n");
        send_to_char(ch, "shadow hopper (+2 sneak/hide)\r\n");
        send_to_char(ch, "lucky (+1 all saves)\r\n");
        send_to_char(ch, "infravision\r\n");
        send_to_char(ch, "combat training versus giants "
                "(+1 size bonus vs. larger opponents)\r\n");
        break;
      case RACE_GNOME:
        send_to_char(ch, "gnomish constitution (+2 con)\r\n");
        send_to_char(ch, "\tRgnomish strength (-2 str)\tn\r\n");
        send_to_char(ch, "illusion resist (+2 save bonus)\r\n");
        send_to_char(ch, "illusion affinity (+2 DC on illusions)\r\n");
        send_to_char(ch, "tinker focus (+2 concentration/listen)\r\n");
        send_to_char(ch, "infravision\r\n");
        send_to_char(ch, "combat training versus giants "
                "(+1 size bonus vs. larger opponents)\r\n");
        break;
      case RACE_HUMAN:
        send_to_char(ch, "diverse (+3 training sessions, 1st level)\r\n");
        send_to_char(ch, "quick learner (+1 training per level)\r\n");
        send_to_char(ch, "well trained (+1 practice session, 1st level)\r\n");
        break;
      case RACE_TRELUX:
        send_to_char(ch, "vital (start with +10 hps bonus)\r\n");
        send_to_char(ch, "hardy (+4 hps bonus per level)\r\n");
        send_to_char(ch, "trelux constitution (+4 con)\r\n");
        send_to_char(ch, "trelux strength (+2 str)\r\n");
        send_to_char(ch, "trelux dexterity (+8 dex)\r\n");
        send_to_char(ch, "trelux small size\r\n");
        send_to_char(ch, "ultravision\r\n");
        send_to_char(ch, "\tRvulnerable cold (20%%)\tn\r\n");
        send_to_char(ch, "resist everything else (20%%)\r\n");
        send_to_char(ch, "leap (help LEAP)\r\n");
        send_to_char(ch, "fly (help TRELUX-FLY)\r\n");
        send_to_char(ch, "\tRtrelux eq (help TRELUX-EQ)\tn\r\n");
        send_to_char(ch, "trelux exoskeleton (help TRELUX-EXOSKELETON)\r\n");
        send_to_char(ch, "trelux pincers (help TRELUX-PINCERS)\r\n");
        break;
      case RACE_CRYSTAL_DWARF:
        send_to_char(ch, "vital (start with +10 hps bonus)\r\n");
        send_to_char(ch, "hardy (+4 hps bonus per level)\r\n");
        send_to_char(ch, "resist acid (10%%)\r\n");
        send_to_char(ch, "resist puncture (10%%)\r\n");
        send_to_char(ch, "resist poison (10%%)\r\n");
        send_to_char(ch, "resist disease (10%%)\r\n");
        send_to_char(ch, "crystal dwarf constitution (+8 con)\r\n");
        send_to_char(ch, "crystal dwarf strength (+2 str)\r\n");
        send_to_char(ch, "crystal dwarf wisdom (+2 wis)\r\n");
        send_to_char(ch, "crystal dwarf charisma (+2 cha)\r\n");
        send_to_char(ch, "infravision\r\n");
        send_to_char(ch, "poison resist (+2 poison save)\r\n");
        send_to_char(ch, "stability (+4 resist bash/trip)\r\n");
        send_to_char(ch, "spell hardiness (+2 spell save)\r\n");
        send_to_char(ch, "crystalbody (help CRYSTALBODY)\r\n");
        send_to_char(ch, "crystalfist (help CRYSTALFIST)\r\n");
        send_to_char(ch, "combat training versus giants "
                "(+1 size bonus vs. larger opponents)\r\n");
        break;
      case RACE_TROLL:
        send_to_char(ch, "regeneration\r\n");
        send_to_char(ch, "\tRweakness to acid (25%%)\tn\r\n");
        send_to_char(ch, "\tRweakness to fire (50%%)\tn\r\n");
        send_to_char(ch, "resist poison (25%%)\r\n");
        send_to_char(ch, "resist disease (50%%)\r\n");
        send_to_char(ch, "troll constitution (+2 con)\r\n");
        send_to_char(ch, "troll strength (+2 str)\r\n");
        send_to_char(ch, "troll dexterity (+2 dex)\r\n");
        send_to_char(ch, "\tRtroll charisma (-4 cha)\tn\r\n");
        send_to_char(ch, "\tRtroll intelligence (-4 cha)\tn\r\n");
        send_to_char(ch, "\tRtroll wisdom (-4 cha)\tn\r\n");
        send_to_char(ch, "ultravision\r\n");
        break;
      default:
        send_to_char(ch, "No Racial (yet)\r\n");
        break;
    }

    /* other innates */
    send_to_char(ch, "\r\n");
    if (CLASS_LEVEL(ch, CLASS_BERSERKER) >= 4) {
      send_to_char(ch, "Berserker Innates:\r\n");
      send_to_char(ch, "berserker shrug (level / 4 damage reduction)\r\n");
    }
    /*
    if (CLASS_LEVEL(ch, CLASS_WIZARD)) {
      send_to_char(ch, "Wizard Innates:\r\n");
      send_to_char(ch, "\tGAcid Splash\tn\r\n");
      send_to_char(ch, "\tBRay of Frost\tn\r\n");
    }
     */
  }
}

void perform_affects(struct char_data *ch, struct char_data *k) {
  int i = 0;
  char buf[MAX_STRING_LENGTH] = {'\0'};
  struct affected_type *aff = NULL;
  struct mud_event_data *pMudEvent = NULL;

  send_to_char(ch,
          "\tC---------------------------------------------------------\tn\r\n");
  send_to_char(ch,
          "\tC-------------- \tWAffected By\tC ------------------------------\tn\r\n");

  /* Showing the bitvector */
  sprintbitarray(AFF_FLAGS(k), affected_bits, AF_ARRAY_MAX, buf);
  send_to_char(ch, "%s%s%s\r\n", CCYEL(ch, C_NRM),
          buf, CCNRM(ch, C_NRM));

  send_to_char(ch,
          "\tC-------------- \tWSpell-Like Affects\tC -----------------------\tn\r\n");

  /* Routine to show what spells a char is affected by */
  if (k->affected) {
    for (aff = k->affected; aff; aff = aff->next) {
      if (aff->duration + 1 >= 1200) // hours
        send_to_char(ch, "[%2d hour(s)  ] ", (int) ((aff->duration + 1) / 1200));
      else if (aff->duration + 1 >= 20) // minutes
        send_to_char(ch, "[%2d minute(s)] ", (int) ((aff->duration + 1) / 20));
      else // rounds
        send_to_char(ch, "[%2d round(s) ] ", (aff->duration + 1));
      send_to_char(ch, "%s%-19s%s ",
              CCCYN(ch, C_NRM), skill_name(aff->spell), CCNRM(ch, C_NRM));

      if (aff->modifier)
        send_to_char(ch, "%+d to %s", aff->modifier, apply_types[(int) aff->location]);

      if (aff->bitvector[0] || aff->bitvector[1] ||
              aff->bitvector[2] || aff->bitvector[3]) {
        if (aff->modifier)
          send_to_char(ch, ", ");
        for (i = 0; i < NUM_AFF_FLAGS; i++) {
          if (IS_SET_AR(aff->bitvector, i)) {
            send_to_char(ch, "sets %s, ", affected_bits[i]);
          }
        }
      }
      send_to_char(ch, "\r\n");
    }
  }
  send_to_char(ch,
          "\tC-------------- \tWCool Downs\tC -------------------------------\tn\r\n");
  if ((pMudEvent = char_has_mud_event(k, eTAUNT)))
    send_to_char(ch, "Taunt - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eRAGE)))
    send_to_char(ch, "Rage - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eCRYSTALFIST)))
    send_to_char(ch, "Crystal Fist - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eCRYSTALBODY)))
    send_to_char(ch, "Crystal Body - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eLAYONHANDS)))
    send_to_char(ch, "Lay on Hands - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eTREATINJURY)))
    send_to_char(ch, "Treat Injuries - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eMUMMYDUST)))
    send_to_char(ch, "Epic Spell:  Mummy Dust - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eDRAGONKNIGHT)))
    send_to_char(ch, "Epic Spell:  Dragon Knight - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eGREATERRUIN)))
    send_to_char(ch, "Epic Spell:  Greater Ruin - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eHELLBALL)))
    send_to_char(ch, "Epic Spell:  Hellball - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eEPICMAGEARMOR)))
    send_to_char(ch, "Epic Spell:  Epic Mage Armor - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eEPICWARDING)))
    send_to_char(ch, "Epic Spell:  Epic Warding - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eSTUNNINGFIST)))
    send_to_char(ch, "Stunning Fist - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eD_ROLL)))
    send_to_char(ch, "Defensive Roll - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, ePURIFY)))
    send_to_char(ch, "Purify - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eC_ANIMAL)))
    send_to_char(ch, "Call Companion - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eC_FAMILIAR)))
    send_to_char(ch, "Call Familiar - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eC_MOUNT)))
    send_to_char(ch, "Call Mount - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eSMITE)))
    send_to_char(ch, "Smite Evil - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, ePERFORM)))
    send_to_char(ch, "Perform - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eTURN_UNDEAD)))
    send_to_char(ch, "Turn Undead - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));

  send_to_char(ch,
          "\tC-------------- \tWOther\tC ------------------------------------\tn\r\n");
  if (CLASS_LEVEL(ch, CLASS_CLERIC) >= 14) {
    if (PLR_FLAGGED(ch, PLR_SALVATION)) {
      if (GET_SALVATION_NAME(ch) != NULL) {
        send_to_char(ch, "Salvation:  Set at %s\r\n", GET_SALVATION_NAME(ch));
      } else {
        send_to_char(ch, "Salvation:  Not set.\r\n");
      }
    } else {
      send_to_char(ch, "Salvation:  Not set.\r\n");
    }
  }

  if ((pMudEvent = char_has_mud_event(k, eTAUNTED)))
    send_to_char(ch, "\tRTaunted!\tn - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eSTUNNED)))
    send_to_char(ch, "\tRStunned!\tn - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eACIDARROW)))
    send_to_char(ch, "\tRAcid Arrow!\tn - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));
  if ((pMudEvent = char_has_mud_event(k, eIMPLODE)))
    send_to_char(ch, "\tRImplode!\tn - Duration: %d seconds\r\n", (int) (event_time(pMudEvent->pEvent) / 10));

  //location of our DAM_x  damtypes
  send_to_char(ch,
          "\tC-------- \tWDamage Type Resistance / Vulnerability\tC ---------\tn\r\n");
  for (i = 0; i < NUM_DAM_TYPES - 1; i++) {
    send_to_char(ch, "%-15s: %-4d%% (%-2d)   ", damtype_display[i + 1],
            compute_damtype_reduction(k, i + 1), compute_energy_absorb(k, i + 1));
    if (i % 2)
      send_to_char(ch, "\r\n");
  }
  send_to_char(ch, "\r\n");
  send_to_char(ch,
          "\tC---------------------------------------------------------\tn\r\n");
}

/* compartmentalized affects, so wizard command (stat affect)
 *  and this can share */
ACMD(do_affects) {

  perform_affects(ch, ch);

}

ACMD(do_attacks) {
  perform_attacks(ch, 2);
}

ACMD(do_score) {
  char buf[MAX_INPUT_LENGTH];
  struct time_info_data playing_time;
  int calc_bab = MIN(MAX_BAB, BAB(ch)), w_type, i = 0, counter = 0;
  struct obj_data *wielded = GET_EQ(ch, WEAR_WIELD_1);
  float height = GET_HEIGHT(ch);

  // get some initial info before score display
  if (wielded && GET_OBJ_TYPE(wielded) == ITEM_WEAPON)
    w_type = GET_OBJ_VAL(wielded, 3) + TYPE_HIT;
  else {
    if (IS_NPC(ch) && ch->mob_specials.attack_type != 0)
      w_type = ch->mob_specials.attack_type + TYPE_HIT;
    else
      w_type = TYPE_HIT;
  }
  playing_time = *real_time_passed((time(0) - ch->player.time.logon) +
          ch->player.time.played, 0);
  height *= 0.393700787402;

  //score display
  send_to_char(ch, "                \t[f451]Score information for\tn %s\r\n",
          GET_NAME(ch));
  send_to_char(ch,
          "\tC=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\tn\r\n");

  send_to_char(ch, "\tCTitle:\tn %s\r\n", GET_TITLE(ch) ? GET_TITLE(ch) : "None.");

  *buf = '\0';
  if (!IS_NPC(ch)) {
    for (i = 0; i < MAX_CLASSES; i++) {
      if (CLASS_LEVEL(ch, i)) {
        if (counter)
          strcat(buf, " / ");
        sprintf(buf, "%s%d %s", buf, CLASS_LEVEL(ch, i), class_abbrevs[i]);
        counter++;
      }
    }
  } else
    strcpy(buf, CLASS_ABBR(ch));
  send_to_char(ch, "\tCClass%s:\tn %s", (counter == 1 ? "" : "es"), buf);

  send_to_char(ch, "\r\n\tCLevel:\tn %d  \tCRace:\tn %s "
          "(\tDType 'innates'\tn)  \tCSex:\tn ",
          GET_LEVEL(ch), RACE_ABBR(ch));
  switch (GET_SEX(ch)) {
    case SEX_MALE: send_to_char(ch, "Male\r\n");
      break;
    case SEX_FEMALE: send_to_char(ch, "Female\r\n");
      break;
    default: send_to_char(ch, "Neutral\r\n");
      break;
  }

  send_to_char(ch, "\tCPlaying time:\tn %d \tCdays /\tn %d \tChours\tn\r\n",
          playing_time.day, playing_time.hours);

  send_to_char(ch, "\tCHit points:\tn %d(%d)   \tCMoves:\tn %d(%d)   \tCMana:\tn %d(%d)\r\n",
          GET_HIT(ch), GET_MAX_HIT(ch), GET_MOVE(ch), GET_MAX_MOVE(ch),
          GET_MANA(ch), GET_MAX_MANA(ch));

  // with sizes, this seems unnecessary now
  /*
     \tCHeight:\tn %d'%d\"  \tCWeight:\tn %d \tClbs\tn\r\n",
                (int)(height / 12.0),
               ((int)(height) % 12), GET_WEIGHT(ch));
   */

  send_to_char(ch, "\tCAge:\tn %d \tCyrs\tn / %d \tCmths\tn",
          age(ch)->year, age(ch)->month);
  send_to_char(ch, "    \tCCharacter Size Category:\tn  %s\r\n",
          size_names[GET_SIZE(ch)]);

  send_to_char(ch,
          "\tC---------------------------------------------------------\tn\r\n");

  send_to_char(ch, "\tCStr:\tn %2d[%2d]      \tCInt:\tn %2d[%2d]      \tCWis:\tn %2d[%2d]\r\n"
          "\tCDex:\tn %2d[%2d]      \tCCon:\tn %2d[%2d]      \tCCha:\tn %2d[%2d]\r\n",
          GET_STR(ch), GET_STR_BONUS(ch), GET_INT(ch), GET_INT_BONUS(ch), GET_WIS(ch),
          GET_WIS_BONUS(ch), GET_DEX(ch), GET_DEX_BONUS(ch), GET_CON(ch), GET_CON_BONUS(ch),
          GET_CHA(ch), GET_CHA_BONUS(ch));

  send_to_char(ch, "\tCArmorClass:\tn %d   \tCBAB:\tn %d   \tCSpell Resist:\tn %d\r\n",
          compute_armor_class(NULL, ch), calc_bab,
          compute_spell_res(NULL, ch, 0));

  send_to_char(ch, "\tCHitroll:\tn %d      \tCDamroll:\tn %d      \tC# of Attacks:\tn %d\r\n",
          compute_bab(ch, NULL, w_type) - calc_bab,
          compute_damage_bonus(ch, NULL, w_type, 0, 0),
          perform_attacks(ch, 1));

  send_to_char(ch, "\tCDamage Reduction:\tn %d      \tCConcealment:\tn %d\r\n",
          compute_damage_reduction(ch, -1),
          compute_concealment(ch));

  send_to_char(ch, "\tCAlignment:\tn %s", get_align_by_num(GET_ALIGNMENT(ch)));
  send_to_char(ch, "        \tCLoad carried:\tn %d \tClbs\tn\r\n", IS_CARRYING_W(ch));

  send_to_char(ch,
          "\tCSaving Throws:  Fortitude[\tn%d\tC] Reflex[\tn%d\tC] Will[\tn%d\tC]\tn\r\n",
          compute_mag_saves(ch, SAVING_FORT, 0),
          compute_mag_saves(ch, SAVING_REFL, 0),
          compute_mag_saves(ch, SAVING_WILL, 0));

  send_to_char(ch,
          "\tC---------------------------------------------------------\tn\r\n");

  if (!IS_NPC(ch))
    send_to_char(ch, "\tCWimpy:\tn %d  ", GET_WIMP_LEV(ch));
  send_to_char(ch, "\tCDivLvl:\tn %d  \tCMgcLvl:\tn %d"
          "  \tCCstrLvl:\tn %d\r\n",
          DIVINE_LEVEL(ch), MAGIC_LEVEL(ch), CASTER_LEVEL(ch));

  send_to_char(ch, "\tCExp:\tn %d   \tCExpTNL:\tn: ", GET_EXP(ch));
  if (GET_LEVEL(ch) >= LVL_IMMORT)
    send_to_char(ch, "N/A   ");
  else
    send_to_char(ch, "%d   ",
          level_exp(ch, GET_LEVEL(ch) + 1) - GET_EXP(ch));

  send_to_char(ch, "\tCStatus:\tn ");
  if (FIGHTING(ch))
    send_to_char(ch, "(Fighting) - ");
  switch (GET_POS(ch)) {
    case POS_DEAD: send_to_char(ch, "Dead\r\n");
      break;
    case POS_MORTALLYW: send_to_char(ch, "Mortally wounded\r\n");
      break;
    case POS_INCAP: send_to_char(ch, "Incapacitated\r\n");
      break;
    case POS_STUNNED: send_to_char(ch, "Stunned\r\n");
      break;
    case POS_SLEEPING: send_to_char(ch, "Sleeping\r\n");
      break;
    case POS_RESTING: send_to_char(ch, "Resting\r\n");
      break;
    case POS_SITTING:
      if (!SITTING(ch))
        send_to_char(ch, "Sitting\r\n");
      else {
        struct obj_data *furniture = SITTING(ch);
        send_to_char(ch, "Sitting upon %s.\r\n", furniture->short_description);
      }
      break;
    case POS_FIGHTING: send_to_char(ch, "Fighting\r\n");
      break;
    case POS_STANDING: send_to_char(ch, "Standing\r\n");
      break;
    default: send_to_char(ch, "Floating\r\n");
      break;
  }

  if (!IS_NPC(ch)) {
    send_to_char(ch, "\tCQuests completed:\tn %d   \tCQuestPoints:\tn %d   \tCOn Quest:\tn",
            GET_NUM_QUESTS(ch), GET_QUESTPOINTS(ch));
    if (GET_QUEST(ch) == NOTHING)
      send_to_char(ch, " None\r\n");
    else
      send_to_char(ch, " %d\r\n",
            GET_QUEST(ch) == NOTHING ? -1 : GET_QUEST(ch));

    if (GET_AUTOCQUEST_VNUM(ch))
      send_to_char(ch, "\tCOn Crafting Job: (%d) %s, using: %s.\r\n",
            GET_AUTOCQUEST_MAKENUM(ch), GET_AUTOCQUEST_DESC(ch),
            material_name[GET_AUTOCQUEST_MATERIAL(ch)]);
  }

  send_to_char(ch, "\tYGold:\tn %d            \tYGold in Bank:\tn %d\r\n",
          GET_GOLD(ch), GET_BANK_GOLD(ch));

  if (GET_LEVEL(ch) >= LVL_IMMORT) {
    if (POOFIN(ch))
      send_to_char(ch, "%sPOOFIN:  %s%s %s%s\r\n", QYEL, QCYN, GET_NAME(ch), POOFIN(ch), QNRM);
    else
      send_to_char(ch, "%sPOOFIN:  %s%s appears with an ear-splitting bang.%s\r\n", QYEL, QCYN, GET_NAME(ch), QNRM);
    if (POOFOUT(ch))
      send_to_char(ch, "%sPOOFOUT: %s%s %s%s\r\n", QYEL, QCYN, GET_NAME(ch), POOFOUT(ch), QNRM);
    else
      send_to_char(ch, "%sPOOFOUT: %s%s disappears in a puff of smoke.%s\r\n", QYEL, QCYN, GET_NAME(ch), QNRM);
    send_to_char(ch, "\tyYour current zone:\tn %s%d%s\r\n", CCCYN(ch, C_NRM), GET_OLC_ZONE(ch), CCNRM(ch, C_NRM));
  }

  if (!IS_NPC(ch)) {
    if (GET_COND(ch, DRUNK) > 10)
      send_to_char(ch, "You are intoxicated.\r\n");
    if (GET_COND(ch, HUNGER) == 0)
      send_to_char(ch, "You are hungry.\r\n");
    if (GET_COND(ch, THIRST) == 0)
      send_to_char(ch, "You are thirsty.\r\n");
    if (PRF_FLAGGED(ch, PRF_SUMMONABLE))
      send_to_char(ch, "\r\nYou are summonable by other players.\r\n");
  }
  send_to_char(ch,
          "\tC=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\tn\r\n");
  send_to_char(ch, "\tDType 'attacks' to see your attack rotation\tn\r\n");
  send_to_char(ch, "\tDType 'affects' to see what you are affected by\tn\r\n");
  if (CLASS_LEVEL(ch, CLASS_WIZARD))
    send_to_char(ch, "\tDType 'memorize' to see your spell interface\tn\r\n");
  if (CLASS_LEVEL(ch, CLASS_SORCERER))
    send_to_char(ch, "\tDType 'meditate' to see your spell interface\tn\r\n");
  if (CLASS_LEVEL(ch, CLASS_CLERIC))
    send_to_char(ch, "\tDType 'prayer' to see your spell interface\tn\r\n");
  if (CLASS_LEVEL(ch, CLASS_RANGER))
    send_to_char(ch, "\tDType 'adjure' to see your spell interface\tn\r\n");
  if (CLASS_LEVEL(ch, CLASS_BARD))
    send_to_char(ch, "\tDType 'compose' to see your spell interface\tn\r\n");
  if (CLASS_LEVEL(ch, CLASS_DRUID))
    send_to_char(ch, "\tDType 'commune' to see your spell interface\tn\r\n");
  if (CLASS_LEVEL(ch, CLASS_PALADIN))
    send_to_char(ch, "\tDType 'chant' to see your spell interface\tn\r\n");
}

ACMD(do_inventory) {
  send_to_char(ch, "You are carrying:\r\n");
  list_obj_to_char(ch->carrying, ch, SHOW_OBJ_SHORT, TRUE, 1);
  
  if (ch->desc->pProtocol->pVariables[eMSDP_MXP]->ValueInt)
    send_to_char(ch, "\r\n\t<send href='equipment'>View equipped items\t</send>\r\n");
}

ACMD(do_equipment) {
  int i, found = 0;
  int mxp_type = 2;

  send_to_char(ch, "You are using:\r\n");
  for (i = 0; i < NUM_WEARS; i++) {
    if (GET_EQ(ch, i)) {
      found = TRUE;
      if (CAN_SEE_OBJ(ch, GET_EQ(ch, i))) {
        send_to_char(ch, "%s", wear_where[i]);
        show_obj_to_char(GET_EQ(ch, i), ch, SHOW_OBJ_SHORT, mxp_type); 
      } else {
        send_to_char(ch, "%s", wear_where[i]);
        send_to_char(ch, "Something.\r\n");
      }
    }
  }
  if (!found)
    send_to_char(ch, " Nothing.\r\n");
  
  if (ch->desc->pProtocol->pVariables[eMSDP_MXP]->ValueInt)
    send_to_char(ch, "\r\n\t<send href='inventory'>View inventory\t</send>\r\n");
}

ACMD(do_time) {
  const char *suf;
  int weekday, day;

  /* day in [1..35] */
  day = time_info.day + 1;

  /* 35 days in a month, 7 days a week */
  weekday = ((35 * time_info.month) + day) % 7;

  send_to_char(ch, "It is %d o'clock %s, on %s.\r\n",
          (time_info.hours % 12 == 0) ? 12 : (time_info.hours % 12),
          time_info.hours >= 12 ? "pm" : "am", weekdays[weekday]);

  /* Peter Ajamian supplied the following as a fix for a bug introduced in the
   * ordinal display that caused 11, 12, and 13 to be incorrectly displayed as
   * 11st, 12nd, and 13rd.  Nate Winters had already submitted a fix, but it
   * hard-coded a limit on ordinal display which I want to avoid. -dak */
  suf = "th";

  if (((day % 100) / 10) != 1) {
    switch (day % 10) {
      case 1:
        suf = "st";
        break;
      case 2:
        suf = "nd";
        break;
      case 3:
        suf = "rd";
        break;
    }
  }

  send_to_char(ch, "The %d%s Day of the %s, Year %d.\r\n",
          day, suf, month_name[time_info.month], time_info.year);
}

ACMD(do_weather) {
  const char *sky_look[] = {
    "cloudless",
    "cloudy",
    "rainy",
    "lit by flashes of lightning"
  };

  if (OUTSIDE(ch)) {
    send_to_char(ch, "The sky is %s and %s.\r\n", sky_look[weather_info.sky],
            weather_info.change >= 0 ? "you feel a warm wind from south" :
            "your foot tells you bad weather is due");
    if (GET_LEVEL(ch) >= LVL_GOD)
      send_to_char(ch, "Pressure: %d (change: %d), Sky: %d (%s)\r\n",
            weather_info.pressure,
            weather_info.change,
            weather_info.sky,
            sky_look[weather_info.sky]);
  } else
    send_to_char(ch, "You have no feeling about the weather at all.\r\n");
}

/* puts -'s instead of spaces */
void space_to_minus(char *str) {
  while ((str = strchr(str, ' ')) != NULL)
    *str = '-';
}

int search_help(const char *argument, int level) {
  int chk, bot, top, mid, minlen;

  bot = 0;
  top = top_of_helpt;
  minlen = strlen(argument);

  while (bot <= top) {
    mid = (bot + top) / 2;

    if (!(chk = strn_cmp(argument, help_table[mid].keywords, minlen))) {
      while ((mid > 0) && !strn_cmp(argument, help_table[mid - 1].keywords, minlen))
        mid--;

      while (level < help_table[mid].min_level && mid < (bot + top) / 2)
        mid++;

      /* The following line was commented out at some point, by someone, for some reason...
       * as I am unaware of that reason, and without a level check all help files, including
       * the ones we may not want a player to see, are available. So I'm reversing this now.
       * -Vat */
      if (strn_cmp(argument, help_table[mid].keywords, minlen) ||
              level < help_table[mid].min_level)
        /*if (strn_cmp(argument, help_table[mid].keywords, minlen))*/
        break;

      return mid;
    } else if (chk > 0)
      bot = mid + 1;
    else
      top = mid - 1;
  }
  return NOWHERE;
}


/* this is used for char creation help */

/* make sure arg doesn't have spaces */
void perform_help(struct descriptor_data *d, char *argument) {
  int mid = 0;

  if (!help_table)
    return;

  if (!*argument)
    return;

  if ((mid = search_help(argument, LVL_IMPL)) == NOWHERE)
    return;

  page_string(d, help_table[mid].entry, 0);
}

ACMD(do_help) {
  int mid = 0;
  int i, found = 0;

  if (!ch->desc)
    return;

  skip_spaces(&argument);

  if (!help_table) {
    send_to_char(ch, "No help available.\r\n");
    return;
  }

  if (!*argument) {
    if (GET_LEVEL(ch) < LVL_IMMORT)
      page_string(ch->desc, help, 0);
    else
      page_string(ch->desc, ihelp, 0);
    return;
  }

  space_to_minus(argument);

  if ((mid = search_help(argument, GET_LEVEL(ch))) == NOWHERE) {
    send_to_char(ch, "There is no help on that word.\r\n");
    mudlog(NRM, MAX(LVL_IMPL, GET_INVIS_LEV(ch)), TRUE,
            "%s tried to get help on %s", GET_NAME(ch), argument);
    for (i = 0; i < top_of_helpt; i++) {
      if (help_table[i].min_level > GET_LEVEL(ch))
        continue;
      /* To help narrow down results, if they don't start with the same letters, move on. */
      if (*argument != *help_table[i].keywords)
        continue;
      if (levenshtein_distance(argument, help_table[i].keywords) <= 2) {
        if (!found) {
          send_to_char(ch, "\r\nDid you mean:\r\n");
          found = 1;
        }
        send_to_char(ch, "  \t<send href=\"Help %s\">%s\t</send>\r\n",
                help_table[i].keywords, help_table[i].keywords);
      }
    }
    send_to_char(ch, "\tDYou can also check the help index, type 'hindex <keyword>'\tn\r\n");
    return;
  }
  page_string(ch->desc, help_table[mid].entry, 0);
  send_to_char(ch, "\tDYou can also check the help index, type 'hindex <keyword>'\tn\r\n");
}


#define WHO_FORMAT \
"Usage: who [minlev[-maxlev]] [-n name] [-c classlist] [-t racelist] [-k] [-l] [-n] [-q] [-r] [-s] [-z]\r\n"

/* Written by Rhade */
ACMD(do_who) {
  struct descriptor_data *d;
  struct char_data *tch;
  int i, num_can_see = 0;
  char name_search[MAX_INPUT_LENGTH], buf[MAX_INPUT_LENGTH];
  char mode;
  int low = 0, high = LVL_IMPL, localwho = 0, questwho = 0;
  int showclass = 0, short_list = 0, outlaws = 0;
  int who_room = 0, showgroup = 0, showleader = 0;
  int showrace = 0;
  int mortals = 0, staff = 0;
  clan_rnum c_n;

  struct {
    char *disp;
    int min_level;
    int max_level;
    int count; /* must always start as 0 */
  } rank[] = {
    { "\tb--\tB= \tCLuminari Staff \tB=\tb--\tn\r\n\tc-=-=-=-=-=-=-=-=-=-=-=-\tn\r\n", LVL_IMMORT, LVL_IMPL, 0},
    { "\tb--\tB=\tC Mortals \tB=\tb--\tn\r\n\tc-=-=-=-=-=-=-=-=-=-=-=-\tn\r\n", 1, LVL_IMMORT - 1, 0},
    { "\n", 0, 0, 0}
  };

  // remove spaces in front of argument
  skip_spaces(&argument);
  // copy argument -> buf
  strcpy(buf, argument); /* strcpy: OK (sizeof: argument == buf) */
  // first char of name_search is now NULL
  name_search[0] = '\0';

  // move along the buf array until '\0'
  while (*buf) {
    char arg[MAX_INPUT_LENGTH], buf1[MAX_INPUT_LENGTH];

    // take buf, first delimit in arg, rest in buf1
    half_chop(buf, arg, buf1);
    // if arg is a digit...
    if (isdigit(*arg)) {
      sscanf(arg, "%d-%d", &low, &high);
      strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */

      // arg isn't a digit, only acceptable input is '-' and a letter
    } else if (*arg == '-') {
      mode = *(arg + 1); /* just in case; we destroy arg in the switch */
      switch (mode) {
        case 'k':
          outlaws = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 'z':
          localwho = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 's':
          short_list = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 'q':
          questwho = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 'n':
          half_chop(buf1, name_search, buf);
          break;
        case 'r':
          who_room = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 'c':
          half_chop(buf1, arg, buf);
          showclass = find_class_bitvector(arg);
          break;
        case 'l':
          showleader = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 'g':
          showgroup = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 't':
          half_chop(buf1, arg, buf);
          showrace = find_race_bitvector(arg);
          break;
        default:
          send_to_char(ch, "%s", WHO_FORMAT);
          return;
      }
    } else {
      send_to_char(ch, "%s", WHO_FORMAT);
      return;
    }
  }

  // first counting the "ranks" which will display how many chars are viewed with do_who call
  for (d = descriptor_list; d && !short_list; d = d->next) {
    if (d->original) // if !switched
      tch = d->original;
    else if (!(tch = d->character)) // if switched, make sure d->character
      continue;

    if (CAN_SEE(ch, tch) && IS_PLAYING(d)) {
      if (*name_search && str_cmp(GET_NAME(tch), name_search) &&
              !strstr(GET_TITLE(tch), name_search))
        continue;
      if (!CAN_SEE(ch, tch) || GET_LEVEL(tch) < low || GET_LEVEL(tch) > high)
        continue;
      if (outlaws && !PLR_FLAGGED(tch, PLR_KILLER) && !PLR_FLAGGED(tch, PLR_THIEF))
        continue;
      if (questwho && !PRF_FLAGGED(tch, PRF_QUEST))
        continue;
      if (localwho && world[IN_ROOM(ch)].zone != world[IN_ROOM(tch)].zone)
        continue;
      if (who_room && (IN_ROOM(tch) != IN_ROOM(ch)))
        continue;
      if (showclass && !(showclass & (1 << GET_CLASS(tch))))
        continue;
      if (showrace && !(showrace & (1 << GET_RACE(tch))))
        continue;
      if (showgroup && !GROUP(tch))
        continue;
      if (showleader && (!GROUP(tch) || GROUP_LEADER(GROUP(tch)) != tch))
        continue;

      // our struct above, we are adjusting the count value based on players found
      for (i = 0; *rank[i].disp != '\n'; i++)
        if (GET_LEVEL(tch) >= rank[i].min_level && GET_LEVEL(tch) <= rank[i].max_level)
          rank[i].count++;
    }
  }

  for (i = 0; *rank[i].disp != '\n'; i++) {
    // go through list of ranks, don't continue if this rank has no players
    if (!rank[i].count && !short_list)
      continue;

    // display top of who list
    if (short_list)
      send_to_char(ch, "Players\r\n-------\r\n");
    else
      send_to_char(ch, "%s", rank[i].disp);

    for (d = descriptor_list; d; d = d->next) {
      if (d->original)
        tch = d->original;
      else if (!(tch = d->character))
        continue;

      if ((GET_LEVEL(tch) < rank[i].min_level || GET_LEVEL(tch) > rank[i].max_level) && !short_list)
        continue;
      if (!IS_PLAYING(d))
        continue;
      if (*name_search && str_cmp(GET_NAME(tch), name_search) &&
              !strstr(GET_TITLE(tch), name_search))
        continue;
      if (!CAN_SEE(ch, tch) || GET_LEVEL(tch) < low || GET_LEVEL(tch) > high)
        continue;
      if (outlaws && !PLR_FLAGGED(tch, PLR_KILLER) && !PLR_FLAGGED(tch, PLR_THIEF))
        continue;
      if (questwho && !PRF_FLAGGED(tch, PRF_QUEST))
        continue;
      if (localwho && world[IN_ROOM(ch)].zone != world[IN_ROOM(tch)].zone)
        continue;
      if (who_room && (IN_ROOM(tch) != IN_ROOM(ch)))
        continue;
      if (showclass && !(showclass & (1 << GET_CLASS(tch))))
        continue;
      if (showrace && !(showrace & (1 << GET_RACE(tch))))
        continue;
      if (showgroup && !GROUP(tch))
        continue;
      if (showleader && (!GROUP(tch) || GROUP_LEADER(GROUP(tch)) != tch))
        continue;

      if (short_list) {
        send_to_char(ch, "[%2d %8s] %-12.12s%s%s",
                GET_LEVEL(tch), RACE_ABBR(tch), GET_NAME(tch),
                CCNRM(ch, C_SPR), ((!(++num_can_see % 4)) ? "\r\n" : ""));
      } else {
        //num_can_see++;
        if (GET_LEVEL(tch) >= LVL_IMMORT) {
          staff++;
          send_to_char(ch, "%13s", admin_level_names[(GET_LEVEL(tch) - LVL_IMMORT)]);
        } else {
          mortals++;
          send_to_char(ch, "[%2d %8s ",
                  GET_LEVEL(tch), RACE_ABBR(tch));
        }

        if (GET_LEVEL(tch) < LVL_IMMORT) {
          int inc, classCount = 0;
          for (inc = 0; inc < MAX_CLASSES; inc++) {
            if (CLASS_LEVEL(tch, inc)) {
              if (classCount)
                send_to_char(ch, "/");
              send_to_char(ch, "%s", class_abbrevs[inc]);
              classCount++;
            }
          }
          send_to_char(ch, "]");
        }

        send_to_char(ch, " %s%s%s%s",
                GET_NAME(tch), (*GET_TITLE(tch) ? " " : ""), GET_TITLE(tch),
                CCNRM(ch, C_SPR));

        if (IS_IN_CLAN(tch) && !(GET_CLANRANK(tch) == NO_CLANRANK)) {
          c_n = real_clan(GET_CLAN(tch));
          send_to_char(ch, " %s[%s%s%s]%s", QBRED, QBYEL, clan_list[c_n].abrev ? CLAN_ABREV(c_n) : "Unkown", QBRED, QNRM);
        }
        if (GET_INVIS_LEV(tch))
          send_to_char(ch, " (i%d)", GET_INVIS_LEV(tch));
        else if (AFF_FLAGGED(tch, AFF_INVISIBLE))
          send_to_char(ch, " (invis)");

        if (PLR_FLAGGED(tch, PLR_MAILING))
          send_to_char(ch, " (mailing)");
        else if (d->olc)
          send_to_char(ch, " (OLC)");
        else if (PLR_FLAGGED(tch, PLR_WRITING))
          send_to_char(ch, " (writing)");

        if (d->original)
          send_to_char(ch, " (out of body)");

        if (d->connected == CON_OEDIT)
          send_to_char(ch, " (Object Edit)");
        if (d->connected == CON_MEDIT)
          send_to_char(ch, " (Mobile Edit)");
        if (d->connected == CON_ZEDIT)
          send_to_char(ch, " (Zone Edit)");
        if (d->connected == CON_SEDIT)
          send_to_char(ch, " (Shop Edit)");
        if (d->connected == CON_REDIT)
          send_to_char(ch, " (Room Edit)");
        if (d->connected == CON_TEDIT)
          send_to_char(ch, " (Text Edit)");
        if (d->connected == CON_TRIGEDIT)
          send_to_char(ch, " (Trigger Edit)");
        if (d->connected == CON_AEDIT)
          send_to_char(ch, " (Social Edit)");
        if (d->connected == CON_CEDIT)
          send_to_char(ch, " (Configuration Edit)");
        if (d->connected == CON_HEDIT)
          send_to_char(ch, " (Help edit)");
        if (d->connected == CON_QEDIT)
          send_to_char(ch, " (Quest Edit)");
        if (d->connected == CON_STUDY)
          send_to_char(ch, " (Spells Known Editor)");
        if (PRF_FLAGGED(tch, PRF_BUILDWALK))
          send_to_char(ch, " (Buildwalking)");
        if (PRF_FLAGGED(tch, PRF_AFK))
          send_to_char(ch, " (AFK)");
        if (PRF_FLAGGED(tch, PRF_NOGOSS))
          send_to_char(ch, " (nogos)");
        if (PRF_FLAGGED(tch, PRF_NOWIZ))
          send_to_char(ch, " (nowiz)");
        if (PRF_FLAGGED(tch, PRF_NOSHOUT))
          send_to_char(ch, " (noshout)");
        if (PRF_FLAGGED(tch, PRF_NOTELL))
          send_to_char(ch, " (notell)");
        if (PRF_FLAGGED(tch, PRF_QUEST))
          send_to_char(ch, " (quest)");
        if (PLR_FLAGGED(tch, PLR_THIEF))
          send_to_char(ch, " (THIEF)");
        if (PLR_FLAGGED(tch, PLR_KILLER))
          send_to_char(ch, " (KILLER)");
        send_to_char(ch, "\r\n");
      }
    }
    send_to_char(ch, "\r\n");
    if (short_list)
      break;
  }
  if (short_list && num_can_see % 4)
    send_to_char(ch, "\r\n");
  //if (!num_can_see)
  //  send_to_char(ch, "Nobody at all!\r\n");
  //else if (num_can_see == 1)
  //  send_to_char(ch, "One lonely character displayed.\r\n");
  //else
  //  send_to_char(ch, "%d characters displayed.\r\n", num_can_see);
  send_to_char(ch, "Total visible players: %d.\r\n", mortals + staff);
  if ((mortals + staff) > boot_high)
    boot_high = mortals + staff;
  send_to_char(ch, "Maximum visible players this boot: %d.\r\n", boot_high);
  
  if (IS_HAPPYHOUR > 0) {
    send_to_char(ch, "\tWIt's a Happy Hour! Type \tRhappyhour\tW to see the current bonuses.\tn\r\n");
  }
}

#define USERS_FORMAT \
"format: users [-l minlevel[-maxlevel]] [-n name] [-h host] [-c classlist] [-o] [-p]\r\n"

ACMD(do_users) {
  char line[200], line2[220], idletime[10], classname[20];
  char state[30], *timeptr, mode;
  char name_search[MAX_INPUT_LENGTH], host_search[MAX_INPUT_LENGTH];
  struct char_data *tch;
  struct descriptor_data *d;
  int low = 0, high = LVL_IMPL, num_can_see = 0;
  int showclass = 0, outlaws = 0, playing = 0, deadweight = 0;
  char buf[MAX_INPUT_LENGTH], arg[MAX_INPUT_LENGTH];

  host_search[0] = name_search[0] = '\0';

  strcpy(buf, argument); /* strcpy: OK (sizeof: argument == buf) */
  while (*buf) {
    char buf1[MAX_INPUT_LENGTH];

    half_chop(buf, arg, buf1);
    if (*arg == '-') {
      mode = *(arg + 1); /* just in case; we destroy arg in the switch */
      switch (mode) {
        case 'o':
        case 'k':
          outlaws = 1;
          playing = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 'p':
          playing = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 'd':
          deadweight = 1;
          strcpy(buf, buf1); /* strcpy: OK (sizeof: buf1 == buf) */
          break;
        case 'l':
          playing = 1;
          half_chop(buf1, arg, buf);
          sscanf(arg, "%d-%d", &low, &high);
          break;
        case 'n':
          playing = 1;
          half_chop(buf1, name_search, buf);
          break;
        case 'h':
          playing = 1;
          half_chop(buf1, host_search, buf);
          break;
        case 'c':
          playing = 1;
          half_chop(buf1, arg, buf);
          showclass = find_class_bitvector(arg);
          break;
        default:
          send_to_char(ch, "%s", USERS_FORMAT);
          return;
      } /* end of switch */

    } else { /* endif */
      send_to_char(ch, "%s", USERS_FORMAT);
      return;
    }
  } /* end while (parser) */
  send_to_char(ch,
          "Num Class    Name         State          Idl   Login\t*   Site\r\n"
          "--- -------- ------------ -------------- ----- -------- ------------------------\r\n");

  one_argument(argument, arg);

  for (d = descriptor_list; d; d = d->next) {
    if (STATE(d) != CON_PLAYING && playing)
      continue;
    if (STATE(d) == CON_PLAYING && deadweight)
      continue;
    if (IS_PLAYING(d)) {
      if (d->original)
        tch = d->original;
      else if (!(tch = d->character))
        continue;

      if (*host_search && !strstr(d->host, host_search))
        continue;
      if (*name_search && str_cmp(GET_NAME(tch), name_search))
        continue;
      if (!CAN_SEE(ch, tch) || GET_LEVEL(tch) < low || GET_LEVEL(tch) > high)
        continue;
      if (outlaws && !PLR_FLAGGED(tch, PLR_KILLER) &&
              !PLR_FLAGGED(tch, PLR_THIEF))
        continue;
      if (showclass && !(showclass & (1 << GET_CLASS(tch))))
        continue;
      if (GET_INVIS_LEV(tch) > GET_LEVEL(ch))
        continue;

      if (d->original)
        sprintf(classname, "[%2d %s]", GET_LEVEL(d->original),
              CLASS_ABBR(d->original));
      else
        sprintf(classname, "[%2d %s]", GET_LEVEL(d->character),
              CLASS_ABBR(d->character));
    } else
      strcpy(classname, "   -   ");

    timeptr = asctime(localtime(&d->login_time));
    timeptr += 11;
    *(timeptr + 8) = '\0';

    if (STATE(d) == CON_PLAYING && d->original)
      strcpy(state, "Switched");
    else
      strcpy(state, connected_types[STATE(d)]);

    if (d->character && STATE(d) == CON_PLAYING)
      sprintf(idletime, "%5d", d->character->char_specials.timer *
            SECS_PER_MUD_HOUR / SECS_PER_REAL_MIN);
    else
      strcpy(idletime, "     ");

    sprintf(line, "%3d %-7s %-12s %-14s %-3s %-8s ", d->desc_num, classname,
            d->original && d->original->player.name ? d->original->player.name :
            d->character && d->character->player.name ? d->character->player.name :
            "UNDEFINED",
            state, idletime, timeptr);

    if (d->host && *d->host)
      sprintf(line + strlen(line), "[%s]\r\n", d->host);
    else
      strcat(line, "[Hostname unknown]\r\n");

    if (STATE(d) != CON_PLAYING) {
      sprintf(line2, "%s%s%s", CCGRN(ch, C_SPR), line, CCNRM(ch, C_SPR));
      strcpy(line, line2);
    }
    if (STATE(d) != CON_PLAYING ||
            (STATE(d) == CON_PLAYING && CAN_SEE(ch, d->character))) {
      send_to_char(ch, "%s", line);
      num_can_see++;
    }
  }

  send_to_char(ch, "\r\n%d visible sockets connected.\r\n", num_can_see);
}

/* Generic page_string function for displaying text */
ACMD(do_gen_ps) {
  if (IS_NPC(ch)) {
    send_to_char(ch, "Not for mobiles!\r\n");
    return;
  }

  switch (subcmd) {
    case SCMD_CREDITS:
      page_string(ch->desc, credits, 0);
      break;
    case SCMD_NEWS:
      GET_LAST_NEWS(ch) = time(0);
      page_string(ch->desc, news, 0);
      break;
    case SCMD_INFO:
      page_string(ch->desc, info, 0);
      break;
    case SCMD_WIZLIST:
      page_string(ch->desc, wizlist, 0);
      break;
    case SCMD_IMMLIST:
      page_string(ch->desc, immlist, 0);
      break;
    case SCMD_HANDBOOK:
      page_string(ch->desc, handbook, 0);
      break;
    case SCMD_POLICIES:
      page_string(ch->desc, policies, 0);
      break;
    case SCMD_MOTD:
      GET_LAST_MOTD(ch) = time(0);
      page_string(ch->desc, motd, 0);
      break;
    case SCMD_IMOTD:
      page_string(ch->desc, imotd, 0);
      break;
    case SCMD_CLEAR:
      send_to_char(ch, "\033[H\033[J");
      break;
    case SCMD_VERSION:
      send_to_char(ch, "%s\r\n", tbamud_version);
      break;
    case SCMD_WHOAMI:
      send_to_char(ch, "%s\r\n", GET_NAME(ch));
      break;
    default:
      log("SYSERR: Unhandled case in do_gen_ps. (%d)", subcmd);
      /* SYSERR_DESC: General page string function for such things as 'credits',
       * 'news', 'wizlist', 'clear', 'version'.  This occurs when a call is made
       * to this routine that is not one of the predefined calls.  To correct it,
       * either a case needs to be added into the function to account for the
       * subcmd that is being passed to it, or the call to the function needs to
       * have the correct subcmd put into place. */
      return;
  }
}

static void perform_mortal_where(struct char_data *ch, char *arg) {
  struct char_data *i;
  struct descriptor_data *d;
  int j;

  if (!*arg) {
    j = world[(IN_ROOM(ch))].zone;
    send_to_char(ch, "\tb--\tB= \tCPlayers in %s \tB=\tb--\r\n\tc-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-\tn\r\n", zone_table[j].name);
    for (d = descriptor_list; d; d = d->next) {
      if (STATE(d) != CON_PLAYING || d->character == ch)
        continue;
      if ((i = (d->original ? d->original : d->character)) == NULL)
        continue;
      if (IN_ROOM(i) == NOWHERE || !CAN_SEE(ch, i))
        continue;
      if (world[IN_ROOM(ch)].zone != world[IN_ROOM(i)].zone)
        continue;
      send_to_char(ch, "\tB[\tc%-20s%s\tB]\tW - %s%s\tn\r\n", GET_NAME(i), QNRM, world[IN_ROOM(i)].name, QNRM);
    }
  } else { /* print only FIRST char, not all. */
    for (i = character_list; i; i = i->next) {
      if (IN_ROOM(i) == NOWHERE || i == ch)
        continue;
      if (!CAN_SEE(ch, i) || world[IN_ROOM(i)].zone != world[IN_ROOM(ch)].zone)
        continue;
      if (!isname(arg, i->player.name))
        continue;
      send_to_char(ch, "\tB[\tc%-25s%s\tB]\tW - %s%s\tn\r\n", GET_NAME(i), QNRM, world[IN_ROOM(i)].name, QNRM);
      return;
    }
    send_to_char(ch, "Nobody around by that name.\r\n");
  }
}

static void print_object_location(int num, struct obj_data *obj, struct char_data *ch,
        int recur) {
  if (num > 0)
    send_to_char(ch, "O%3d. %-25s%s - ", num, obj->short_description, QNRM);
  else
    send_to_char(ch, "%33s", " - ");

  if (SCRIPT(obj)) {
    if (!TRIGGERS(SCRIPT(obj))->next)
      send_to_char(ch, "[T%d] ", GET_TRIG_VNUM(TRIGGERS(SCRIPT(obj))));
    else
      send_to_char(ch, "[TRIGS] ");
  }

  if (IN_ROOM(obj) != NOWHERE)
    send_to_char(ch, "[%5d] %s%s\r\n", GET_ROOM_VNUM(IN_ROOM(obj)), world[IN_ROOM(obj)].name, QNRM);
  else if (obj->carried_by)
    send_to_char(ch, "carried by %s%s\r\n", PERS(obj->carried_by, ch), QNRM);
  else if (obj->worn_by)
    send_to_char(ch, "worn by %s%s\r\n", PERS(obj->worn_by, ch), QNRM);
  else if (obj->in_obj) {
    send_to_char(ch, "inside %s%s%s\r\n", obj->in_obj->short_description, QNRM, (recur ? ", which is" : " "));
    if (recur)
      print_object_location(0, obj->in_obj, ch, recur);
  } else
    send_to_char(ch, "in an unknown location\r\n");
}

static void perform_immort_where(struct char_data *ch, char *arg) {
  struct char_data *i;
  struct obj_data *k;
  struct descriptor_data *d;
  int num = 0, found = 0;

  if (!*arg) {
    send_to_char(ch, "Players  Room    Location                       Zone\r\n");
    send_to_char(ch, "-------- ------- ------------------------------ -------------------\r\n");
    for (d = descriptor_list; d; d = d->next)
      if (IS_PLAYING(d)) {
        i = (d->original ? d->original : d->character);
        if (i && CAN_SEE(ch, i) && (IN_ROOM(i) != NOWHERE)) {
          if (d->original)
            send_to_char(ch, "%-8s%s - [%5d] %s%s (in %s%s)\r\n",
                  GET_NAME(i), QNRM, GET_ROOM_VNUM(IN_ROOM(d->character)),
                  world[IN_ROOM(d->character)].name, QNRM, GET_NAME(d->character), QNRM);
          else
            send_to_char(ch, "%-8s%s %s[%s%5d%s]%s %-*s%s %s%s\r\n", GET_NAME(i), QNRM,
                  QCYN, QYEL, GET_ROOM_VNUM(IN_ROOM(i)), QCYN, QNRM,
                  30 + count_color_chars(world[IN_ROOM(i)].name), world[IN_ROOM(i)].name, QNRM,
                  zone_table[(world[IN_ROOM(i)].zone)].name, QNRM);
        }
      }
  } else {
    for (i = character_list; i; i = i->next)
      if (CAN_SEE(ch, i) && IN_ROOM(i) != NOWHERE && isname(arg, i->player.name)) {
        found = 1;
        send_to_char(ch, "M%3d. %-25s%s - [%5d] %-25s%s", ++num, GET_NAME(i), QNRM,
                GET_ROOM_VNUM(IN_ROOM(i)), world[IN_ROOM(i)].name, QNRM);
        if (SCRIPT(i) && TRIGGERS(SCRIPT(i))) {
          if (!TRIGGERS(SCRIPT(i))->next)
            send_to_char(ch, "[T%d] ", GET_TRIG_VNUM(TRIGGERS(SCRIPT(i))));
          else
            send_to_char(ch, "[TRIGS] ");
        }
        send_to_char(ch, "%s\r\n", QNRM);
      }
    for (num = 0, k = object_list; k; k = k->next)
      if (CAN_SEE_OBJ(ch, k) && isname(arg, k->name)) {
        found = 1;
        print_object_location(++num, k, ch, TRUE);
      }
    if (!found)
      send_to_char(ch, "Couldn't find any such thing.\r\n");
  }
}

ACMD(do_where) {
  char arg[MAX_INPUT_LENGTH];

  one_argument(argument, arg);

  if (GET_LEVEL(ch) >= LVL_IMMORT)
    perform_immort_where(ch, arg);
  else
    perform_mortal_where(ch, arg);
}

ACMD(do_levels) {
  char buf[MAX_STRING_LENGTH], arg[MAX_STRING_LENGTH];
  size_t len = 0, nlen;
  int i, ret, min_lev = 1, max_lev = LVL_IMMORT, val;

  if (IS_NPC(ch)) {
    send_to_char(ch, "You ain't nothin' but a hound-dog.\r\n");
    return;
  }
  one_argument(argument, arg);

  if (arg != NULL && *arg) {
    if (isdigit(*arg)) {
      ret = sscanf(arg, "%d-%d", &min_lev, &max_lev);
      if (ret == 0) {
        /* No valid args found */
        min_lev = 1;
        max_lev = LVL_IMMORT;
      } else if (ret == 1) {
        /* One arg = range is (num) either side of current level */
        val = min_lev;
        max_lev = MIN(GET_LEVEL(ch) + val, LVL_IMMORT);
        min_lev = MAX(GET_LEVEL(ch) - val, 1);
      } else if (ret == 2) {
        /* Two args = min-max range limit - just do sanity checks */
        min_lev = MAX(min_lev, 1);
        max_lev = MIN(max_lev + 1, LVL_IMMORT);
      }
    } else {
      send_to_char(ch, "Usage: %slevels [<min>-<max> | <range>]%s\r\n\r\n", QYEL, QNRM);
      send_to_char(ch, "Displays exp required for levels.\r\n");
      send_to_char(ch, "%slevels       %s- shows all levels (1-%d)\r\n", QCYN, QNRM, (LVL_IMMORT - 1));
      send_to_char(ch, "%slevels 5     %s- shows 5 levels either side of your current level\r\n", QCYN, QNRM);
      send_to_char(ch, "%slevels 10-40 %s- shows level 10 to level 40\r\n", QCYN, QNRM);
      return;
    }
  }

  for (i = min_lev; i < max_lev; i++) {
    nlen = snprintf(buf + len, sizeof (buf) - len, "[%2d] %8d-%-8d : ", (int) i,
            level_exp(ch, i), level_exp(ch, i + 1) - 1);
    if (len + nlen >= sizeof (buf))
      break;
    len += nlen;

    //why are we checking sex for titles?  titles use to be sex
    //dependent...  -zusuk
    switch (GET_SEX(ch)) {
      case SEX_MALE:
      case SEX_NEUTRAL:
        nlen = snprintf(buf + len, sizeof (buf) - len, "%s\r\n", titles(GET_CLASS(ch), i));
        break;
      case SEX_FEMALE:
        nlen = snprintf(buf + len, sizeof (buf) - len, "%s\r\n", titles(GET_CLASS(ch), i));
        break;
      default:
        nlen = snprintf(buf + len, sizeof (buf) - len, "Oh dear.  You seem to be sexless.\r\n");
        break;
    }
    if (len + nlen >= sizeof (buf))
      break;
    len += nlen;
  }

  if (len < sizeof (buf) && max_lev == LVL_IMMORT)
    snprintf(buf + len, sizeof (buf) - len, "[%2d] %8d          : Immortality\r\n",
          LVL_IMMORT, level_exp(ch, LVL_IMMORT));
  page_string(ch->desc, buf, TRUE);
}

ACMD(do_consider) {
  char buf[MAX_INPUT_LENGTH];
  struct char_data *victim;
  int diff;

  one_argument(argument, buf);

  if (!(victim = get_char_vis(ch, buf, NULL, FIND_CHAR_ROOM))) {
    send_to_char(ch, "Consider killing who?\r\n");
    return;
  }
  if (victim == ch) {
    send_to_char(ch, "Easy!  Very easy indeed!\r\n");
    return;
  }
  if (!IS_NPC(victim)) {
    send_to_char(ch, "Would you like to borrow a cross and a shovel?\r\n");
    return;
  }
  diff = (GET_LEVEL(victim) - GET_LEVEL(ch));

  if (diff <= -10)
    send_to_char(ch, "Now where did that chicken go?\r\n");
  else if (diff <= -5)
    send_to_char(ch, "You could do it with a needle!\r\n");
  else if (diff <= -2)
    send_to_char(ch, "Easy.\r\n");
  else if (diff <= -1)
    send_to_char(ch, "Fairly easy.\r\n");
  else if (diff == 0)
    send_to_char(ch, "The perfect match!\r\n");
  else if (diff <= 1)
    send_to_char(ch, "You would need some luck!\r\n");
  else if (diff <= 2)
    send_to_char(ch, "You would need a lot of luck!\r\n");
  else if (diff <= 3)
    send_to_char(ch, "You would need a lot of luck and great equipment!\r\n");
  else if (diff <= 5)
    send_to_char(ch, "Do you feel lucky, punk?\r\n");
  else if (diff <= 10)
    send_to_char(ch, "Are you mad!?\r\n");
  else if (diff <= 100)
    send_to_char(ch, "You ARE mad!\r\n");
}

ACMD(do_diagnose) {
  char buf[MAX_INPUT_LENGTH];
  struct char_data *vict;

  one_argument(argument, buf);

  if (*buf) {
    if (!(vict = get_char_vis(ch, buf, NULL, FIND_CHAR_ROOM)))
      send_to_char(ch, "%s", CONFIG_NOPERSON);
    else
      diag_char_to_char(vict, ch);
  } else {
    if (FIGHTING(ch))
      diag_char_to_char(FIGHTING(ch), ch);
    else
      send_to_char(ch, "Diagnose who?\r\n");
  }
}

ACMD(do_toggle) {
  char buf2[4], arg[MAX_INPUT_LENGTH], arg2[MAX_INPUT_LENGTH];
  int toggle, tp, wimp_lev, result = 0, len = 0, i;
  const char *types[] = {"off", "brief", "normal", "on", "\n"};

  const struct {
    char *command;
    bitvector_t toggle; /* this needs changing once hashmaps are implemented */
    char min_level;
    char *disable_msg;
    char *enable_msg;
  } tog_messages[] = {
    /*0*/
    {"summonable", PRF_SUMMONABLE, 0,
      "You are now safe from summoning by other players.\r\n",
      "You may now be summoned by other players.\r\n"},
    /*1*/
    {"nohassle", PRF_NOHASSLE, LVL_IMMORT,
      "Nohassle disabled.\r\n",
      "Nohassle enabled.\r\n"},
    /*2*/
    {"brief", PRF_BRIEF, 0,
      "Brief mode off.\r\n",
      "Brief mode on.\r\n"},
    /*3*/
    {"compact", PRF_COMPACT, 0,
      "Compact mode off.\r\n",
      "Compact mode on.\r\n"},
    /*4*/
    {"notell", PRF_NOTELL, 0,
      "You can now hear tells.\r\n",
      "You are now deaf to tells.\r\n"},
    /*5*/
    {"noauction", PRF_NOAUCT, 0,
      "You can now hear auctions.\r\n",
      "You are now deaf to auctions.\r\n"},
    /*6*/
    {"noshout", PRF_NOSHOUT, 0,
      "You can now hear shouts.\r\n",
      "You are now deaf to shouts.\r\n"},
    /*7*/
    {"nogossip", PRF_NOGOSS, 0,
      "You can now hear gossip.\r\n",
      "You are now deaf to gossip.\r\n"},
    /*8*/
    {"nograts", PRF_NOGRATZ, 0,
      "You can now hear gratz.\r\n",
      "You are now deaf to gratz.\r\n"},
    /*9*/
    {"nowiz", PRF_NOWIZ, LVL_IMMORT,
      "You can now hear the Wiz-channel.\r\n",
      "You are now deaf to the Wiz-channel.\r\n"},
    /*10*/
    {"quest", PRF_QUEST, 0,
      "You are no longer part of the Quest.\r\n",
      "Okay, you are part of the Quest.\r\n"},
    /*11*/
    {"showvnums", PRF_SHOWVNUMS, LVL_IMMORT,
      "You will no longer see the vnums.\r\n",
      "You will now see the vnums.\r\n"},
    /*12*/
    {"norepeat", PRF_NOREPEAT, 0,
      "You will now have your communication repeated.\r\n",
      "You will no longer have your communication repeated.\r\n"},
    /*13*/
    {"holylight", PRF_HOLYLIGHT, LVL_IMMORT,
      "HolyLight mode off.\r\n",
      "HolyLight mode on.\r\n"},
    /*14*/
    {"slownameserver", 0, LVL_IMPL,
      "Nameserver_is_slow changed to OFF; IP addresses will now be resolved.\r\n",
      "Nameserver_is_slow changed to ON; sitenames will no longer be resolved.\r\n"},
    /*15*/
    {"autoexits", PRF_AUTOEXIT, 0,
      "Autoexits disabled.\r\n",
      "Autoexits enabled.\r\n"},
    /*16*/
    {"trackthru", 0, LVL_IMPL,
      "Players can no longer track through doors.\r\n",
      "Players can now track through doors.\r\n"},
    /*17*/
    {"clsolc", PRF_CLS, LVL_BUILDER,
      "You will no longer clear screen in OLC.\r\n",
      "You will now clear screen in OLC.\r\n"},
    /*18*/
    {"buildwalk", PRF_BUILDWALK, LVL_BUILDER,
      "Buildwalk is now Off.\r\n",
      "Buildwalk is now On.\r\n"},
    /*19*/
    {"afk", PRF_AFK, 0,
      "AFK is now Off.\r\n",
      "AFK is now On.\r\n"},
    /*20*/
    {"autoloot", PRF_AUTOLOOT, 0,
      "Autoloot disabled.\r\n",
      "Autoloot enabled.\r\n"},
    /*21*/
    {"autogold", PRF_AUTOGOLD, 0,
      "Autogold disabled.\r\n",
      "Autogold enabled.\r\n"},
    /*22*/
    {"autosplit", PRF_AUTOSPLIT, 0,
      "Autosplit disabled.\r\n",
      "Autosplit enabled.\r\n"},
    /*23*/
    {"autosac", PRF_AUTOSAC, 0,
      "Autosac disabled.\r\n",
      "Autosac enabled.\r\n"},
    /*24*/
    {"autoassist", PRF_AUTOASSIST, 0,
      "Autoassist disabled.\r\n",
      "Autoassist enabled.\r\n"},
    /*25*/
    {"automap", PRF_AUTOMAP, 1,
      "You will no longer see the mini-map.\r\n",
      "You will now see a mini-map at the side of room descriptions.\r\n"},
    /*26*/
    {"autokey", PRF_AUTOKEY, 0,
      "You will now have to unlock doors manually before opening.\r\n",
      "You will now automatically unlock doors when opening them (if you have the key).\r\n"},
    /*27*/
    {"autodoor", PRF_AUTODOOR, 0,
      "You will now need to specify a door direction when opening, closing and unlocking.\r\n",
      "You will now find the next available door when opening, closing or unlocking.\r\n"},
    /*28*/
    {"clantalk", PRF_NOCLANTALK, LVL_GOD,
      "You can now hear all clan's clantalk.\r\n",
      "All clantalk will now be hidden.\r\n"},
    /*29*/
    {"color", 0, 0, "\n", "\n"},
    /*30*/
    {"syslog", 0, LVL_IMMORT, "\n", "\n"},
    /*31*/
    {"wimpy", 0, 0, "\n", "\n"},
    /*32*/
    {"pagelength", 0, 0, "\n", "\n"},
    /*33*/
    {"screenwidth", 0, 0, "\n", "\n"},
    /*34*/
    {"autoscan", PRF_AUTOSCAN, 0,
      "Autoscan disabled.\r\n",
      "Autoscan enabled.\r\n"},
    /*LAST*/
    {"\n", 0, -1, "\n", "\n"} /* must be last */
  };

  if (IS_NPC(ch))
    return;

  argument = one_argument(argument, arg);
  any_one_arg(argument, arg2); /* so that we don't skip 'on' */

  if (!*arg) {
    if (!GET_WIMP_LEV(ch))
      strcpy(buf2, "OFF"); /* strcpy: OK */
    else
      sprintf(buf2, "%-3.3d", GET_WIMP_LEV(ch)); /* sprintf: OK */

    if (GET_LEVEL(ch) == LVL_IMPL) {
      send_to_char(ch,
              " SlowNameserver: %-3s   "
              "                        "
              " Trackthru Doors: %-3s\r\n",

              ONOFF(CONFIG_NS_IS_SLOW),
              ONOFF(CONFIG_TRACK_T_DOORS));
    }

    if (GET_LEVEL(ch) >= LVL_IMMORT) {
      send_to_char(ch,
              "      Buildwalk: %-3s    "
              "          NoWiz: %-3s    "
              "         ClsOLC: %-3s\r\n"
              "       NoHassle: %-3s    "
              "      Holylight: %-3s    "
              "      ShowVnums: %-3s\r\n"
              "     NoClanTalk: %-3s\r\n"
              "         Syslog: %-3s\r\n",

              ONOFF(PRF_FLAGGED(ch, PRF_BUILDWALK)),
              ONOFF(PRF_FLAGGED(ch, PRF_NOWIZ)),
              ONOFF(PRF_FLAGGED(ch, PRF_CLS)),
              ONOFF(PRF_FLAGGED(ch, PRF_NOHASSLE)),
              ONOFF(PRF_FLAGGED(ch, PRF_HOLYLIGHT)),
              ONOFF(PRF_FLAGGED(ch, PRF_SHOWVNUMS)),
              ONOFF(PRF_FLAGGED(ch, PRF_NOCLANTALK)),
              types[(PRF_FLAGGED(ch, PRF_LOG1) ? 1 : 0) + (PRF_FLAGGED(ch, PRF_LOG2) ? 2 : 0)]);
    }

    send_to_char(ch,
            "Hit Pnt Display: %-3s    "
            "          Brief: %-3s    "
            "     Summonable: %-3s\r\n"

            "   Move Display: %-3s    "
            "        Compact: %-3s    "
            "          Quest: %-3s\r\n"

            "   Mana Display: %-3s    "
            "         NoTell: %-3s    "
            "       NoRepeat: %-3s\r\n"

            "      AutoExits: %-3s    "
            "        NoShout: %-3s    "
            "          Wimpy: %-3s\r\n"

            "       NoGossip: %-3s    "
            "      NoAuction: %-3s    "
            "        NoGrats: %-3s\r\n"

            "       AutoLoot: %-3s    "
            "       AutoGold: %-3s    "
            "      AutoSplit: %-3s\r\n"

            "        AutoSac: %-3s    "
            "     AutoAssist: %-3s    "
            "        AutoMap: %-3s\r\n"

            "     Pagelength: %-3d    "
            "    Screenwidth: %-3d    "
            "            AFK: %-3s\r\n"

            "        Autokey: %-3s    "
            "       Autodoor: %-3s    "
            "          Color: %s\r\n"

            "       Autoscan: %-3s    "
            "    EXP Display: %-3s    "
            "  Exits Display: %-3s\r\n"

            "   Room Display: %-3s    "
            "Memtime Display: %-3s\r\n",


            ONOFF(PRF_FLAGGED(ch, PRF_DISPHP)),
            ONOFF(PRF_FLAGGED(ch, PRF_BRIEF)),
            ONOFF(PRF_FLAGGED(ch, PRF_SUMMONABLE)),

            ONOFF(PRF_FLAGGED(ch, PRF_DISPMOVE)),
            ONOFF(PRF_FLAGGED(ch, PRF_COMPACT)),
            ONOFF(PRF_FLAGGED(ch, PRF_QUEST)),

            ONOFF(PRF_FLAGGED(ch, PRF_DISPMANA)),
            ONOFF(PRF_FLAGGED(ch, PRF_NOTELL)),
            ONOFF(PRF_FLAGGED(ch, PRF_NOREPEAT)),

            ONOFF(PRF_FLAGGED(ch, PRF_AUTOEXIT)),
            ONOFF(PRF_FLAGGED(ch, PRF_NOSHOUT)),
            buf2,

            ONOFF(PRF_FLAGGED(ch, PRF_NOGOSS)),
            ONOFF(PRF_FLAGGED(ch, PRF_NOAUCT)),
            ONOFF(PRF_FLAGGED(ch, PRF_NOGRATZ)),

            ONOFF(PRF_FLAGGED(ch, PRF_AUTOLOOT)),
            ONOFF(PRF_FLAGGED(ch, PRF_AUTOGOLD)),
            ONOFF(PRF_FLAGGED(ch, PRF_AUTOSPLIT)),

            ONOFF(PRF_FLAGGED(ch, PRF_AUTOSAC)),
            ONOFF(PRF_FLAGGED(ch, PRF_AUTOASSIST)),
            ONOFF(PRF_FLAGGED(ch, PRF_AUTOMAP)),

            GET_PAGE_LENGTH(ch),
            GET_SCREEN_WIDTH(ch),
            ONOFF(PRF_FLAGGED(ch, PRF_AFK)),

            ONOFF(PRF_FLAGGED(ch, PRF_AUTOKEY)),
            ONOFF(PRF_FLAGGED(ch, PRF_AUTODOOR)),
            types[COLOR_LEV(ch)],

            ONOFF(PRF_FLAGGED(ch, PRF_AUTOSCAN)),
            ONOFF(PRF_FLAGGED(ch, PRF_DISPEXP)),
            ONOFF(PRF_FLAGGED(ch, PRF_DISPEXITS)),

            ONOFF(PRF_FLAGGED(ch, PRF_DISPROOM)),
            ONOFF(PRF_FLAGGED(ch, PRF_DISPMEMTIME))
            /*end*/);
    return;
  }

  len = strlen(arg);
  for (toggle = 0; *tog_messages[toggle].command != '\n'; toggle++)
    if (!strncmp(arg, tog_messages[toggle].command, len))
      break;

  if (*tog_messages[toggle].command == '\n' || tog_messages[toggle].min_level > GET_LEVEL(ch)) {
    send_to_char(ch, "You can't toggle that!\r\n");
    return;
  }

  switch (toggle) {
    case SCMD_COLOR:
      if (!*arg2) {
        send_to_char(ch, "Your current color level is %s.\r\n", types[COLOR_LEV(ch)]);
        return;
      }

      if (((tp = search_block(arg2, types, FALSE)) == -1)) {
        send_to_char(ch, "Usage: toggle color { Off | Brief | Normal | On }\r\n");
        return;
      }
      REMOVE_BIT_AR(PRF_FLAGS(ch), PRF_COLOR_1);
      REMOVE_BIT_AR(PRF_FLAGS(ch), PRF_COLOR_2);
      if (tp & 1) SET_BIT_AR(PRF_FLAGS(ch), PRF_COLOR_1);
      if (tp & 2) SET_BIT_AR(PRF_FLAGS(ch), PRF_COLOR_2);

      send_to_char(ch, "Your %scolor%s is now %s.\r\n", CCRED(ch, C_SPR), CCNRM(ch, C_OFF), types[tp]);
      return;
    case SCMD_SYSLOG:
      if (!*arg2) {
        send_to_char(ch, "Your syslog is currently %s.\r\n",
                types[(PRF_FLAGGED(ch, PRF_LOG1) ? 1 : 0) + (PRF_FLAGGED(ch, PRF_LOG2) ? 2 : 0)]);
        return;
      }
      if (((tp = search_block(arg2, types, FALSE)) == -1)) {
        send_to_char(ch, "Usage: toggle syslog { Off | Brief | Normal | On }\r\n");
        return;
      }
      REMOVE_BIT_AR(PRF_FLAGS(ch), PRF_LOG1);
      REMOVE_BIT_AR(PRF_FLAGS(ch), PRF_LOG2);
      if (tp & 1) SET_BIT_AR(PRF_FLAGS(ch), PRF_LOG1);
      if (tp & 2) SET_BIT_AR(PRF_FLAGS(ch), PRF_LOG2);

      send_to_char(ch, "Your syslog is now %s.\r\n", types[tp]);
      return;
    case SCMD_SLOWNS:
      result = (CONFIG_NS_IS_SLOW = !CONFIG_NS_IS_SLOW);
      break;
    case SCMD_TRACK:
      result = (CONFIG_TRACK_T_DOORS = !CONFIG_TRACK_T_DOORS);
      break;
    case SCMD_BUILDWALK:
      if (GET_LEVEL(ch) < LVL_BUILDER) {
        send_to_char(ch, "Builders only, sorry.\r\n");
        return;
      }
      result = PRF_TOG_CHK(ch, PRF_BUILDWALK);
      if (PRF_FLAGGED(ch, PRF_BUILDWALK)) {
        for (i = 0; *arg2 && *(sector_types[i]) != '\n'; i++)
          if (is_abbrev(arg2, sector_types[i]))
            break;
        if (*(sector_types[i]) == '\n')
          i = 0;
        GET_BUILDWALK_SECTOR(ch) = i;
        send_to_char(ch, "Default sector type is %s\r\n", sector_types[i]);
        mudlog(CMP, GET_LEVEL(ch), TRUE,
                "OLC: %s turned buildwalk on.  Allowed zone %d", GET_NAME(ch), GET_OLC_ZONE(ch));
      } else
        mudlog(CMP, GET_LEVEL(ch), TRUE,
              "OLC: %s turned buildwalk off.  Allowed zone %d", GET_NAME(ch), GET_OLC_ZONE(ch));
      break;
    case SCMD_AFK:
      if ((result = PRF_TOG_CHK(ch, PRF_AFK)))
        act("$n is now away from $s keyboard.", TRUE, ch, 0, 0, TO_ROOM);
      else {
        act("$n has returned to $s keyboard.", TRUE, ch, 0, 0, TO_ROOM);
        if (has_mail(GET_IDNUM(ch)))
          send_to_char(ch, "You have mail waiting.\r\n");
      }
      break;
    case SCMD_WIMPY:
      if (!*arg2) {
        if (GET_WIMP_LEV(ch)) {
          send_to_char(ch, "Your current wimp level is %d hit points.\r\n", GET_WIMP_LEV(ch));
          return;
        } else {
          send_to_char(ch, "At the moment, you're not a wimp.  (sure, sure...)\r\n");
          return;
        }
      }
      if (isdigit(*arg2)) {
        if ((wimp_lev = atoi(arg2)) != 0) {
          if (wimp_lev < 0)
            send_to_char(ch, "Heh, heh, heh.. we are jolly funny today, eh?\r\n");
          else if (wimp_lev > GET_MAX_HIT(ch))
            send_to_char(ch, "That doesn't make much sense, now does it?\r\n");
          else if (wimp_lev > (GET_MAX_HIT(ch) / 2))
            send_to_char(ch, "You can't set your wimp level above half your hit points.\r\n");
          else {
            send_to_char(ch, "Okay, you'll wimp out if you drop below %d hit points.", wimp_lev);
            GET_WIMP_LEV(ch) = wimp_lev;
          }
        } else {
          send_to_char(ch, "Okay, you'll now tough out fights to the bitter end.");
          GET_WIMP_LEV(ch) = 0;
        }
      } else
        send_to_char(ch, "Specify at how many hit points you want to wimp out at.  (0 to disable)\r\n");
      break;
    case SCMD_PAGELENGTH:
      if (!*arg2)
        send_to_char(ch, "You current page length is set to %d lines.", GET_PAGE_LENGTH(ch));
      else if (is_number(arg2)) {
        GET_PAGE_LENGTH(ch) = MIN(MAX(atoi(arg2), 5), 255);
        send_to_char(ch, "Okay, your page length is now set to %d lines.", GET_PAGE_LENGTH(ch));
      } else
        send_to_char(ch, "Please specify a number of lines (5 - 255).");
      break;
    case SCMD_SCREENWIDTH:
      if (!*arg2)
        send_to_char(ch, "Your current screen width is set to %d characters.", GET_SCREEN_WIDTH(ch));
      else if (is_number(arg2)) {
        GET_SCREEN_WIDTH(ch) = MIN(MAX(atoi(arg2), 40), 200);
        send_to_char(ch, "Okay, your screen width is now set to %d characters.", GET_SCREEN_WIDTH(ch));
      } else
        send_to_char(ch, "Please specify a number of characters (40 - 200).");
      break;
    case SCMD_AUTOMAP:
      if (can_see_map(ch)) {
        if (!*arg2) {
          TOGGLE_BIT_AR(PRF_FLAGS(ch), tog_messages[toggle].toggle);
          result = (PRF_FLAGGED(ch, tog_messages[toggle].toggle));
        } else if (!strcmp(arg2, "on")) {
          SET_BIT_AR(PRF_FLAGS(ch), tog_messages[toggle].toggle);
          result = 1;
        } else if (!strcmp(arg2, "off")) {
          REMOVE_BIT_AR(PRF_FLAGS(ch), tog_messages[toggle].toggle);
        } else {
          send_to_char(ch, "Value for %s must either be 'on' or 'off'.\r\n", tog_messages[toggle].command);
          return;
        }
      } else
        send_to_char(ch, "Sorry, automap is currently disabled.\r\n");
      break;
    default:
      if (!*arg2) {
        TOGGLE_BIT_AR(PRF_FLAGS(ch), tog_messages[toggle].toggle);
        result = (PRF_FLAGGED(ch, tog_messages[toggle].toggle));
      } else if (!strcmp(arg2, "on")) {
        SET_BIT_AR(PRF_FLAGS(ch), tog_messages[toggle].toggle);
        result = 1;
      } else if (!strcmp(arg2, "off")) {
        REMOVE_BIT_AR(PRF_FLAGS(ch), tog_messages[toggle].toggle);
      } else {
        send_to_char(ch, "Value for %s must either be 'on' or 'off'.\r\n", tog_messages[toggle].command);
        return;
      }
  }
  if (result)
    send_to_char(ch, "%s", tog_messages[toggle].enable_msg);
  else
    send_to_char(ch, "%s", tog_messages[toggle].disable_msg);
}

ACMD(do_commands) {
  int no, i, cmd_num;
  int wizhelp = 0, socials = 0;
  struct char_data *vict;
  char arg[MAX_INPUT_LENGTH];
  char buf[MAX_STRING_LENGTH];
  const char *commands[1000];
  int overflow = sizeof (commands) / sizeof (commands[0]);

  if (!ch->desc)
    return;

  one_argument(argument, arg);

  if (*arg) {
    if (!(vict = get_char_vis(ch, arg, NULL, FIND_CHAR_WORLD)) || IS_NPC(vict)) {
      send_to_char(ch, "Who is that?\r\n");
      return;
    }
  } else
    vict = ch;

  if (subcmd == SCMD_SOCIALS)
    socials = 1;
  else if (subcmd == SCMD_WIZHELP)
    wizhelp = 1;

  sprintf(buf, "The following %s%s are available to %s:\r\n",
          wizhelp ? "privileged " : "",
          socials ? "socials" : "commands",
          vict == ch ? "you" : GET_NAME(vict));

  /* cmd_num starts at 1, not 0, to remove 'RESERVED' */
  for (no = 0, cmd_num = 1;
          complete_cmd_info[cmd_sort_info[cmd_num]].command[0] != '\n';
          ++cmd_num) {

    i = cmd_sort_info[cmd_num];

    if (complete_cmd_info[i].minimum_level < 0 || GET_LEVEL(vict) < complete_cmd_info[i].minimum_level)
      continue;

    if ((complete_cmd_info[i].minimum_level >= LVL_IMMORT) != wizhelp)
      continue;

    if (!wizhelp && socials != (complete_cmd_info[i].command_pointer == do_action))
      continue;

    if (wizhelp && complete_cmd_info[i].command_pointer == do_action)
      continue;

    if (--overflow < 0)
      continue;

    /* matching command: copy to commands list */
    commands[no++] = complete_cmd_info[i].command;
  }

  /* display commands list in a nice columnized format */
  column_list(ch, 0, commands, no, FALSE);
}

void free_history(struct char_data *ch, int type) {
  struct txt_block *tmp = GET_HISTORY(ch, type), *ftmp;

  while ((ftmp = tmp)) {
    tmp = tmp->next;
    if (ftmp->text)
      free(ftmp->text);
    free(ftmp);
  }
  GET_HISTORY(ch, type) = NULL;
}

ACMD(do_history) {
  char arg[MAX_INPUT_LENGTH];
  int type;

  one_argument(argument, arg);

  if (is_abbrev(arg, "chat"))
    strcpy(arg, "gossip");
  type = search_block(arg, history_types, FALSE);

  if (!*arg || type < 0) {
    int i;

    send_to_char(ch, "Usage: history <");
    for (i = 0; *history_types[i] != '\n'; i++) {
      send_to_char(ch, " %s ", history_types[i]);
      if (*history_types[i + 1] == '\n')
        send_to_char(ch, ">\r\n");
      else
        send_to_char(ch, "|");
    }
    return;
  }

  if (GET_HISTORY(ch, type) && GET_HISTORY(ch, type)->text && *GET_HISTORY(ch, type)->text) {
    struct txt_block *tmp;
    for (tmp = GET_HISTORY(ch, type); tmp; tmp = tmp->next)
      send_to_char(ch, "%s", tmp->text);
    /* Make this a 1 if you want history to clear after viewing */
#if 0
    free_history(ch, type);
#endif
  } else
    send_to_char(ch, "You have no history in that channel.\r\n");
}

#define HIST_LENGTH 100

void add_history(struct char_data *ch, char *str, int type) {
  int i = 0;
  char time_str[MAX_STRING_LENGTH], buf[MAX_STRING_LENGTH];
  struct txt_block *tmp;
  time_t ct;

  if (IS_NPC(ch))
    return;

  tmp = GET_HISTORY(ch, type);
  ct = time(0);
  strftime(time_str, sizeof (time_str), "%H:%M ", localtime(&ct));

  sprintf(buf, "%s%s", time_str, str);

  if (!tmp) {
    CREATE(GET_HISTORY(ch, type), struct txt_block, 1);
    GET_HISTORY(ch, type)->text = strdup(buf);
  } else {
    while (tmp->next)
      tmp = tmp->next;
    CREATE(tmp->next, struct txt_block, 1);
    tmp->next->text = strdup(buf);

    for (tmp = GET_HISTORY(ch, type); tmp; tmp = tmp->next, i++);

    for (; i > HIST_LENGTH && GET_HISTORY(ch, type); i--) {
      tmp = GET_HISTORY(ch, type);
      GET_HISTORY(ch, type) = tmp->next;
      if (tmp->text)
        free(tmp->text);
      free(tmp);
    }
  }
  /* add this history message to ALL */
  if (type != HIST_ALL)
    add_history(ch, str, HIST_ALL);
}

ACMD(do_whois) {
  struct char_data *victim = 0;
  int hours;
  int got_from_file = 0, c_r;
  char buf[MAX_STRING_LENGTH];
  clan_rnum c_n;

  one_argument(argument, buf);

  if (!*buf) {
    send_to_char(ch, "Whois who?\r\n");
    return;
  }

  if (!(victim = get_player_vis(ch, buf, NULL, FIND_CHAR_WORLD))) {
    CREATE(victim, struct char_data, 1);
    clear_char(victim);

    new_mobile_data(victim);
    /* Allocate mobile event list */
    //victim->events = create_list();

    CREATE(victim->player_specials, struct player_special_data, 1);

    if (load_char(buf, victim) > -1)
      got_from_file = 1;
    else {
      send_to_char(ch, "There is no such player.\r\n");
      free_char(victim);
      return;
    }
  }

  if (IS_IN_CLAN(victim)) {
    c_n = real_clan(GET_CLAN(victim));
    send_to_char(ch, "[Clan Data]\r\n");
    if ((c_r = GET_CLANRANK(victim)) == NO_CLANRANK) {
      send_to_char(ch, "Applied to : %s%s\r\n", CLAN_NAME(c_n), QNRM);
      send_to_char(ch, "Status     : %sAwaiting Approval%s\r\n", QBRED, QNRM);
    } else {
      send_to_char(ch, "Current Clan : %s%s\r\n", CLAN_NAME(c_n), QNRM);
      send_to_char(ch, "Clan Rank    : %s%s (Rank %d)\r\n", clan_list[c_n].rank_name[(c_r - 1)], QNRM, c_r);
    }
  }

  /* We either have our victim from file or he's playing or function has returned. */
  sprinttype(GET_SEX(victim), genders, buf, sizeof (buf));
  send_to_char(ch, "Name: %s %s\r\nSex: %s\r\n", GET_NAME(victim),
          (victim->player.title ? victim->player.title : ""), buf);

  sprinttype(victim->player.chclass, pc_class_types, buf, sizeof (buf));
  send_to_char(ch, "Current Class: %s\r\n", buf);

  send_to_char(ch, "\tCClass(es):\tn ");

  int i, counter = 0;
  for (i = 0; i < MAX_CLASSES; i++) {
    if (CLASS_LEVEL(victim, i)) {
      if (counter)
        send_to_char(ch, " / ");
      send_to_char(ch, "%d %s", CLASS_LEVEL(victim, i), class_abbrevs[i]);
      counter++;
    }
  }
  send_to_char(ch, "\r\n");

  if (IS_MORPHED(victim))
    send_to_char(ch, "Race : %s\r\n",
          npc_race_types[IS_MORPHED(victim)]);
  else
    send_to_char(ch, "Race : %s\r\n",
          pc_race_types[GET_RACE(victim)]);

  send_to_char(ch, "Level: %d\r\n", GET_LEVEL(victim));

  if (!(GET_LEVEL(victim) < LVL_IMMORT) ||
          (GET_LEVEL(ch) >= GET_LEVEL(victim))) {
    strcpy(buf, (char *) asctime(localtime(&(victim->player.time.logon))));
    buf[10] = '\0';

    hours = (time(0) - victim->player.time.logon) / 3600;

    if (!got_from_file) {
      send_to_char(ch, "Last Logon: They're playing now!  (Idle %d Minutes)",
              victim->char_specials.timer * SECS_PER_MUD_HOUR / SECS_PER_REAL_MIN);

      if (!victim->desc)
        send_to_char(ch, "  (Linkless)\r\n");
      else
        send_to_char(ch, "\r\n");

      if (PRF_FLAGGED(victim, PRF_AFK))
        send_to_char(ch, "%s%s is afk right now, so %s may not respond to communication.%s\r\n", CBGRN(ch, C_NRM), GET_NAME(victim), GET_SEX(victim) == SEX_NEUTRAL ? "it" : (GET_SEX(victim) == SEX_MALE ? "he" : "she"), CCNRM(ch, C_NRM));
    } else if (hours > 0)
      send_to_char(ch, "Last Logon: %s (%d days & %d hours ago.)\r\n", buf, hours / 24, hours % 24);
    else
      send_to_char(ch, "Last Logon: %s (0 hours & %d minutes ago.)\r\n",
            buf, (int) (time(0) - victim->player.time.logon) / 60);
  }

  if (has_mail(GET_IDNUM(victim)))
    send_to_char(ch, "They have mail waiting.\r\n");
  else
    send_to_char(ch, "They have no mail waiting.\r\n");

  if (PLR_FLAGGED(victim, PLR_DELETED))
    send_to_char(ch, "***DELETED***\r\n");

  if (!got_from_file && victim->desc != NULL && GET_LEVEL(ch) >= LVL_GOD) {
    protocol_t * prot = victim->desc->pProtocol;
    send_to_char(ch, "Client:  %s [%s]\r\n",
            prot->pVariables[eMSDP_CLIENT_ID]->pValueString,
            prot->pVariables[eMSDP_CLIENT_VERSION]->pValueString ?
            prot->pVariables[eMSDP_CLIENT_VERSION]->pValueString : "Unknown");
    send_to_char(ch, "Color:   %s\r\n", prot->pVariables[eMSDP_XTERM_256_COLORS]->ValueInt ? "Xterm" : (prot->pVariables[eMSDP_ANSI_COLORS]->ValueInt ? "Ansi" : "None"));
    send_to_char(ch, "MXP:     %s\r\n", prot->bMXP ? "Yes" : "No");
    send_to_char(ch, "Charset: %s\r\n", prot->bCHARSET ? "Yes" : "No");
    send_to_char(ch, "MSP:     %s\r\n", prot->bMSP ? "Yes" : "No");
    send_to_char(ch, "ATCP:    %s\r\n", prot->bATCP ? "Yes" : "No");
    send_to_char(ch, "MSDP:    %s\r\n", prot->bMSDP ? "Yes" : "No");
  }

  if (got_from_file)
    free_char(victim);
}

bool get_zone_levels(zone_rnum znum, char *buf) {
  /* Create a string for the level restrictions for this zone. */
  if ((zone_table[znum].min_level == -1) && (zone_table[znum].max_level == -1)) {
    sprintf(buf, "<Not Set!>");
    return FALSE;
  }

  if (zone_table[znum].min_level == -1) {
    sprintf(buf, "Up to level %d", zone_table[znum].max_level);
    return TRUE;
  }

  if (zone_table[znum].max_level == -1) {
    sprintf(buf, "Above level %d", zone_table[znum].min_level);
    return TRUE;
  }

  sprintf(buf, "Levels %d to %d", zone_table[znum].min_level, zone_table[znum].max_level);
  return TRUE;
}

ACMD(do_areas) {
  int i, hilev = -1, lolev = -1, zcount = 0, lev_set, len = 0, tmp_len = 0;
  char arg[MAX_INPUT_LENGTH], *second, lev_str[MAX_INPUT_LENGTH], buf[MAX_STRING_LENGTH];
  //  char zvn[MAX_INPUT_LENGTH];
  bool show_zone = FALSE, overlap = FALSE, overlap_shown = FALSE, show_popularity = FALSE;
  //  float pop;
  //  clan_rnum ocr;

  one_argument(argument, arg);

  if (*arg) {
    /* There was an arg typed - check for level range */
    second = strchr(arg, '-');
    if (second) {
      /* Check for 1st value */
      if (second == arg)
        lolev = 0;
      else
        lolev = atoi(arg);

      /* Check for 2nd value */
      if (*(second + 1) == '\0' || !isdigit(*(second + 1)))
        hilev = 100;
      else
        hilev = atoi(second + 1);

    } else {
      /* No range - single number */
      lolev = atoi(arg);
      hilev = -1; /* No high level - indicates single level */
    }
  }
  if (hilev != -1 && lolev > hilev) {
    /* Swap hi and lo lev if needed */
    i = lolev;
    lolev = hilev;
    hilev = i;
  }

  if (GET_CLAN(ch) && GET_CLAN(ch) != NO_CLAN && GET_CLANRANK(ch) != NO_CLANRANK) {
    if (real_clan(GET_CLAN(ch)) != NO_CLAN) {
      show_popularity = TRUE;
    }
  }

  if (hilev != -1)
    len = snprintf(buf, sizeof (buf), "Checking range: %s%d to %d%s\r\n", QYEL, lolev, hilev, QNRM);
  else if (lolev != -1)
    len = snprintf(buf, sizeof (buf), "Checking level: %s%d%s\r\n", QYEL, lolev, QNRM);
  else
    len = snprintf(buf, sizeof (buf), "Checking all areas.\r\n");

  for (i = 0; i <= top_of_zone_table; i++) { /* Go through the whole zone table */
    show_zone = FALSE;
    overlap = FALSE;

    if (ZONE_FLAGGED(i, ZONE_GRID)) { /* Is this zone 'on the grid' ?    */
      if (lolev == -1) {
        /* No range supplied, show all zones */
        show_zone = TRUE;
      } else if ((hilev == -1) && (lolev >= ZONE_MINLVL(i)) && (lolev <= ZONE_MAXLVL(i))) {
        /* Single number supplied, it's in this zone's range */
        show_zone = TRUE;
      } else if ((hilev != -1) && (lolev >= ZONE_MINLVL(i)) && (hilev <= ZONE_MAXLVL(i))) {
        /* Range supplied, it's completely within this zone's range (no overlap) */
        show_zone = TRUE;
      } else if ((hilev != -1) && ((lolev >= ZONE_MINLVL(i) && lolev <= ZONE_MAXLVL(i)) || (hilev <= ZONE_MAXLVL(i) && hilev >= ZONE_MINLVL(i)))) {
        /* Range supplied, it overlaps this zone's range */
        show_zone = TRUE;
        overlap = TRUE;
      } else if (ZONE_MAXLVL(i) < 0 && (lolev >= ZONE_MINLVL(i))) {
        /* Max level not set for this zone, but specified min in range */
        show_zone = TRUE;
      } else if (ZONE_MAXLVL(i) < 0 && (hilev >= ZONE_MINLVL(i))) {
        /* Max level not set for this zone, so just display it as red */
        show_zone = TRUE;
        overlap = TRUE;
      }
    }
    /* need to edit the below to include the owning clan and popularity % of the clan */
    /* sprintf(zvn, "%s[%s%3d%s]%s ", QCYN, QYEL, zone_table[i].number, QCYN, QNRM);
      if (show_popularity) {
        // Get the VNUM of the zone the player is currently in
        pop = get_popularity(zone_table[i].number, GET_CLAN(ch));
        ocr = real_clan(get_owning_clan(zone_table[i].number));
        send_to_char(ch, "@n(%3d) %s%s%-*s@n %s%s%s  Popularity: %s[%s%3.2f%%%s]%s  Owning Clan: %s%s@n\r\n", ++zcount,
                   PRF_FLAGGED(ch, PRF_SHOWVNUMS) ? zvn : "", overlap ? QRED : QCYN,
                   count_color_chars(zone_table[i].name)+30, zone_table[i].name,
                   lev_set ? "@c" : "@n", lev_set ? lev_str : "All Levels", QNRM,
                   QCYN, QYEL, pop, QCYN, QNRM, (ocr == NO_CLAN) ? "None!" : clan_list[ocr].clan_name, QNRM);
      } else {
        send_to_char(ch, "@n(%3d) %s%s%-*s@n %s%s@n\r\n", ++zcount,
                   PRF_FLAGGED(ch, PRF_SHOWVNUMS) ? zvn : "", overlap ? QRED : QCYN,
                     count_color_chars(zone_table[i].name)+30, zone_table[i].name,
                 lev_set ? "@c" : "@n", lev_set ? lev_str : "All Levels");   */

    if (show_zone) {
      if (overlap) overlap_shown = TRUE;
      lev_set = get_zone_levels(i, lev_str);
      tmp_len = snprintf(buf + len, sizeof (buf) - len, "\tn(%3d) %s%-*s\tn %s%s\tn\r\n", ++zcount, overlap ? QRED : QCYN,
              count_color_chars(zone_table[i].name) + 30, zone_table[i].name,
              lev_set ? "\tc" : "\tn", lev_set ? lev_str : "All Levels");
      len += tmp_len;
    }
  }
  tmp_len = snprintf(buf + len, sizeof (buf) - len, "%s%d%s area%s found.\r\n", QYEL, zcount, QNRM, zcount == 1 ? "" : "s");
  len += tmp_len;

  if (overlap_shown) {
    tmp_len = snprintf(buf + len, sizeof (buf) - len, "Areas shown in \trred\tn may have some creatures outside the specified range.\r\n");
    len += tmp_len;
  }

  if (zcount == 0)
    send_to_char(ch, "No areas found.\r\n");
  else
    page_string(ch->desc, buf, TRUE);
}

void list_scanned_chars(struct char_data * list, struct char_data * ch, int
        distance, int door) {
  char buf[MAX_STRING_LENGTH], buf2[MAX_STRING_LENGTH];

  const char *how_far[] = {
    "close by",
    "a ways off",
    "far off to the"
  };

  struct char_data *i;
  int count = 0;
  *buf = '\0';

  /* this loop is a quick, easy way to help make a grammatical sentence
     (i.e., "You see x, x, y, and z." with commas, "and", etc.) */

  for (i = list; i; i = i->next_in_room)

    /* put any other conditions for scanning someone in this if statement -
       i.e., if (CAN_SEE(ch, i) && condition2 && condition3) or whatever */

    if (CAN_SEE(ch, i))
      count++;

  if (!count)
    return;

  for (i = list; i; i = i->next_in_room) {

    /* make sure to add changes to the if statement above to this one also, using
       or's to join them.. i.e.,
       if (!CAN_SEE(ch, i) || !condition2 || !condition3) */

    if (!CAN_SEE(ch, i))
      continue;
    if (!*buf)
      sprintf(buf, "You see %s", GET_NAME(i));
    else
      sprintf(buf, "%s%s", buf, GET_NAME(i));
    if (--count > 1)
      strcat(buf, ", ");
    else if (count == 1)
      strcat(buf, " and ");
    else {
      sprintf(buf2, " %s %s.\r\n", how_far[distance], dirs[door]);
      strcat(buf, buf2);
    }

  }
  send_to_char(ch, "%s", buf);
}

ACMD(do_scan) {
  int door;
  bool found = FALSE;

  int range;
  int maxrange = 3;

  room_rnum scanned_room = IN_ROOM(ch);

  if (IS_AFFECTED(ch, AFF_BLIND)) {
    send_to_char(ch, "You can't see a damned thing, you're blind!\r\n");
    return;
  }

  for (door = 0; door < DIR_COUNT; door++) {
    send_to_char(ch, "Scanning %s:\r\n", dirs[door]);
    look_in_direction(ch, door);
    for (range = 1; range <= maxrange; range++) {
      if (world[scanned_room].dir_option[door] && world[scanned_room].dir_option[door]->to_room != NOWHERE &&
              !IS_SET(world[scanned_room].dir_option[door]->exit_info, EX_CLOSED) &&
              !IS_SET(world[scanned_room].dir_option[door]->exit_info, EX_HIDDEN)) {
        scanned_room = world[scanned_room].dir_option[door]->to_room;
        if (IS_DARK(scanned_room) && !CAN_SEE_IN_DARK(ch)) {
          if (world[scanned_room].people)
            send_to_char(ch, "%s: It's too dark to see, but you can hear shuffling.\r\n", dirs[door]);
          else
            send_to_char(ch, "%s: It is too dark to see anything.\r\n", dirs[door]);
          found = TRUE;
        } else {
          if (world[scanned_room].people) {
            list_scanned_chars(world[scanned_room].people, ch, range - 1, door);
            found = TRUE;
          }
        }
      } else
        break;
    } // end of range
    scanned_room = IN_ROOM(ch);
  } // end of directions
  if (!found) {
    send_to_char(ch, "\tcYou don't see anybody nearby.\tn\r\n");
  }
} // end of do_scan


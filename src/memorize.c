/****************************************************************************
 *  Realms of Luminari
 *  File:     memorize.c
 *  Usage:    spell memorization functions, two types:wizard-type and sorc-type
 *            spellbook-related functions are here too
 *  Header:   Header Info is in spells.h
 *  Authors:  Nashak and Zusuk
 *            Spellbook functions taken from CWG project, adapted by Zusuk
 ****************************************************************************/

/*** TODO:  03/14/2013 reported - move header info into separate file or
 neatly organize in spells.h */

#include "conf.h"
#include "sysdep.h"
#include "structs.h"
#include "utils.h"
#include "interpreter.h"
#include "spells.h"
#include "db.h"
#include "comm.h"
#include "mud_event.h"
#include "constants.h"
#include "act.h"
#include "handler.h"  // for obj_from_char()
#include "spec_procs.h"  // for compute_ability

/* local, global variables, defines */

char buf[MAX_INPUT_LENGTH] = { '\0' };
#define	TERMINATE	0

/* =============================================== */
/* ==================Spellbooks=================== */
/* =============================================== */


/* this function displays the contents of a scroll */
void display_scroll(struct char_data *ch, struct obj_data *obj)
{
  if (GET_OBJ_TYPE(obj) != ITEM_SCROLL)
    return;
  
  send_to_char(ch, "The scroll contains the following spell(s):\r\n");
  send_to_char(ch, "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\r\n");
  if (GET_OBJ_VAL(obj, 1) >= 1)
    send_to_char(ch, "%-20s\r\n", skill_name(GET_OBJ_VAL(obj, 1)));
  if (GET_OBJ_VAL(obj, 2) >= 1)
    send_to_char(ch, "%-20s\r\n", skill_name(GET_OBJ_VAL(obj, 2)));
  if (GET_OBJ_VAL(obj, 3) >= 1)
    send_to_char(ch, "%-20s\r\n", skill_name(GET_OBJ_VAL(obj, 3)));
  
  return;
}


/* This function displays the contents of ch's given spellbook */
void display_spells(struct char_data *ch, struct obj_data *obj)
{
  int i;
  
  if (GET_OBJ_TYPE(obj) != ITEM_SPELLBOOK)
    return;

  if (!obj->sbinfo) {
    send_to_char(ch, "This spellbook is completely unused...\r\n");
    return;
  }
  
  send_to_char(ch, "The spellbook contains the following spell(s):\r\n");
  send_to_char(ch, "-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=\r\n");
  
  for (i = 0; i < SPELLBOOK_SIZE; i++) {
    if (obj->sbinfo[i].spellname)
      send_to_char(ch, "%-20s		[%2d]\r\n",
            spell_info[obj->sbinfo[i].spellname].name,
            ((spell_info[obj->sbinfo[i].spellname].min_level[CLASS_WIZARD] + 1) / 2));    
  }
      
  return;
}


/* this function checks whether the spellbook a ch has
 * given spellnum
 * Input:  obj (spellbook), spellnum of spell we're looking for
 * Output:  returns TRUE if spell found in book, otherwise FALSE
 */
bool spell_in_book(struct obj_data *obj, int spellnum)
{
  int i = 0;
  
  if (!obj)
    return FALSE;
  
  if (GET_OBJ_TYPE(obj) != ITEM_SPELLBOOK)
    return FALSE;
  
  if (!obj->sbinfo)
    return FALSE;

  for (i = 0; i < SPELLBOOK_SIZE; i++)
    if (obj->sbinfo[i].spellname == spellnum)
      return TRUE;

  return FALSE;
}


/* this function checks whether the scroll a ch has
 * given spellnum
 * Input:  obj (scroll), spellnum of spell we're looking for
 * Output:  returns TRUE if spell found in the scroll, otherwise FALSE
 */
int spell_in_scroll(struct obj_data *obj, int spellnum)
{
  if (!obj)
    return FALSE;
  
  if (GET_OBJ_TYPE(obj) != ITEM_SCROLL)
    return FALSE;
  
  if (GET_OBJ_VAL(obj, 1) == spellnum ||
      GET_OBJ_VAL(obj, 2) == spellnum ||
      GET_OBJ_VAL(obj, 3) == spellnum)
    return TRUE;

  return FALSE;
}


/* this function is the function to check to see if the ch:
 * 1)  has a spellbook
 * 2)  then calls another function to see if the spell is in the book
 * 3)  it will also check if the spell happens to be in a scroll
 * Input:  ch, spellnum, class of ch related to gen_mem command
 * Output:  returns TRUE if found, FALSE if not found
 */
bool spellbook_ok(struct char_data *ch, int spellnum, int class, bool check_scroll)
{
  struct obj_data *obj;
  bool found = FALSE;
  
  if (class == CLASS_WIZARD) {

    for (obj = ch->carrying; obj && !found; obj = obj->next_content) {
      if (GET_OBJ_TYPE(obj) == ITEM_SPELLBOOK) {
        if (spell_in_book(obj, spellnum)) {
          found = TRUE;
          break;
        }
        continue;
      }
      if (GET_OBJ_TYPE(obj) == ITEM_SCROLL && check_scroll) {
        if (spell_in_scroll(obj, spellnum) && CLASS_LEVEL(ch, class) >=
              spell_info[spellnum].min_level[class]) {
          found = TRUE;
          send_to_char(ch, "The \tmmagical energy\tn of the scroll leaves the "
                "paper and enters your \trmind\tn!\r\n");
          send_to_char(ch, "With the \tmmagical energy\tn transfered from the "
                "scroll, the scroll withers to dust!\r\n");
          obj_from_char(obj);
          break;
        }
        continue;
      }
    }

    if (!found) {
      if (check_scroll)
        send_to_char(ch, "You don't seem to have %s in your spellbook or in "
                "any scrolls.\r\n",
              spell_info[spellnum].name);        
      return FALSE;
    }
  } else  //classes besides wizard
    return FALSE;
  
  return TRUE;
}

/* used for scribing spells from scrolls to spellbook
   used for scribing spells from memory to scrolls
 TODO:  scribing spells from memory to spellbook 
 */
ACMD(do_scribe) {
  char arg1[MAX_INPUT_LENGTH] = { '\0' };
  char arg2[MAX_INPUT_LENGTH] = { '\0' };
  char *s = NULL, buf[READ_SIZE] = { '\0' };
  int i = 0, spellnum = -1, found = FALSE;
  struct obj_data *obj = NULL, *scroll = NULL, *next_obj = NULL;

  half_chop(argument, arg1, arg2);

  /* quick outs */
  if (!*arg1 || !*arg2) {
    send_to_char(ch, "Usually you scribe SOMETHING.\r\n");
    return;
  }
  if (!GET_SKILL(ch, SKILL_SCRIBE)) {
    send_to_char(ch, "You really aren't qualified to do that...\r\n");
    return;
  }
  if (!(obj = get_obj_in_list_vis(ch, arg1, NULL, ch->carrying))) {
    send_to_char(ch, "You don't seem to have any %ss.\r\n", arg1);
    return;
  }

  s = strtok(arg2, "\0");
  spellnum = find_skill_num(s);

  if ((spellnum < 1) || (spellnum >= NUM_SPELLS)) {
    send_to_char(ch, "Strange, there is no such spell.\r\n");
    return;
  }
  
  /* only wizard spells */
  if ((LVL_IMMORT - 1) < spell_info[spellnum].min_level[CLASS_WIZARD]) {
    send_to_char(ch, "That spell is not appropriate for scribing!\r\n");
    return;
  }
  
  /* very much just a dummy check*/
  if (!GET_SKILL(ch, spellnum)) {
    send_to_char(ch, "That spell is not fitting!\r\n");
    return;
  }
  
  /* found an object, looking for a spellbook or scroll.. */
  if (GET_OBJ_TYPE(obj) == ITEM_SPELLBOOK) {    
    /* check if spell is already in book */
    if (spell_in_book(obj, spellnum)) {
      send_to_char(ch, "You already have the spell '%s' in this spellbook.\r\n",
              spell_info[spellnum].name);
      return;
    }
    /* initiate spellbook if needed */
    if (!obj->sbinfo) {
      CREATE(obj->sbinfo, struct obj_spellbook_spell, SPELLBOOK_SIZE);
      memset((char *) obj->sbinfo, 0,
              SPELLBOOK_SIZE * sizeof (struct obj_spellbook_spell));
    }
    /* look for empty spot in book */
    for (i = 0; i < SPELLBOOK_SIZE; i++)
      if (obj->sbinfo[i].spellname == 0)
        break;

    /* oops no space */
    if (i == SPELLBOOK_SIZE) {
      send_to_char(ch, "Your spellbook is full!\r\n");
      return;
    }

    /* ok now loop through inventory, check if we have a scroll
       with requested spell name in it */
    for (scroll = ch->carrying; scroll; scroll = next_obj) {
      next_obj = scroll->next_content;

      if (spell_in_scroll(scroll, spellnum)) {
        send_to_char(ch, "You use a scroll with the spell in your "
                  "inventory...\r\n");
        found = TRUE;
        break;
      }
      
    }
    
    if (!found || !scroll) {
      send_to_char(ch, "You must have a scroll with that spell in order"
              " to scribe it!\r\n");
      return;
    }
    
    /*
    if (!hasSpell(ch, spellnum)) {
      send_to_char(ch, "You must have the spell committed to memory before "
              "you can scribe it!\r\n");
      return;
    }
    */

    obj->sbinfo[i].spellname = spellnum;
    obj->sbinfo[i].pages = MAX(1, lowest_spell_level(spellnum) / 2);
    send_to_char(ch, "You scribe the spell '%s' into your spellbook, which "
            "takes up %d pages.\r\n", spell_info[spellnum].name,
            obj->sbinfo[i].pages);
    extract_obj(scroll);
    save_char(ch, 0);
    
  } else if (GET_OBJ_TYPE(obj) == ITEM_SCROLL) {
    
    if (GET_OBJ_VAL(obj, 1) > 0 ||
        GET_OBJ_VAL(obj, 2) > 0 ||
        GET_OBJ_VAL(obj, 3) > 0) {
      send_to_char(ch, "The scroll has a spell inscribed on it!\r\n");
      return;
    }
    
    if (!hasSpell(ch, spellnum)) {
      send_to_char(ch, "You must have the spell committed to memory before "
              "you can scribe it!\r\n");
      return;
    }
    
    GET_OBJ_VAL(obj, 0) = GET_LEVEL(ch);
    GET_OBJ_VAL(obj, 1) = spellnum;
    GET_OBJ_VAL(obj, 2) = -1;
    GET_OBJ_VAL(obj, 3) = -1;
    
    found = FALSE;
    
    sprintf(buf, "a scroll of '%s'", spell_info[spellnum].name);
    obj->short_description = strdup(buf);
    send_to_char(ch, "You scribe the spell '%s' onto %s.\r\n",
            spell_info[spellnum].name, obj->short_description);
  } else {
    send_to_char(ch, "But you don't have anything suitable for scribing!\r\n");
    return;
  }

  if (!found) {
    send_to_char(ch, "The magical energy committed for the spell '%s' has been "
            "expended.\r\n", spell_info[spellnum].name);
    sprintf(buf, "%d", spellnum);
    forgetSpell(ch, spellnum, -1);
  }
  
  increase_skill(ch, SKILL_SCRIBE);
  
}


/* =============================================== */
/* ==================Memorization================= */
/* =============================================== */

/* It can get confusing the terrible way I set everything up
 *   so here are some notes to help keep self-sanity
 * NUM_CASTERS is an easy way to iterate PRAY*() macros
 * if you are checking all the classes, it'll be NUM_CLASSES
 *   and make sure to use classArray() function to access the
 *   correct value of the PRAY*() macros
 * To make it even more confusing, a lot of the functions
 *   have a double role, for wizard-types and sorc-types
 */


/* since the spell array position for classes doesn't correspond 
 * with the class values, we need a little conversion
 */
int classArray(int class) {
  switch (class) {
    case CLASS_CLERIC:
      return 0;
    case CLASS_DRUID:
      return 1;
    case CLASS_WIZARD:
      return 2;
    case CLASS_SORCERER:
      return 3;
    case CLASS_PALADIN:
      return 4;
    case CLASS_RANGER:
      return 5;
    case CLASS_BARD:
      return 6;
  }
  return -1;
}


// the number of spells received per level for caster types
int wizardSlots[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  1,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  2,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  2,  1,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  3,  3,  2,  0,  0,  0,  0,  0,  0,  0 },
  {  4,  3,  2,  1,  0,  0,  0,  0,  0,  0 },//7
  {  4,  4,  3,  2,  0,  0,  0,  0,  0,  0 },
  {  4,  4,  3,  2,  1,  0,  0,  0,  0,  0 },//9
  {  4,  4,  3,  3,  2,  0,  0,  0,  0,  0 },
  {  4,  4,  4,  3,  2,  1,  0,  0,  0,  0 },//11
  {  4,  4,  4,  3,  3,  2,  0,  0,  0,  0 },
  {  4,  4,  4,  4,  3,  2,  1,  0,  0,  0 },//13
  {  4,  4,  4,  4,  3,  3,  2,  0,  0,  0 },
  {  4,  4,  4,  4,  4,  3,  2,  1,  0,  0 },//15
  {  4,  4,  4,  4,  4,  3,  3,  2,  0,  0 },
  {  4,  4,  4,  4,  4,  4,  3,  2,  1,  0 },//17
  {  4,  4,  4,  4,  4,  4,  3,  3,  2,  0 },
  {  4,  4,  4,  4,  4,  4,  4,  3,  3,  0 },
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//20
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//21
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//22
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//23
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//24
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//25
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//26
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//27
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//28
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//29
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//30
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//31
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//32
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 },//33
  {  4,  4,  4,  4,  4,  4,  4,  4,  4,  0 }//34
};

int sorcererSlots[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  3,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  4,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  5,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  6,  3,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  6,  4,  0,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  6,  5,  3,  0,  0,  0,  0,  0,  0,  0 },
  {  6,  6,  4,  0,  0,  0,  0,  0,  0,  0 },//7
  {  6,  6,  5,  3,  0,  0,  0,  0,  0,  0 },
  {  6,  6,  6,  4,  0,  0,  0,  0,  0,  0 },//9
  {  6,  6,  6,  5,  3,  0,  0,  0,  0,  0 },
  {  6,  6,  6,  6,  4,  0,  0,  0,  0,  0 },//11
  {  6,  6,  6,  6,  5,  3,  0,  0,  0,  0 },
  {  6,  6,  6,  6,  6,  4,  0,  0,  0,  0 },//13
  {  6,  6,  6,  6,  6,  5,  3,  0,  0,  0 },
  {  6,  6,  6,  6,  6,  6,  4,  0,  0,  0 },//15
  {  6,  6,  6,  6,  6,  6,  5,  3,  0,  0 },
  {  6,  6,  6,  6,  6,  6,  6,  4,  0,  0 },//17
  {  6,  6,  6,  6,  6,  6,  6,  5,  3,  0 },
  {  6,  6,  6,  6,  6,  6,  6,  6,  4,  0 },
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//20
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//21
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//22
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//23
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//24
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//25
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//26
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//27
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//28
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//29
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//30
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//31
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//32
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 },//33
  {  6,  6,  6,  6,  6,  6,  6,  6,  6,  0 }//34
};

int bardSlots[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  1,  0,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  3,  2,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  2,  0,  0,  0,  0,  0,  0,  0,  0 },//7
  {  3,  3,  1,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  3,  2,  0,  0,  0,  0,  0,  0,  0 },//9
  {  3,  3,  2,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  3,  3,  1,  0,  0,  0,  0,  0,  0 },//11
  {  3,  3,  3,  2,  0,  0,  0,  0,  0,  0 },
  {  3,  3,  3,  2,  0,  0,  0,  0,  0,  0 },//13
  {  3,  3,  3,  3,  1,  0,  0,  0,  0,  0 },
  {  4,  3,  3,  3,  2,  0,  0,  0,  0,  0 },//15
  {  4,  4,  3,  3,  2,  0,  0,  0,  0,  0 },
  {  4,  4,  4,  3,  3,  1,  0,  0,  0,  0 },//17
  {  4,  4,  4,  4,  3,  2,  0,  0,  0,  0 },
  {  4,  4,  4,  4,  4,  3,  0,  0,  0,  0 },
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//20
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//21
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//22
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//23
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//24
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//25
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//26
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//27
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//28
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//29
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//30
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//31
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//32
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 },//33
  {  4,  4,  4,  4,  4,  4,  0,  0,  0,  0 }//34
};
  

/** known spells for 1sorcs **/
int sorcererKnown[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  2,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  1,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  4,  2,  0,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  4,  2,  1,  0,  0,  0,  0,  0,  0,  0 },
  {  5,  3,  2,  0,  0,  0,  0,  0,  0,  0 },//7
  {  5,  3,  2,  1,  0,  0,  0,  0,  0,  0 },
  {  5,  4,  3,  2,  0,  0,  0,  0,  0,  0 },//9
  {  5,  4,  3,  2,  1,  0,  0,  0,  0,  0 },
  {  5,  5,  4,  3,  2,  0,  0,  0,  0,  0 },//11
  {  5,  5,  4,  3,  2,  1,  0,  0,  0,  0 },
  {  5,  5,  4,  4,  3,  2,  0,  0,  0,  0 },//13
  {  5,  5,  4,  4,  3,  2,  1,  0,  0,  0 },
  {  5,  5,  4,  4,  4,  3,  2,  0,  0,  0 },//15
  {  5,  5,  4,  4,  4,  3,  2,  1,  0,  0 },
  {  5,  5,  4,  4,  4,  3,  3,  2,  0,  0 },//17
  {  5,  5,  4,  4,  4,  3,  3,  2,  1,  0 },
  {  5,  5,  4,  4,  4,  3,  3,  3,  2,  0 },
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//20
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//21
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//22
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//23
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//24
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//25
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//26
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//27
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//28
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//29
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//30
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//31
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//32
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 },//33
  {  5,  5,  4,  4,  4,  3,  3,  3,  3,  0 }//34
};

/** known spells for bards **/
int bardKnown[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  2,  0,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  4,  3,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  4,  3,  0,  0,  0,  0,  0,  0,  0,  0 },//7
  {  4,  4,  2,  0,  0,  0,  0,  0,  0,  0 },
  {  4,  4,  3,  0,  0,  0,  0,  0,  0,  0 },//9
  {  4,  4,  3,  0,  0,  0,  0,  0,  0,  0 },
  {  4,  4,  4,  2,  0,  0,  0,  0,  0,  0 },//11
  {  4,  4,  4,  3,  0,  0,  0,  0,  0,  0 },
  {  4,  4,  4,  4,  2,  0,  0,  0,  0,  0 },//13
  {  4,  4,  4,  4,  3,  0,  0,  0,  0,  0 },
  {  4,  4,  4,  4,  3,  0,  0,  0,  0,  0 },//15
  {  5,  4,  4,  4,  4,  2,  0,  0,  0,  0 },
  {  5,  5,  4,  4,  4,  3,  0,  0,  0,  0 },//17
  {  5,  5,  5,  4,  4,  3,  0,  0,  0,  0 },
  {  5,  5,  5,  5,  4,  4,  0,  0,  0,  0 },
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//20
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//21
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//22
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//23
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//24
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//25
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//26
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//27
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//28
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//29
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//30
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//31
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//32
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 },//33
  {  5,  5,  5,  5,  5,  4,  0,  0,  0,  0 }//34
};  
  

int rangerSlots[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 1
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 2
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 3
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 4
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 6
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 7
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 8
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 9
  {  1,  1,  0,  0,  0,  0,  0,  0,  0,  0 },	// 10
  {  1,  1,  0,  0,  0,  0,  0,  0,  0,  0 },	// 11
  {  1,  1,  1,  0,  0,  0,  0,  0,  0,  0 },	// 12
  {  1,  1,  1,  0,  0,  0,  0,  0,  0,  0 },	// 13
  {  2,  1,  1,  0,  0,  0,  0,  0,  0,  0 },	// 14
  {  2,  1,  1,  1,  0,  0,  0,  0,  0,  0 },	// 15
  {  2,  2,  1,  1,  0,  0,  0,  0,  0,  0 },	// 16
  {  2,  2,  2,  1,  0,  0,  0,  0,  0,  0 },	// 17
  {  3,  2,  2,  1,  0,  0,  0,  0,  0,  0 },	// 18
  {  3,  3,  3,  2,  0,  0,  0,  0,  0,  0 },	// 19
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 20
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 21
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 22
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 23
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 24
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 25
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 26
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 27
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 28
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 29
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 30
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 31
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 32
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 33
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 }	// 34
};

int paladinSlots[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 1
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 2
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 3
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 4
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 6
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 7
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 8
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 9
  {  1,  1,  0,  0,  0,  0,  0,  0,  0,  0 },	// 10
  {  1,  1,  0,  0,  0,  0,  0,  0,  0,  0 },	// 11
  {  1,  1,  1,  0,  0,  0,  0,  0,  0,  0 },	// 12
  {  1,  1,  1,  0,  0,  0,  0,  0,  0,  0 },	// 13
  {  2,  1,  1,  0,  0,  0,  0,  0,  0,  0 },	// 14
  {  2,  1,  1,  1,  0,  0,  0,  0,  0,  0 },	// 15
  {  2,  2,  1,  1,  0,  0,  0,  0,  0,  0 },	// 16
  {  2,  2,  2,  1,  0,  0,  0,  0,  0,  0 },	// 17
  {  3,  2,  2,  1,  0,  0,  0,  0,  0,  0 },	// 18
  {  3,  3,  3,  2,  0,  0,  0,  0,  0,  0 },	// 19
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 20
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 21
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 22
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 23
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 24
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 25
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 26
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 27
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 28
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 29
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 30
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 31
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 32
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 },	// 33
  {  3,  3,  3,  3,  0,  0,  0,  0,  0,  0 }	// 34
};

int clericSlots[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  1,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  2,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  2,  1,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  3,  3,  2,  0,  0,  0,  0,  0,  0,  0 },
  {  4,  3,  2,  1,  0,  0,  0,  0,  0,  0 },
  {  4,  3,  3,  2,  0,  0,  0,  0,  0,  0 },
  {  4,  4,  3,  2,  1,  0,  0,  0,  0,  0 },
  {  4,  4,  3,  3,  2,  0,  0,  0,  0,  0 },	// 10
  {  5,  4,  4,  3,  2,  1,  0,  0,  0,  0 },
  {  5,  4,  4,  3,  3,  2,  0,  0,  0,  0 },
  {  5,  5,  4,  4,  3,  2,  1,  0,  0,  0 },
  {  5,  5,  4,  4,  3,  3,  2,  0,  0,  0 },
  {  5,  5,  5,  4,  4,  3,  2,  1,  0,  0 },	// 15
  {  5,  5,  5,  4,  4,  3,  3,  2,  0,  0 },
  {  5,  5,  5,  5,  4,  4,  3,  2,  1,  0 },
  {  5,  5,  5,  5,  4,  4,  3,  3,  2,  0 },
  {  5,  5,  5,  5,  5,  4,  4,  3,  3,  0 },
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 20
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 21
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 22
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 23
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 24
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 25
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 26
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 27
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 28
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 29
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 30
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 31
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 32
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 33
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 }	// 34
};

int druidSlots[LVL_IMPL + 1][10] = {
// 1st,2nd,3rd,4th,5th,6th,7th,8th,9th,10th
  {  0,  0,  0,  0,  0,  0,  0,  0,  0,  0 },	// 0
  {  1,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  0,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  2,  1,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  2,  0,  0,  0,  0,  0,  0,  0,  0 },
  {  3,  2,  1,  0,  0,  0,  0,  0,  0,  0 },	// 5
  {  3,  3,  2,  0,  0,  0,  0,  0,  0,  0 },
  {  4,  3,  2,  1,  0,  0,  0,  0,  0,  0 },
  {  4,  3,  3,  2,  0,  0,  0,  0,  0,  0 },
  {  4,  4,  3,  2,  1,  0,  0,  0,  0,  0 },
  {  4,  4,  3,  3,  2,  0,  0,  0,  0,  0 },	// 10
  {  5,  4,  4,  3,  2,  1,  0,  0,  0,  0 },
  {  5,  4,  4,  3,  3,  2,  0,  0,  0,  0 },
  {  5,  5,  4,  4,  3,  2,  1,  0,  0,  0 },
  {  5,  5,  4,  4,  3,  3,  2,  0,  0,  0 },
  {  5,  5,  5,  4,  4,  3,  2,  1,  0,  0 },	// 15
  {  5,  5,  5,  4,  4,  3,  3,  2,  0,  0 },
  {  5,  5,  5,  5,  4,  4,  3,  2,  1,  0 },
  {  5,  5,  5,  5,  4,  4,  3,  3,  2,  0 },
  {  5,  5,  5,  5,  5,  4,  4,  3,  3,  0 },
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 20
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 21
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 22
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 23
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 24
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 25
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 26
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 27
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 28
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 29
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 30
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 31
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 32
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 },	// 33
  {  5,  5,  5,  5,  5,  4,  4,  4,  4,  0 }	// 34
};


/*** Utility Functions needed for memorization ***/


// is character currently occupied with memming of some sort?
int isOccupied(struct char_data *ch) {
  int i;

  for (i = 0; i < NUM_CASTERS; i++)
    if (PRAYIN(ch, i))
      return TRUE;

  return FALSE;
}


// initialize all the spell slots of the character
void init_spell_slots(struct char_data *ch)
{
  int slot, x;

  if (!ch) {
    return;
  }

  for (slot = 0; slot < MAX_MEM; slot++) {
    for (x = 0; x < NUM_CASTERS; x++) {
      PRAYING(ch, slot, x) = 0;
      PRAYED(ch, slot, x) = 0;
      PRAYTIME(ch, slot, x) = 0;
    }
  }
  for (x = 0; x < NUM_CASTERS; x++)
    PRAYIN(ch, x) = FALSE;
}


// given class and spellnum, returns spells circle
int spellCircle(int class, int spellnum)
{
  switch (class) {
    case CLASS_BARD:
      switch (spell_info[spellnum].min_level[class]) {
        case 3:
        case 4:
          return 1;
        case 5:
        case 6:
        case 7:
          return 2;
        case 8:
        case 9:
        case 10:
          return 3;
        case 11:
        case 12:
        case 13:
          return 4;
        case 14:
        case 15:
        case 16:
          return 5;
        case 17:
        case 18:
        case 19:
          return 6;
        // don't use 20, that is reserved for epic!  case 20:
        default:
          return 99;
      }
      return 1;      
    case CLASS_SORCERER:
      return ((MAX(1, (spell_info[spellnum].min_level[class]) / 2)));
    /* can get confusing, just check out class.c to see what level
       they get their circles at in the spell_level function */
    case CLASS_PALADIN:
      switch (spell_info[spellnum].min_level[class]) {
        case 6:
        case 7:
        case 8:
        case 9:
          return 1;
        case 10:
        case 11:
          return 2;
        case 12:
        case 13:
        case 14:
          return 3;
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
          return 4;
        default:
          return 99;
      }
      return 1;
    /* can get confusing, just check out class.c to see what level
       they get their circles at in the spell_level function */
    case CLASS_RANGER:
      switch (spell_info[spellnum].min_level[class]) {
        case 6:
        case 7:
        case 8:
        case 9:
          return 1;
        case 10:
        case 11:
          return 2;
        case 12:
        case 13:
        case 14:
          return 3;
        case 15:
        case 16:
        case 17:
        case 18:
        case 19:
        case 20:
          return 4;
        default:
          return 99;
      }
      return 1;      
    /* wizard, druid, cleric */      
    default:
      return ((int)((spell_info[spellnum].min_level[class] + 1) / 2));
  }
}

  // *note remember in both constant arrays, value 0 is circle 1
  // and probably should eventually change that for uniformity
// returns # of total slots based on level, class and stat bonus
// of given circle
int comp_slots(struct char_data *ch, int circle, int class)
{
  int spellSlots = 0;
  
  /* they don't even have access to this circle */
  if (getCircle(ch, class) < circle)
    return 0;
  
  switch(class) {
    case CLASS_RANGER:
      spellSlots += spell_bonus[GET_WIS(ch)][circle-1];
      spellSlots += rangerSlots[CLASS_LEVEL(ch, class)][circle-1];
      break;    
    case CLASS_PALADIN:
      spellSlots += spell_bonus[GET_WIS(ch)][circle-1];
      spellSlots += paladinSlots[CLASS_LEVEL(ch, class)][circle-1];
      break;
    case CLASS_CLERIC:
      spellSlots += spell_bonus[GET_WIS(ch)][circle-1];
      spellSlots += clericSlots[CLASS_LEVEL(ch, class)][circle-1];
      break;
    case CLASS_DRUID:
      spellSlots += spell_bonus[GET_WIS(ch)][circle-1];
      spellSlots += druidSlots[CLASS_LEVEL(ch, class)][circle-1];
      break;
    case CLASS_WIZARD:
      spellSlots += spell_bonus[GET_INT(ch)][circle-1];
      spellSlots += wizardSlots[CLASS_LEVEL(ch, class)][circle-1];
      break;
    case CLASS_SORCERER:
      spellSlots += spell_bonus[GET_CHA(ch)][circle-1];
      spellSlots += sorcererSlots[CLASS_LEVEL(ch, class)][circle-1];
      break;
    case CLASS_BARD:
      spellSlots += spell_bonus[GET_CHA(ch)][circle-1];
      spellSlots += bardSlots[CLASS_LEVEL(ch, class)][circle-1];
      break;
    default:
      if (GET_LEVEL(ch) < LVL_IMMORT) {
        log("Invalid class passed to comp_slots.");
      }
      return (-1);  
  }  

  return spellSlots;
}


// SORCERER types:   this will add circle-slot to the mem list
//   and the corresponding memtime in memtime list
// WIZARD types:  adds <spellnum> to the characters memorizing list, and
//   places the corresponding memtime in memtime list
#define SORC_TIME_FACTOR  12
#define BARD_TIME_FACTOR  14
void addSpellMemming(struct char_data *ch, int spellnum, int time, int class)
{
  int slot;

  /* sorcerer type system */
  if (class == CLASS_SORCERER) {
    /* replace spellnum with its circle */
    spellnum = spellCircle(class, spellnum);
    /* replace time with slot-mem-time */
    time = SORC_TIME_FACTOR * spellnum;
  } 
  else if (class == CLASS_BARD) {
    /* replace spellnum with its circle */
    spellnum = spellCircle(class, spellnum);
    /* replace time with slot-mem-time */
    time = BARD_TIME_FACTOR * spellnum;
  } 


  /* wizard type system */
  for (slot = 0; slot < MAX_MEM; slot++) {
    if (PRAYING(ch, slot, classArray(class)) == TERMINATE) {
      PRAYING(ch, slot, classArray(class)) = spellnum;
      PRAYTIME(ch, slot, classArray(class)) = time;
      break;
    }
  }
}


// resets the memtimes for character (in case of aborted studies)
void resetMemtimes(struct char_data *ch, int class)
{
  int slot;

  for (slot = 0; slot < MAX_MEM; slot++) {
    if (PRAYING(ch, slot, classArray(class)) == TERMINATE)
      break;

    /* the formula for metime for sorcs is just factor*circle
     * which is conveniently equal to the corresponding PRAYING()
     * slot
     */
    if (class == CLASS_SORCERER)
      PRAYTIME(ch, slot, classArray(class)) =
            PRAYING(ch, slot, classArray(class)) * SORC_TIME_FACTOR;
    else if (class == CLASS_BARD)
      PRAYTIME(ch, slot, classArray(class)) =
            PRAYING(ch, slot, classArray(class)) * BARD_TIME_FACTOR;
    else
      PRAYTIME(ch, slot, classArray(class)) = 
            spell_info[PRAYING(ch, slot, classArray(class))].memtime;
  }
}


// adds <spellnum> to the next available slot in the characters
// memorized list
void addSpellMemmed(struct char_data *ch, int spellnum, int class)
{
  int slot;

  for (slot = 0; slot < MAX_MEM; slot++) {
    if (PRAYED(ch, slot, classArray(class)) == 0) {
      PRAYED(ch, slot, classArray(class)) = spellnum;
      return;
    }
  }
}

// SORCERER types:  just clears top of PRAYING() list
// WIZARD types:  finds the first instance of <spellnum> in the characters
//   memorizing spells, forgets it, then updates the memorizing list
void removeSpellMemming(struct char_data *ch, int spellnum, int class)
{
  int slot, nextSlot;

  if (classArray(class) == -1)
    return;
  
  /* sorcerer-types */
  if (class == CLASS_SORCERER) {
    // iterate until we find 0 (terminate) or end of array
    for (slot = 0;
            (PRAYING(ch, slot, classArray(class)) || slot < (MAX_MEM - 1));
            slot++) {
      // shift everything over
      PRAYING(ch, slot, classArray(class)) =
                  PRAYING(ch, slot + 1, classArray(class));
      PRAYTIME(ch, slot, classArray(class)) =
                  PRAYTIME(ch, slot + 1, classArray(class));
    }
    return;
  }
  else if (class == CLASS_BARD) {
    // iterate until we find 0 (terminate) or end of array
    for (slot = 0;
            (PRAYING(ch, slot, classArray(class)) || slot < (MAX_MEM - 1));
            slot++) {
      // shift everything over
      PRAYING(ch, slot, classArray(class)) =
                  PRAYING(ch, slot + 1, classArray(class));
      PRAYTIME(ch, slot, classArray(class)) =
                  PRAYTIME(ch, slot + 1, classArray(class));
    }
    return;
  }
  
  /* wizard-types */
  for (slot = 0; slot < MAX_MEM; slot++) {
    if (PRAYING(ch, slot, classArray(class)) == spellnum) { //found the spell
      /* is there more in the memming list? */
      if (PRAYING(ch, slot + 1, classArray(class)) != TERMINATE) {
        for (nextSlot = slot; nextSlot < MAX_MEM - 1; nextSlot++) { 
          //go through rest of list and shift everything
          PRAYING(ch, nextSlot, classArray(class)) =
                  PRAYING(ch, nextSlot + 1, classArray(class));
          PRAYTIME(ch, nextSlot, classArray(class)) =
                  PRAYTIME(ch, nextSlot + 1, classArray(class));
        }
        // tag end of list with 'terminate'
        PRAYING(ch, nextSlot, classArray(class)) = TERMINATE;
        PRAYTIME(ch, nextSlot, classArray(class)) = TERMINATE;
      } else {
        // must be the spell found was last in list
        PRAYING(ch, slot, classArray(class)) = TERMINATE;
        PRAYTIME(ch, slot, classArray(class)) = TERMINATE;
      }
      return;
    }
  }
}


// finds the first instance of <spellnum> in the characters
//   memorized spells, forgets it, then updates the memorized list
// *for now it has to do - will extract a wizard spell first
//   if you can't find it in wizard, then take it out of cleric, etc
// *returns class
int forgetSpell(struct char_data *ch, int spellnum, int class)
{
  int slot, nextSlot, x = 0;

  /* we know the class */
  if (class != -1) {  
    if (PRAYED(ch, 0, classArray(class))) {
      for (slot = 0; slot < (MAX_MEM); slot++) {
        if (PRAYED(ch, slot, classArray(class)) == spellnum) {
          if (PRAYED(ch, slot + 1, classArray(class)) != 0) {
            for (nextSlot = slot; nextSlot < (MAX_MEM) - 1; nextSlot++) {
              PRAYED(ch, nextSlot, classArray(class)) =
                    PRAYED(ch, nextSlot + 1, classArray(class));
            }
            PRAYED(ch, nextSlot, classArray(class)) = 0;
          } else {
            PRAYED(ch, slot, classArray(class)) = 0;
          }
          return class;
        }
      }
    }
  } else { /* class == -1 */
    /* we don't know the class, so search all the arrays */

    for (x = 0; x < NUM_CLASSES; x++) {
      if (x == CLASS_SORCERER) /* checking this separately */
        continue;
      if (x == CLASS_BARD) /* checking this separately */
        continue;
      if (classArray(x) == -1) /* not caster */
        continue;
      if (PRAYED(ch, 0, classArray(x))) {
        for (slot = 0; slot < (MAX_MEM); slot++) {
          if (PRAYED(ch, slot, classArray(x)) == spellnum) {
            if (PRAYED(ch, slot + 1, classArray(x)) != 0) {
              for (nextSlot = slot; nextSlot < (MAX_MEM) - 1; nextSlot++) {
                PRAYED(ch, nextSlot, classArray(x)) =
                    PRAYED(ch, nextSlot + 1, classArray(x));
              }
              PRAYED(ch, nextSlot, classArray(x)) = 0;
            } else {
              PRAYED(ch, slot, classArray(x)) = 0;
            }
            return x;
          }
        }
      }    
    } /* we found nothing so far*/
    /* check sorc-type arrays */
    if (CLASS_LEVEL(ch, CLASS_SORCERER)) {
      /* got a free slot? */
      if (hasSpell(ch, spellnum)) {
        addSpellMemming(ch, spellnum, 0, CLASS_SORCERER);
        return CLASS_SORCERER;
      }
    }
    if (CLASS_LEVEL(ch, CLASS_BARD)) {
      /* got a free slot? */
      if (hasSpell(ch, spellnum)) {
        addSpellMemming(ch, spellnum, 0, CLASS_BARD);
        return CLASS_BARD;
      }
    }
    
  }

  /* failed to find anything */
  return -1;
}


// wizard-types:  returns total spells in both MEMORIZED and MEMORIZING of a
//   given circle
// sorc-types:  returns total spell slots used in a given circle
int numSpells(struct char_data *ch, int circle, int class)
{
  int num = 0, slot;

  /* sorc types */
  if (class == CLASS_SORCERER) {
    for (slot = 0; slot < (MAX_MEM); slot++) {
      if (PRAYING(ch, slot, classArray(class)) == circle)
        num++;
    }
  }
  else if (class == CLASS_BARD) {
    for (slot = 0; slot < (MAX_MEM); slot++) {
      if (PRAYING(ch, slot, classArray(class)) == circle)
        num++;
    }
  }
  else {
    /* wizard-types */
    for (slot = 0; slot < (MAX_MEM); slot++) {
      if (spellCircle(class, PRAYED(ch, slot, classArray(class))) == circle)
        num++;
      if (spellCircle(class, PRAYING(ch, slot, classArray(class))) == circle)
        num++;
    }
  }

  return (num);
}


/* for sorc-types:  counts how many spells you have of a given circle */
int count_sorc_known(struct char_data *ch, int circle, int class)
{
  int num = 0, slot;
  
  for (slot = 0; slot < MAX_MEM; slot++) {
    if (class == CLASS_SORCERER) {
      if (spellCircle(CLASS_SORCERER,
            PRAYED(ch, slot, classArray(CLASS_SORCERER))) == circle)
        num++;
    }
    else if (class == CLASS_BARD) {
      if (spellCircle(CLASS_BARD,
            PRAYED(ch, slot, classArray(CLASS_BARD))) == circle)
        num++;
    }

  }
  return num;
}


/* For Sorc-types:  Checks if they know the given spell or not */
bool sorcKnown(struct char_data *ch, int spellnum, int class)
{
  int slot;
  
  // for zusuk testing
  /*
  if (GET_LEVEL(ch) == LVL_IMPL)
    return TRUE;
  */
  
  for (slot = 0; slot < MAX_MEM; slot++) {
    if (class == CLASS_SORCERER) {
      if (PRAYED(ch, slot, classArray(CLASS_SORCERER)) == spellnum)
        return TRUE;
    }
    else if (class == CLASS_BARD) {
      if (PRAYED(ch, slot, classArray(CLASS_BARD)) == spellnum)
        return TRUE;
    }

  }
  return FALSE;
}


/* For Sorc-types:  finds spellnum in their known list and extracts it */
void sorc_extract_known(struct char_data *ch, int spellnum, int class)
{
  int slot, nextSlot;
    
  for (slot = 0; slot < MAX_MEM; slot++) {
    if (PRAYED(ch, slot, classArray(class)) == spellnum) { //found the spell
      /* is there more in the list? */
      if (PRAYED(ch, slot + 1, classArray(class)) != TERMINATE) {
        for (nextSlot = slot; nextSlot < MAX_MEM - 1; nextSlot++) { 
          //go through rest of list and shift everything
          PRAYED(ch, nextSlot, classArray(class)) =
                  PRAYED(ch, nextSlot + 1, classArray(class));
        }
        // tag end of list with 'terminate'
        PRAYED(ch, nextSlot, classArray(class)) = TERMINATE;
      } else {
        // must be the spell found was last in list
        PRAYED(ch, slot, classArray(class)) = TERMINATE;
      }
      return;
    }
  }
  
  return;
}


/* For Sorc-types:  adds spellnum to their known list */
/* returns 0 failure, returns 1 success*/
int sorc_add_known(struct char_data *ch, int spellnum, int class)
{
  int slot, circle;
  
  circle = spellCircle(class, spellnum);
  
  if ((sorcererKnown[CLASS_LEVEL(ch, class)][circle-1] -
          count_sorc_known(ch, circle, class)) <= 0)
    return FALSE;
  
  for (slot = 0; slot < MAX_MEM; slot++) {
    if (PRAYED(ch, slot, classArray(class)) == TERMINATE) {
      /* found an empty slot! */
      PRAYED(ch, slot, classArray(class)) = spellnum;
      return TRUE;
    }
  }
  
  return FALSE;
}


// for SORCERER types:  returns TRUE if they know the spell AND if they
//   got free slots
// for WIZARD types:  returns TRUE if the character has the spell memorized
//   returns FALSE if the character doesn't
bool hasSpell(struct char_data *ch, int spellnum)
{
  int slot, x;

  // could check to see what classes ch has to speed up this search

  for (x = 0; x < NUM_CLASSES; x++) {
    if (classArray(x) == -1)
      continue;
    if (x == CLASS_SORCERER)
      continue;
    if (x == CLASS_BARD)
      continue;
    for (slot = 0; slot < MAX_MEM; slot++) {
      if (PRAYED(ch, slot, classArray(x)) == spellnum)
        return TRUE;
    }
  }

  /* check our sorc-type system */
  if (CLASS_LEVEL(ch, CLASS_SORCERER)) {
    // is this one of the "known" spells?
    if (sorcKnown(ch, spellnum, CLASS_SORCERER)) {
      int circle = spellCircle(CLASS_SORCERER, spellnum);
      // do we have any slots left?
      // take total slots for the correct circle and subtract from used
      if ((comp_slots(ch, circle, CLASS_SORCERER) - 
              numSpells(ch, circle, CLASS_SORCERER)) > 0)
        return TRUE;
    }
  }
  else if (CLASS_LEVEL(ch, CLASS_BARD)) {
    // is this one of the "known" spells?
    if (sorcKnown(ch, spellnum, CLASS_BARD)) {
      int circle = spellCircle(CLASS_BARD, spellnum);
      // do we have any slots left?
      // take total slots for the correct circle and subtract from used
      if ((comp_slots(ch, circle, CLASS_BARD) - 
              numSpells(ch, circle, CLASS_BARD)) > 0)
        return TRUE;
    }
  }

  
  return FALSE;
}


// returns the characters highest circle access in a given class
int getCircle(struct char_data *ch, int class)
{
  // npc's default to best chart
  if (IS_NPC(ch)) {
    return (MAX(1, MIN(9, (GET_LEVEL(ch) + 1) / 2)));
  }

  // if pc has no caster classes, he/she has no business here
  if (!IS_CASTER(ch)) {
    return (-1);
  }

  if (!CLASS_LEVEL(ch, class)) {
    return 0;
  }

  switch (class) {
    case CLASS_PALADIN:
      if (CLASS_LEVEL(ch, CLASS_PALADIN) < 6)
        return 0;
      else if (CLASS_LEVEL(ch, CLASS_PALADIN) < 10)
        return 1;
      else if (CLASS_LEVEL(ch, CLASS_PALADIN) < 12)
        return 2;
      else if (CLASS_LEVEL(ch, CLASS_PALADIN) < 15)
        return 3;
      else
        return 4;
    case CLASS_RANGER:
      if (CLASS_LEVEL(ch, CLASS_RANGER) < 6)
        return 0;
      else if (CLASS_LEVEL(ch, CLASS_RANGER) < 10)
        return 1;
      else if (CLASS_LEVEL(ch, CLASS_RANGER) < 12)
        return 2;
      else if (CLASS_LEVEL(ch, CLASS_RANGER) < 15)
        return 3;
      else
        return 4;      
    case CLASS_BARD:
      if (CLASS_LEVEL(ch, CLASS_BARD) < 3)
        return 0;
      else if (CLASS_LEVEL(ch, CLASS_BARD) < 5)
        return 1;
      else if (CLASS_LEVEL(ch, CLASS_BARD) < 8)
        return 2;
      else if (CLASS_LEVEL(ch, CLASS_BARD) < 11)
        return 3;
      else if (CLASS_LEVEL(ch, CLASS_BARD) < 14)
        return 4;
      else if (CLASS_LEVEL(ch, CLASS_BARD) < 17)
        return 5;
      else
        return 6;      
    case CLASS_SORCERER:
      return (MAX(1, (MIN(9, CLASS_LEVEL(ch, class) / 2))));
    case CLASS_WIZARD:
      return (MAX(1, MIN(9, (CLASS_LEVEL(ch, class) + 1) / 2)));
    case CLASS_DRUID:
      return (MAX(1, MIN(9, (CLASS_LEVEL(ch, class) + 1) / 2)));
    case CLASS_CLERIC:
      return (MAX(1, MIN(9, (CLASS_LEVEL(ch, class) + 1) / 2)));
    default:
      return (MAX(1, MIN(9, (CLASS_LEVEL(ch, class) + 1) / 2)));
  }

}


/********** end utility ***************/


/*********** Event Engine ************/


// updates the characters memorizing list/memtime, and
// memorizes the spell upon completion
void updateMemming(struct char_data *ch, int class)
{
  int bonus = 1;

  if (classArray(class) == -1)
    return;
  
  //calaculate memtime bonus based on concentration
  if (!IS_NPC(ch) && GET_ABILITY(ch, ABILITY_CONCENTRATION)) {
    bonus = MAX(1, compute_ability(ch, ABILITY_CONCENTRATION) / 2 - 3);
  }

  //if you aren't resting, can't mem; same with fighting
  if (GET_POS(ch) != POS_RESTING || FIGHTING(ch)) {
    switch (class) {
      case CLASS_SORCERER:
        send_to_char(ch, "Your meditation is interrupted.\r\n");
        act("$n aborts $s meditation.", FALSE, ch, 0, 0, TO_ROOM);
        break;
      case CLASS_WIZARD:
        send_to_char(ch, "You abort your studies.\r\n");
        act("$n aborts $s studies.", FALSE, ch, 0, 0, TO_ROOM);
        break;
      case CLASS_CLERIC:
        send_to_char(ch, "You abort your prayers.\r\n");
        act("$n aborts $s prayers.", FALSE, ch, 0, 0, TO_ROOM);
        break;
      case CLASS_PALADIN:
        send_to_char(ch, "You abort your chant.\r\n");
        act("$n aborts $s chant.", FALSE, ch, 0, 0, TO_ROOM);
        break;
      case CLASS_RANGER:
        send_to_char(ch, "You abort your adjuration.\r\n");
        act("$n aborts $s adjuration.", FALSE, ch, 0, 0, TO_ROOM);
        break;
      case CLASS_DRUID:
        send_to_char(ch, "You abort your communing.\r\n");
        act("$n aborts $s communing.", FALSE, ch, 0, 0, TO_ROOM);
        break;
    }
    resetMemtimes(ch, class);
    PRAYIN(ch, classArray(class)) = FALSE;
    return;
  }

  // no mem list
  if (PRAYING(ch, 0, classArray(class)) == TERMINATE) {
    PRAYIN(ch, classArray(class)) = FALSE;
    return;
  }
  
  // wizard spellbook requirement
  if (class == CLASS_WIZARD &&
          !spellbook_ok(ch, PRAYING(ch, 0, classArray(class)), CLASS_WIZARD, FALSE)
          ) {
    send_to_char(ch, "You don't seem to have that spell in your spellbook!\r\n");
    resetMemtimes(ch, class);
    PRAYIN(ch, classArray(class)) = FALSE;
    return;
  }

  // continue memorizing
  PRAYTIME(ch, 0, classArray(class)) -= bonus;
  if (PRAYTIME(ch, 0, classArray(class)) <= 0) {
    switch (class) {
      case CLASS_CLERIC:
        sprintf(buf, "You finish praying for %s.\r\n",
                spell_info[PRAYING(ch, 0, classArray(class))].name);
        addSpellMemmed(ch, PRAYING(ch, 0, classArray(class)), class);
        break;
      case CLASS_RANGER:
        sprintf(buf, "You finish adjuring for %s.\r\n",
                spell_info[PRAYING(ch, 0, classArray(class))].name);
        addSpellMemmed(ch, PRAYING(ch, 0, classArray(class)), class);
        break;        
      case CLASS_PALADIN:
        sprintf(buf, "You finish chanting for %s.\r\n",
                spell_info[PRAYING(ch, 0, classArray(class))].name);
        addSpellMemmed(ch, PRAYING(ch, 0, classArray(class)), class);
        break;
      case CLASS_DRUID:
        sprintf(buf, "You finish communing for %s.\r\n",
                spell_info[PRAYING(ch, 0, classArray(class))].name);
        addSpellMemmed(ch, PRAYING(ch, 0, classArray(class)), class);
        break;
      case CLASS_SORCERER:
        sprintf(buf, "You have recovered a spell slot: %d.\r\n",
                PRAYING(ch, 0, classArray(class)));
        break;
      case CLASS_BARD:
        sprintf(buf, "You have recovered a compose slot: %d.\r\n",
                PRAYING(ch, 0, classArray(class)));
        break;
      default: // wizard
        sprintf(buf, "You finish memorizing %s.\r\n",
                spell_info[PRAYING(ch, 0, classArray(class))].name);
        addSpellMemmed(ch, PRAYING(ch, 0, classArray(class)), class);
        break;        
    }
    send_to_char(ch, buf);
    removeSpellMemming(ch, PRAYING(ch, 0, classArray(class)), class);
    if (PRAYING(ch, 0, classArray(class)) == TERMINATE) {
      switch (class) {
        case CLASS_SORCERER:
          send_to_char(ch, "Your meditations are complete.\r\n");
          act("$n completes $s meditation.", FALSE, ch, 0, 0, TO_ROOM);
          break;
        case CLASS_BARD:
          send_to_char(ch, "Your compositions are complete.\r\n");
          act("$n completes $s compositions.", FALSE, ch, 0, 0, TO_ROOM);
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "Your prayers are complete.\r\n");
          act("$n completes $s prayers.", FALSE, ch, 0, 0, TO_ROOM);
          break;
        case CLASS_RANGER:
          send_to_char(ch, "Your adjuring session is complete.\r\n");
          act("$n completes $s adjuration.", FALSE, ch, 0, 0, TO_ROOM);
          break;          
        case CLASS_PALADIN:
          send_to_char(ch, "Your chanting is complete.\r\n");
          act("$n completes $s chant.", FALSE, ch, 0, 0, TO_ROOM);
          break;
        case CLASS_DRUID:
          send_to_char(ch, "Your communing is complete.\r\n");
          act("$n completes $s communing.", FALSE, ch, 0, 0, TO_ROOM);
          break;
        default: // wizard
          send_to_char(ch, "Your studies are complete.\r\n");
          act("$n completes $s studies.", FALSE, ch, 0, 0, TO_ROOM);
          break;        
      }
      PRAYIN(ch, classArray(class)) = FALSE;
      return;
    }
  }
  NEW_EVENT(eMEMORIZING, ch, NULL, 1 * PASSES_PER_SEC);

}


EVENTFUNC(event_memorizing)
{
  int x = 0;
  struct char_data *ch;
  struct mud_event_data *pMudEvent;
  
  //initialize everything and dummy checks 
  if (event_obj == NULL) return 0;
  pMudEvent = (struct mud_event_data *) event_obj;
  ch = (struct char_data *) pMudEvent->pStruct;

  for (x = 0; x < NUM_CLASSES; x++) {
    if (classArray(x) == -1)
      continue;
    if (PRAYIN(ch, classArray(x))) {
      updateMemming(ch, x);
      return 0;
    }
  }

  return 0;
}

/*************  end event engine ***************/


/************ display functions ***************/

// display sorc interface
void display_sorc(struct char_data *ch, int class)
{
  int slot;

  send_to_char(ch, "\tCTotal Slots:\r\n");
  
  if (class == CLASS_SORCERER) {
    for (slot = 1; slot <= getCircle(ch, CLASS_SORCERER); slot++) {
      send_to_char(ch, "\tM%d:\tm %d  ", slot, comp_slots(ch, slot, CLASS_SORCERER));
    }
    send_to_char(ch, "\r\n\r\n\tCSlots Used:\r\n");
    for (slot = 1; slot <= getCircle(ch, CLASS_SORCERER); slot++) {
      send_to_char(ch, "\tM%d:\tm %d  ", slot, numSpells(ch, slot, CLASS_SORCERER));
    }
    send_to_char(ch, "\r\n\r\n\tCSlots Left:\r\n");
    for (slot = 1; slot <= getCircle(ch, CLASS_SORCERER); slot++) {
      send_to_char(ch, "\tM%d:\tm %d  ", slot, comp_slots(ch, slot, CLASS_SORCERER) -
          numSpells(ch, slot, CLASS_SORCERER));
    }
    send_to_char(ch, "\tn\r\n\r\n");
    if (PRAYING(ch, 0, classArray(CLASS_SORCERER)))
      send_to_char(ch, "\tCTime left for next slot to recover:"
              "  \tn%d\tC seconds.\tn\r\n",
              PRAYTIME(ch, 0, classArray(CLASS_SORCERER)));  
  }
  else if (class == CLASS_BARD) {
    for (slot = 1; slot <= getCircle(ch, CLASS_BARD); slot++) {
      send_to_char(ch, "\tM%d:\tm %d  ", slot, comp_slots(ch, slot, CLASS_BARD));
    }
    send_to_char(ch, "\r\n\r\n\tCSlots Used:\r\n");
    for (slot = 1; slot <= getCircle(ch, CLASS_BARD); slot++) {
      send_to_char(ch, "\tM%d:\tm %d  ", slot, numSpells(ch, slot, CLASS_BARD));
    }
    send_to_char(ch, "\r\n\r\n\tCSlots Left:\r\n");
    for (slot = 1; slot <= getCircle(ch, CLASS_BARD); slot++) {
      send_to_char(ch, "\tM%d:\tm %d  ", slot, comp_slots(ch, slot, CLASS_BARD) -
          numSpells(ch, slot, CLASS_BARD));
    }
    send_to_char(ch, "\tn\r\n\r\n");
    if (PRAYING(ch, 0, classArray(CLASS_BARD)))
      send_to_char(ch, "\tCTime left for next slot to recover:"
              "  \tn%d\tC seconds.\tn\r\n",
              PRAYTIME(ch, 0, classArray(CLASS_BARD)));  
  }
  
}


// display memmed or prayed list
void display_memmed(struct char_data*ch, int class)
{
  int slot, memSlot, num[MAX_SPELLS];
  bool printed;
  
  if (classArray(class) == -1)
    return;

  //initialize an array size of MAX_SPELLS
  for (slot = 0; slot < MAX_SPELLS; slot++)
    num[slot] = 0;

  //increment the respective spellnum slot in array
  //according to # of spells memmed
  for (slot = 0; slot < (MAX_MEM); slot++) {
    if (PRAYED(ch, slot, classArray(class)) == TERMINATE)
      break;
    else
      num[PRAYED(ch, slot, classArray(class))]++;
  }

  /***  display memorized spells ***/
  if (PRAYED(ch, 0, classArray(class)) != 0) {
    switch (class) {
      case CLASS_DRUID:
        send_to_char(ch, "\r\n\tGYou have communed for the following"
                " spells:\r\n\r\n");
        break;
      case CLASS_CLERIC:
        send_to_char(ch, "\r\n\tGYou have prayed for the following"
                " spells:\r\n\r\n");
        break;
      case CLASS_PALADIN:
        send_to_char(ch, "\r\n\tGYou have chanted for the following"
                " spells:\r\n\r\n");
        break;
      case CLASS_RANGER:
        send_to_char(ch, "\r\n\tGYou have adjured for the following"
                " spells:\r\n\r\n");
        break;
      default:  /* wizard */
        send_to_char(ch, "\r\n\tGYou have memorized the following"
                " spells:\r\n\r\n");
        break;
    }
    for (slot = getCircle(ch, class); slot > 0; slot--) {
      printed = FALSE;
      for (memSlot = 0; memSlot < (MAX_MEM); memSlot++) {
        if (PRAYED(ch, memSlot, classArray(class)) != 0 &&
            spellCircle(class, PRAYED(ch, memSlot, classArray(class))) == 
                slot) {
          if (num[PRAYED(ch, memSlot, classArray(class))] != 0) {
            if (!printed) {
              send_to_char(ch, "[Circle: %d]          %2d - %s\r\n",
			            slot, num[PRAYED(ch, memSlot, classArray(class))],
			            spell_info[PRAYED(ch, memSlot, 
                                      classArray(class))].name);
              printed = TRUE;
              num[PRAYED(ch, memSlot, classArray(class))] = 0;
            } else {
              send_to_char(ch, "                     %2d - %s\r\n",
			            num[PRAYED(ch, memSlot, classArray(class))],
			            spell_info[PRAYED(ch, memSlot, 
                                      classArray(class))].name);
              num[PRAYED(ch, memSlot, classArray(class))] = 0;
            }
          }
        }
      }
    }
  }

  send_to_char(ch, "\tn");
}


// displays current memming list
void display_memming(struct char_data *ch, int class)
{
  int slot = 0;
  int spellLevel = 0;
  
  if (classArray(class) == -1)
    return;

  /*** Display memorizing spells ***/
  if (PRAYING(ch, 0, classArray(class)) != 0) {
    if (PRAYIN(ch, classArray(class))) {
      switch (class) {
        case CLASS_DRUID:
          send_to_char(ch, "\r\n\tCYou are currently communing for:\r\n");
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "\r\n\tCYou are currently praying for:\r\n");
          break;
        case CLASS_RANGER:
          send_to_char(ch, "\r\n\tCYou are currently adjuring for:\r\n");
          break;
        case CLASS_PALADIN:
          send_to_char(ch, "\r\n\tCYou are currently chanting for:\r\n");
          break;
        default:  /* wizard */
          send_to_char(ch, "\r\n\tCYou are currently memorizing:\r\n");
          break;
      }
    } else {
      switch (class) {
        case CLASS_DRUID:
          send_to_char(ch, "\r\n\tCYou are ready to commune for: (type 'rest' "
                       "then 'commune' to continue)\r\n");
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "\r\n\tCYou are ready to pray for: (type 'rest' "
                       "then 'pray' to continue)\r\n");
          break;
        case CLASS_RANGER:
          send_to_char(ch, "\r\n\tCYou are ready to adjure for: (type 'rest'"
                       " then 'adjure' to continue)\r\n");
          break;
        case CLASS_PALADIN:
          send_to_char(ch, "\r\n\tCYou are ready to chant for: (type 'rest'"
                       " then 'chant' to continue)\r\n");
          break;
        default:  /* wizard */
          send_to_char(ch, "\r\n\tCYou are ready to memorize: (type 'rest' "
                       "then 'memorize' to continue)\r\n");
          break;
      }
    }
    for (slot = 0; slot < (MAX_MEM); slot++) {
      if (PRAYING(ch, slot, classArray(class)) != 0) {
        spellLevel = spellCircle(class, PRAYING(ch, slot, classArray(class)));
        send_to_char(ch, "  %s [%d%s] with %d seconds remaining.\r\n",
                     spell_info[PRAYING(ch, slot, classArray(class))].name,
                     spellLevel, spellLevel == 1 ? "st" : spellLevel == 2 ?
                     "nd" : spellLevel == 3 ? "rd" : "th",
                     PRAYTIME(ch, slot, classArray(class)));
      }
    }
  }

  send_to_char(ch, "\tn");
}


// display how many available slots you have left for spells
void display_slots(struct char_data *ch, int class)
{
  int slot, memSlot, empty[10], last = 0;
  bool printed, spells = FALSE;

  /*** How many more spells can we mem?  ***/
  printed = FALSE;
  memSlot = 0;

  //fill our empty[] with # available slots
  for (slot = 0; slot < getCircle(ch, class); slot++) {
    spells = FALSE;

    if ((empty[slot] = comp_slots(ch, slot + 1, class) -
		numSpells(ch, slot + 1, class)) > 0)
      spells = TRUE;

    if (spells) {
      last = slot;	// how do we punctuate the end
      memSlot++;	// keep track # circles we need to print
    }
  }

  // display info
  switch (class) {
    case CLASS_DRUID:
      send_to_char(ch, "\r\nYou can commune");
      break;
    case CLASS_CLERIC:
      send_to_char(ch, "\r\nYou can pray");
      break;
    case CLASS_RANGER:
      send_to_char(ch, "\r\nYou can adjure");
      break;
    case CLASS_PALADIN:
      send_to_char(ch, "\r\nYou can chant");
      break;
    default:  /* wizard */
      send_to_char(ch, "\r\nYou can memorize");
      break;
  }
  for (slot = 0; slot < getCircle(ch, class); slot++) {
    if (empty[slot] > 0) {
      printed = TRUE;
      send_to_char(ch, " %d %d%s", empty[slot], (slot + 1),
		(slot + 1) == 1 ? "st" : (slot + 1) == 2 ? "nd" : (slot + 1) == 3 ?
		"rd" : "th");
      if (--memSlot > 1)
        send_to_char(ch, ",");
      else if (memSlot == 1)
        send_to_char(ch, " and");
    }
  }
  if (!printed)
    send_to_char(ch, " no more spells.\r\n");
  else
    send_to_char(ch, " circle spell%s.\r\n",
	empty[last] == 1 ? "" : "s");
}


// entry point: lists ch spells, both memorized, memtimes, slots and memorizing
void printMemory(struct char_data *ch, int class)
{

  //sorc types, just seperated their interface
  if (class == CLASS_SORCERER)
    display_sorc(ch, CLASS_SORCERER);
  else if (class == CLASS_BARD)
    display_sorc(ch, CLASS_BARD);
  else {
    display_memmed(ch, class);
    display_memming(ch, class);
    display_slots(ch, class);
  }
  
  switch (class) {
    case CLASS_SORCERER:
      send_to_char(ch, "\tDCommands: '\tYstudy sorcerer\tD' to adjust known spells.\tn\r\n"
                       "\tDRest, then type '\tYmeditate\tD' to recover spell slots.\tn\r\n");
      break;
    case CLASS_BARD:
      send_to_char(ch, "\tDCommands: '\tYstudy bard\tD' to adjust known spells.\tn\r\n"
                       "\tDRest, then type '\tYcompose\tD' to recover spell slots.\tn\r\n");
      break;
    case CLASS_DRUID:
      send_to_char(ch, "\tDCommands: commune <spellname>, uncommune <spellname>, "
                     "spells druid\tn\r\n");
      break;
    case CLASS_CLERIC:
      send_to_char(ch, "\tDCommands: prayer <spellname>, blank <spellname>, "
                     "spells cleric\tn\r\n");
      break;
    case CLASS_RANGER:
      send_to_char(ch, "\tDCommands: adjure <spellname>, unadjure "
              "<spellname>, spells ranger\tn\r\n");
      break;
    case CLASS_PALADIN:
      send_to_char(ch, "\tDCommands: chant <spellname>, omit "
              "<spellname>, spells paladin\tn\r\n");
      break;
    case CLASS_WIZARD:
      send_to_char(ch, "\tDCommands: memorize <spellname>, forget <spellname>, "
                     "spells wizard\tn\r\n");
      break;
    default:
      break;
  }
}
/************  end display functions **************/


/*************  command functions *****************/

// forget command for players
ACMD(do_gen_forget)
{
  int spellnum, slot, class = -1;
  char arg[MAX_INPUT_LENGTH];

  if (subcmd == SCMD_BLANK)
    class = CLASS_CLERIC;
  else if (subcmd == SCMD_FORGET)
    class = CLASS_WIZARD;
  else if (subcmd == SCMD_UNADJURE)
    class = CLASS_RANGER;
  else if (subcmd == SCMD_OMIT)
    class = CLASS_PALADIN;
  else if (subcmd == SCMD_UNCOMMUNE)
    class = CLASS_DRUID;
  else {
    send_to_char(ch, "Invalid command!\r\n");
    return;
  }

  skip_spaces(&argument);
  spellnum = find_skill_num(argument);
  one_argument(argument, arg);

  if (!*arg) {
    send_to_char(ch, "What would you like to forget? (or all for everything)\r\n");
    return;
  }
  if (getCircle(ch, class) == -1) {
    send_to_char(ch, "Huh?\r\n");
    return;
  }

  if (!strcmp(arg, "all")) {
    if (PRAYING(ch, 0, classArray(class))) {
      for (slot = 0; slot < (MAX_MEM); slot++) {
        PRAYING(ch, slot, classArray(class)) = 0;
      }
      switch (class) {
        case CLASS_DRUID:
          send_to_char(ch, "You purge everything you were attempting to "
                           "commune for.\r\n");
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "You cancel everything you were attempting to "
                           "pray for.\r\n");
          break;
        case CLASS_RANGER:
          send_to_char(ch, "You purge everything you were attempting to "
                           "adjure for.\r\n");
          break;
        case CLASS_PALADIN:
          send_to_char(ch, "You purge everything you were attempting to "
                           "chant for.\r\n");
          break;
        default:  /* wizard */
          send_to_char(ch, "You forget everything you were attempting to "
                           "memorize.\r\n");
          break;
      }
      PRAYIN(ch, classArray(class)) = FALSE;
      return;
    } else if (PRAYED(ch, 0, classArray(class))) {
      for (slot = 0; slot < (MAX_MEM); slot++) {
        PRAYED(ch, slot, classArray(class)) = 0;
      }
      switch (class) {
        case CLASS_DRUID:
          send_to_char(ch, "You purge everything you had communed for.\r\n");
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "You cancel everything you had prayed for.\r\n");
          break;
        case CLASS_RANGER:
          send_to_char(ch, "You purge everything you had adjured "
                  "for.\r\n");
          break;
        case CLASS_PALADIN:
          send_to_char(ch, "You purge everything you had chanted "
                  "for.\r\n");
          break;
        default:  /* wizard */
          send_to_char(ch, "You forget everything you had memorized.\r\n");
          break;
      }
      PRAYIN(ch, classArray(class)) = FALSE;
      return;
    } else {
      switch (class) {
        case CLASS_DRUID:
          send_to_char(ch, "You do not have anything communed for!\r\n");
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "You do not have anything prayed for!\r\n");
          break;
        case CLASS_RANGER:
          send_to_char(ch, "You do not have anything adjured for!\r\n");
          break;
        case CLASS_PALADIN:
          send_to_char(ch, "You do not have anything chanted!\r\n");
          break;
        default:  /* wizard */
          send_to_char(ch, "You do not have anything memorizing/memorized!\r\n");
          break;
      }
      return;
    }
  }
  
  if (spellnum < 1 || spellnum > MAX_SPELLS) {
    send_to_char(ch, "You never knew that to begin with.\r\n");
    return;
  }

  // are we memorizing it?
  for (slot = 0; slot < (MAX_MEM); slot++) {
    if (PRAYING(ch, slot, classArray(class)) == spellnum) {
      removeSpellMemming(ch, spellnum, class);
      switch (class) {
        case CLASS_DRUID:
          send_to_char(ch, "You stop communing for %s.\r\n", spell_info[spellnum].name);
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "You stop praying for %s.\r\n", spell_info[spellnum].name);
          break;
        case CLASS_RANGER:
          send_to_char(ch, "You stop adjuring for %s.\r\n", spell_info[spellnum].name);
          break;
        case CLASS_PALADIN:
          send_to_char(ch, "You stop chanting for %s.\r\n", spell_info[spellnum].name);
          break;
        default:  /* wizard */
          send_to_char(ch, "You stop memorizing %s.\r\n", spell_info[spellnum].name);
          break;
      }
      return;
    }
  }

  // is it memmed?
  for (slot = 0; slot < (MAX_MEM); slot++) {
    if (PRAYED(ch, slot, classArray(class)) == spellnum) {
      forgetSpell(ch, spellnum, class);
      switch (class) {
        case CLASS_DRUID:
          send_to_char(ch, "You purge %s from your communion.\r\n",
                       spell_info[spellnum].name);
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "You purge %s from your prayers.\r\n",
                       spell_info[spellnum].name);
          break;
        case CLASS_RANGER:
          send_to_char(ch, "You purge %s from your adjuring session.\r\n",
                       spell_info[spellnum].name);
          break;
        case CLASS_PALADIN:
          send_to_char(ch, "You purge %s from your chant.\r\n",
                       spell_info[spellnum].name);
          break;
        default:  /* wizard */
          send_to_char(ch, "You purge %s from your memory.\r\n",
                       spell_info[spellnum].name);
          break;
      }
      return;
    }
  }

  switch (class) {
    case CLASS_DRUID:
      send_to_char(ch, "You aren't communing for and don't have communed %s!\r\n",
                   spell_info[spellnum].name);
      break;
    case CLASS_CLERIC:
      send_to_char(ch, "You aren't praying for and don't have prayed %s!\r\n",
                   spell_info[spellnum].name);
      break;
    case CLASS_RANGER:
      send_to_char(ch, "You aren't adjuring for and don't have "
              "adjured %s!\r\n", spell_info[spellnum].name);
      break;
    case CLASS_PALADIN:
      send_to_char(ch, "You aren't petiioning for and don't have "
              "chanted %s!\r\n", spell_info[spellnum].name);
      break;
    default:  /* wizard */
      send_to_char(ch, "You aren't memorizing and don't have memorized %s!\r\n",
                   spell_info[spellnum].name);
      break;
  }
}


// memorize command for players
ACMD(do_gen_memorize)
{
  int spellnum, class = -1, num_spells;

  if (subcmd == SCMD_PRAY)
    class = CLASS_CLERIC;
  else if (subcmd == SCMD_MEMORIZE)
    class = CLASS_WIZARD;
  else if (subcmd == SCMD_ADJURE)
    class = CLASS_RANGER;
  else if (subcmd == SCMD_CHANT)
    class = CLASS_PALADIN;
  else if (subcmd == SCMD_COMMUNE)
    class = CLASS_DRUID;
  else if (subcmd == SCMD_MEDITATE)
    class = CLASS_SORCERER;
  else if (subcmd == SCMD_COMPOSE)
    class = CLASS_BARD;
  else {
    send_to_char(ch, "Invalid command!\r\n");
    return;
  }

  if (getCircle(ch, class) == -1) {
    send_to_char(ch, "Try changing professions.\r\n");
    return;
  }

  if (class == CLASS_SORCERER || class == CLASS_BARD || !*argument) {
    printMemory(ch, class);
    if (GET_POS(ch) == POS_RESTING && !FIGHTING(ch)) {
      if (!isOccupied(ch) && PRAYING(ch, 0, classArray(class)) != 0) {        
        switch (class) {
          case CLASS_DRUID:
            send_to_char(ch, "You continue your communion.\r\n");
            act("$n continues $s communion.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          case CLASS_CLERIC:
            send_to_char(ch, "You continue your prayers.\r\n");
            act("$n continues $s prayers.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          case CLASS_RANGER:
            send_to_char(ch, "You continue your adjuration.\r\n");
            act("$n continues $s adjuration.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          case CLASS_PALADIN:
            send_to_char(ch, "You continue your chant.\r\n");
            act("$n continues $s chant.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          case CLASS_SORCERER:
            send_to_char(ch, "You continue your meditation.\r\n");
            act("$n continues $s meditation.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          case CLASS_BARD:
            send_to_char(ch, "You continue your composition.\r\n");
            act("$n continues $s composition.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          default:  /* wizard */
            send_to_char(ch, "You continue your studies.\r\n");
            act("$n continues $s studies.", FALSE, ch, 0, 0, TO_ROOM);
            break;
        }
        PRAYIN(ch, classArray(class)) = TRUE;
        NEW_EVENT(eMEMORIZING, ch, NULL, 1 * PASSES_PER_SEC);
      }      
    }
    return;
  } else {
    skip_spaces(&argument);
    spellnum = find_skill_num(argument);
  }

  if (spellnum < 1 || spellnum > MAX_SPELLS) {
    send_to_char(ch, "Memorize what?\r\n");
    return;
  }

  if (GET_POS(ch) != POS_RESTING) {
    send_to_char(ch, "You are not relaxed enough, you must be resting.\r\n");
    return;
  }
  
  if (CLASS_LEVEL(ch, class) < spell_info[spellnum].min_level[class]) {
    send_to_char(ch, "You have heard of that spell....\r\n");
    return;
  }

  int minLevel = 0, compSlots = 0;
  minLevel = spellCircle(class, spellnum);
  compSlots = comp_slots(ch, minLevel, class);
  num_spells = numSpells(ch, minLevel, class);

  if (compSlots != -1) {
    if ((compSlots - num_spells) > 0) {
      switch (class) {
        case CLASS_DRUID:
          send_to_char(ch, "You start to commune for %s.\r\n", spell_info[spellnum].name);
          break;
        case CLASS_CLERIC:
          send_to_char(ch, "You start to pray for %s.\r\n", spell_info[spellnum].name);
          break;
        case CLASS_RANGER:
          send_to_char(ch, "You start to adjure for %s.\r\n", spell_info[spellnum].name);
          break;
        case CLASS_PALADIN:
          send_to_char(ch, "You start to chant for %s.\r\n", spell_info[spellnum].name);
          break;
        case CLASS_WIZARD:
          //spellbooks
          if (!spellbook_ok(ch, spellnum, class, TRUE)) {
            return;
          }
          send_to_char(ch, "You start to memorize %s.\r\n", spell_info[spellnum].name);
          break;
      }
      addSpellMemming(ch, spellnum, spell_info[spellnum].memtime, class);
      if (!isOccupied(ch)) {
        PRAYIN(ch, classArray(class)) = TRUE;
        NEW_EVENT(eMEMORIZING, ch, NULL, 1 * PASSES_PER_SEC);
        switch (class) {
          case CLASS_DRUID:
            send_to_char(ch, "You continue your communing.\r\n");
            act("$n continues $s communing.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          case CLASS_CLERIC:
            send_to_char(ch, "You continue your prayers.\r\n");
            act("$n continues $s prayers.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          case CLASS_RANGER:
            send_to_char(ch, "You continue your adjuration.\r\n");
            act("$n continues $s adjuration.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          case CLASS_PALADIN:
            send_to_char(ch, "You continue your chant.\r\n");
            act("$n continues $s chant.", FALSE, ch, 0, 0, TO_ROOM);
            break;
          default:  /* wizard */
            send_to_char(ch, "You continue your studies.\r\n");
            act("$n continues $s studies.", FALSE, ch, 0, 0, TO_ROOM);
            break;
        }
      }
      return;
    } else {
      send_to_char(ch, "You can't retain more spells of that level!\r\n");
    }
  } else
    log("ERR:  Reached end of do_gen_memorize.");

}

/***  end command functions ***/
#undef	TERMINATE



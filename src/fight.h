/**
* @file fight.h
* Fighting and violence functions and variables.
* 
* Part of the core tbaMUD source code distribution, which is a derivative
* of, and continuation of, CircleMUD.
*                                                                        
* All rights reserved.  See license for complete information.                                                                
* Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University 
* CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.
*
*/
#ifndef _FIGHT_H_
#define _FIGHT_H_

/* Structures and defines */
/* Attacktypes with grammar */
struct attack_hit_type {
   const char *singular;
   const char *plural;
};

/* Functions available in fight.c */
void idle_weapon_spells(struct char_data *ch);
int compute_damtype_reduction(struct char_data *ch, int dam_type);
int compute_energy_absorb(struct char_data *ch, int dam_type);
void perform_flee(struct char_data *ch);
void appear(struct char_data *ch, bool forced);
void check_killer(struct char_data *ch, struct char_data *vict);
int perform_attacks(struct char_data *ch, int mode);
int compute_armor_class(struct char_data *attacker, struct char_data *ch);
int compute_damage_reduction(struct char_data *ch, int dam_type);
int compute_concealment(struct char_data *ch);
int compute_bab(struct char_data *ch, struct char_data *victim, int attktype);
int compute_damage_bonus(struct char_data *ch, struct char_data *victim,
	int attktype, int mod, int mode);
int damage(struct char_data *ch, struct char_data *victim,
	int dam, int attacktype, int dam_type, int dualwield);
void death_cry(struct char_data *ch);
void die(struct char_data * ch, struct char_data * killer);
void free_messages(void);
int dam_killed_vict(struct char_data *ch, struct char_data *victim);
/*
 * dualwield = is this a dual wield attack?
 */
void hit(struct char_data *ch, struct char_data *victim,
	int type, int dam_type, int penalty, int dualwield);
void load_messages(void);
void perform_violence(void);
void raw_kill(struct char_data * ch, struct char_data * killer);
void raw_kill_old(struct char_data * ch, struct char_data * killer);
void  set_fighting(struct char_data *ch, struct char_data *victim);
int skill_message(int dam, struct char_data *ch, struct char_data *vict,
          int attacktype, int dualwield);
void  stop_fighting(struct char_data *ch);


/* Global variables */
#ifndef __FIGHT_C__
extern struct attack_hit_type attack_hit_text[];
extern struct char_data *combat_list;
#endif /* __FIGHT_C__ */

#endif /* _FIGHT_H_*/

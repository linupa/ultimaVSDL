/* magic.h - Cast and Use: CAST.OVL and the spell helpers of CAST2.OVL. Parameter order follows
 * the Ghidra signatures.
 *
 * Spells are numbered 0-47, six to a circle (circle = n / 6 + 1); a spell costs its circle in
 * magic points and needs that level. spell_flags says where it works (8 outdoors, 4 towns,
 * 2 dungeons, 1 combat) and `spells` holds how many are mixed. Spell results: 1 "Success!",
 * 0 "Failed!", -1 nothing more to say.
 * Timed effects set active_spell to a letter: 'P' protection, 'Q' quickness, 'C' confusion,
 * 'N' negate magic, 'T' time stop; 0x0E the Amulet, 0x1C the Crown, 0x1D the Black Badge.
 */
#ifndef U5_MAGIC_H
#define U5_MAGIC_H

#include <stdint.h>

/* CAST2.OVL */
void spell_effect(int fx);                   /* e1e0: sound and a flash of the view */
int  ask_target_member(void);                /* e27e: "On who: " */
int  ask_spell_direction(void);              /* e4e6: key 1-4 (W E N S) or 0; dir_x / dir_y = the square */
int  heal_member(int member);                /* e5a2: 1-30 hit points; 0 if dead */
void change_wind(int show_fx, int dir);      /* e5ea */
void party_view_effect(void);                /* e64c: the whole view lit for 20 ticks */
int  summon_daemon(int from_scroll);         /* e6a2 */
void print_coordinates(void);                /* e8cc */
int  magic_unlock_door(void);                /* e948 */
int  dispel_field(int show_fx);              /* e99c */
void set_light(uint8_t turns);               /* eaca */
void start_timed_spell(int fx, uint8_t turns, uint8_t letter);   /* ead8 */

/* CAST.OVL */
int  is_spell_immune_kind(int slot);         /* bf80: Shadowlords, Blackthorn, Lord British */
void cast_missile(int missile);              /* bfb2 */
int  cast_field(int field);                  /* bfcc: 0 fire, 1 poison, 2 sleep, 3 energy */
int  spell_an_zu(void);                      /* c094 */
int  spell_an_nox(void);                     /* c12e */
int  spell_mani(void);                       /* c17a */
int  spell_an_ylem(void);                    /* c1b0 */
int  spell_an_sanct(void);                   /* c252 */
void spell_an_xen_corp(void);                /* c3be */
void spell_in_wis(void);                     /* c424 */
int  spell_kal_xen(void);                    /* c430 */
int  spell_in_xen_mani(void);                /* c534 */
int  spell_in_por(void);                     /* c55c */
void spell_wis_quas(void);                   /* c6cc */
int  spell_in_bet_xen(void);                 /* c734 */
int  spell_an_ex_por(void);                  /* c7c6 */
int  spell_vas_mani(void);                   /* c82c */
void spell_in_vas_por_ylem(int caster);      /* c89e */
int  spell_an_xen_ex(void);                  /* c920 */
int  spell_rel_xen_bet(void);                /* c9dc */
int  spell_sanct_lor(void);                  /* ca7e */
int  spell_in_quas_xen(void);                /* caa8 */
void spell_in_quas_corp(void);               /* cc18 */
int  spell_vas_rel_por(void);                /* cc70 */
int  spell_an_tym(void);                     /* cccc */
int  cmd_cast(void);                         /* cd3a: 0 only after a moongate journey (no turn) */
int  use_scroll(int scroll);                 /* d15e */
int  use_potion(int potion);                 /* d2da */
void use_moonstone(int moonstone);           /* d4bc */
void use_shard(unsigned shard);              /* d534 */
int  use_remove_item(unsigned item);         /* d6e4 */
void cmd_use(void);                          /* d712 */
int  plot_spell_pixel(unsigned color, unsigned y, int x);   /* db30 */
int  trace_spell_cone(int dir, int caster, int tile, int *hit_x, int *hit_y);   /* dbb6 */
void cast_area_spell(int tile, int kind, int caster);       /* dee0 */

#endif

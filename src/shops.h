/* shops.h - merchants: SHOPPES.OVL (common code, provisioner, reagents, stables, armoury,
 * healer), SHOPPES2.OVL (tavern, shipwright) and SHOPPES3.OVL (inn). Entered from open_shop
 * (talk.c) with shop_type, shop_town, shopkeeper_name and shop_name set. Parameter order
 * follows the Ghidra signatures.
 *
 * Shop text comes from SHOPPE.DAT (shop_say loads 1500 bytes at an offset). In that text, and in
 * the strings passed to shop_print, bytes >= 0x80 are words from the talk word table and
 * # $ % ^ & * @ are substitutions: keeper, shop name, price, quantity, item, item 2, time of day.
 *
 * Prices go up for low intelligence: price + price * (100 - 3 * INT) / 100. Most shops divide
 * unsigned, so from INT 34 on the negative product wraps into a huge number; ported as is. */
#ifndef U5_SHOPS_H
#define U5_SHOPS_H

#include <stdint.h>

/* SHOPPES */
void shop_print(const char *text);             /* a2b6 */
void shop_say(uint16_t text_offset);           /* a40a: SHOPPE.DAT offset */
void shop_take_gold(void);                     /* a42a: the Shadowlord of Falsehood takes 1-64 gold */
void shop_greeting(void);                      /* a446 */
void shop_farewell(int kind);                  /* a492: 0 nothing bought, 1 bought, else silent */
int  ask_yes_no(void);                         /* a510: 'Y' or 'N' */
int  provisioner_offer(int result, int item, int member);   /* a54a */
int  provisioner_menu(int member);             /* a686 */
void shop_provisioner(int member);             /* a732 */
int  reagent_offer(int result, int reagent, int member);    /* a792 */
int  reagent_menu(int member);                 /* a8f6 */
void shop_reagents(int member);                /* a9ee */
void shop_stables(int member);                 /* aa4e */
int  armoury_offer(int result, int item, int member);       /* ac3c */
int  armoury_buy_menu(int member);             /* adc0 */
int  sell_list_init(void);                     /* aee8: 1 if anything to sell */
uint8_t armoury_sell_list(int *first, int *selected);       /* af10 */
int  armoury_sell_item(int item, int member);  /* b106 */
void armoury_sell(int member);                 /* b1f4 */
void shop_armoury(int member);                 /* b542 */
int  healer_ask_who(void);                     /* b60c */
void healer_effect(void);                      /* b640 */
int  healer_ask_pay(void);                     /* b6fa: 1 = refused or can't pay */
void shop_healer(void);                        /* b788 */

/* SHOPPES2 */
void tavern_price_for_party(int price_each);   /* e1e0 */
void number_word(void);                        /* e24a */
void sir_or_milady(void);                      /* e28c */
int  tavern_serve_food(void);                  /* e2bc */
int  tavern_order_food(void);                  /* e3b2 */
int  tavern_wine(void);                        /* e3d4 */
int  tavern_buy_rations(int member);           /* e560 */
int  tavern_rumours(void);                     /* e6e8 */
void tavern_farewell(void);                    /* e844 */
void shop_tavern(int member);                  /* e84c */
int  can_afford(int price);                    /* e9c2 */
int  shipwright_offer(int member, int ship, int skiff);     /* e9ee */
void shipwright_menu(int member);              /* ea88 */
void shop_shipwright(int member);              /* ec9c */

/* SHOPPES3 */
int  count_left_companions(void);              /* e1e0 */
int  inn_sir_or_milady(uint8_t sex);           /* e20c: 0 if no room */
int  inn_rest(int result, uint8_t sex, int intel);          /* e252 */
int  inn_leave_companion(int result, uint8_t sex, int intel);   /* e48e */
int  prev_inn_guest(int slot);                 /* e674 */
int  next_inn_guest(int slot);                 /* e696 */
int  inn_pick_up_companion(int result, int intel);          /* e6c6 */
void shop_inn(int member);                     /* ea94 */

#endif

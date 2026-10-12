/* town.h - towns, castles, keeps and dwellings: NPC placement, lamps, horses, stairs and movement.
 * Port of parts of TOWN.OVL; load_town_level is in map.cpp. */
#ifndef U5_TOWN_H
#define U5_TOWN_H

#include <stdint.h>
#include "party.h"
#include "strings.h"

/* A settlement and what happens in it: TOWN.OVL (walking about, lamps, horses, the turn) and the
 * shops of SHOPPES.OVL (shops.cpp). `town` is the one Town object.
 *
 * All of it is the object's own. Some of it is part of a saved game: those members are registered
 * with SaveGame (savegame.h), which puts them at their old places in SAVED.GAM. */
class Town {
public:
    static Town &instance();

    uint8_t just_entered = 0;        /* enter_town: skips one magic check in town_main_loop */
    uint8_t tune_progress = 0;       /* notes of the secret harpsichord tune matched so far */

    /* The shop being visited. The names are string ids (Strings::Id values). */
    uint16_t shop_type = 0;          /* 0 armoury ... 7 inn (open_shop) */
    uint16_t shop_town = 0;          /* which town's shop of that type: index into the price tables */
    uint16_t shop_name = 0;          /* # in the shop's text */
    uint16_t shopkeeper_name = 0;    /* $ in the shop's text */
    uint16_t shop_item_name = 0, shop_item_name2 = 0;   /* further $ substitutions */
    uint16_t shop_price = 0;         /* % in the shop's text */
    uint16_t shop_quantity = 0;      /* quantity offered (reagents) */
    uint8_t shop_text_buf[500] = {}; /* shop_print's output */
    uint8_t tavern_index = 0, tavern_drinks = 0;
    uint16_t tavern_farewell_kind = 0, tavern_diners = 0, tavern_diners2 = 0, tavern_food_tile = 0;   /* words in the original */
    uint16_t shipwright_can_buy = 0, shipwright_bought = 0;

    /* Part of the saved game (registered with SaveGame). */
    uint8_t drunk_turns = 0;         /* ds:5957  "Hic!" */
    uint8_t shadowlord_here = 0;     /* ds:5958  index of the Shadowlord in this town, or 0xFF */
    uint8_t stored_ship = 0;         /* ds:6605  the ship left outside: 0x40 present, 0x80 type, low 6 bits skiffs */
    uint8_t stored_ship_x = 0, stored_ship_y = 0;   /* ds:5953 */
    uint8_t lamp_x[32] = {}, lamp_y[32] = {}, lamp_tile[32] = {};   /* ds:58EE  the street lamps of this level */
    uint8_t lamp_count = 0;          /* ds:594E */

    /* The harpsichord tune that opens the passage in Lord British's castle (ds:275A). */
    static uint8_t secret_tune[13];

    /* The read-only tables of the settlements and shops, by their offsets in the original data
     * segment (see town.cpp). table_word gives the unsigned little-endian word. */
    enum Table {
        SHOP_NAME = 0x21CA,   /* by shop type * 0x20 + town * 2: the shop's name (string id) */
        SHOPKEEPER_NAME = 0x22CA,   /* and its keeper's */
        SHOP_LOCATION = 0x23CA,   /* by shop type * 0x10 + n: the location of the type's n-th shop */
        DRUNK_KEY = 0x2742,   /* the direction a drunk staggers instead */
        NOTE_PITCH = 0x2746,   /* the harpsichord's notes (words) */
        SHADOWLORD_AIR = 0x27DC,   /* "An air of ...": string id by Shadowlord */
        NPC_HIDDEN_BITS = 0x28C2,   /* by location, a bit per NPC (two words) */
        REAGENT_PRICE = 0x3A32,   /* reagents: price by town and reagent; 0 = not sold */
        REAGENT_QUANTITY = 0x3A5A,   /* and how many that buys */
        ARMOURY_STOCK = 0x3AE2,   /* armoury: the items a town's armoury sells */
        SHOP_GREETING = 0x3B2A,   /* by shop type, four each: greetings in SHOPPE.DAT */
        SHOP_FAREWELL_A = 0x3B6A,   /* farewells */
        SHOP_FAREWELL_B = 0x3BAA,
        PROVISION_PRICE = 0x3BEA,   /* provisioner: price by town and item (words) */
        PROVISION_TEXT = 0x3C0A,   /* and the pitch for each item */
        REAGENT_TEXT = 0x3C10,   /* reagents: the pitch for each, in the text reagent_menu loads */
        REAGENT_NAME = 0x3C20,   /* and the menu's names */
        HORSE_PRICE = 0x3C30,   /* stables: price by town */
        HORSE_DX = 0x3C38,   /* and where the horse is put, by side */
        HORSE_DY = 0x3C40,
        ARMOURY_ITEM_TEXT = 0x3C48,   /* armoury: the pitch for each item */
        ARMOURY_OFFER_A = 0x3CA6,   /* phrases, four each */
        ARMOURY_OFFER_B = 0x3CAE,
        ARMOURY_MENU_PHRASE = 0x3CB6,
        ARMOURY_SELL_TEXT = 0x3CBE,   /* buying from the party: eight phrases in the text armoury_sell loads */
        ARMOURY_SELL_A = 0x3D2E,
        ARMOURY_SELL_C = 0x3D36,
        ARMOURY_SELL_B = 0x3D3E,
        ARMOURY_ASK_A = 0x3D46,
        ARMOURY_ASK_B = 0x3D4A,
        ARMOURY_ASK_C = 0x3D52,
        HEAL_PRICE = 0x3D86,   /* healer: prices by town */
        CURE_PRICE = 0x3D8E,
        RESURRECT_PRICE = 0x3D96,   /* (words) */
        TAVERN_FOOD_KEY = 0x4C1E,   /* tavern, by its index: the menu letters of food, wine, rations and rumours */
        TAVERN_WINE_KEY = 0x4C24,
        TAVERN_RATIONS_KEY = 0x4C2A,
        TAVERN_RUMOUR_KEY = 0x4C30,
        MEAL_PRICE = 0x4C36,   /* prices (words) */
        WINE_PRICE = 0x4C48,   /* by wine */
        RATIONS_PRICE = 0x4C54,
        RATIONS_TEXT = 0x4C66,
        RUMOUR_KEYWORD = 0x4C74,   /* rumours: the words the keeper knows (string ids) */
        RUMOUR_SUBJECT = 0x4CA8,   /* what each is about */
        RUMOUR_PLACE = 0x4CDC,   /* which place it points to */
        RUMOUR_PLACE_NAME = 0x4CF6,   /* and that place's name */
        RUMOUR_PRICE = 0x4D10,
        RUMOUR_TEXT = 0x4D44,
        TAVERN_INDEX = 0x4D4C,   /* which tavern a town's is */
        TAVERN_GREETING = 0x4D56,
        TAVERN_MENU = 0x4D5E,
        SHIP_PRICE = 0x4D66,   /* shipwright: prices by town (words) */
        SKIFF_PRICE = 0x4D6E,
        SHIP_X = 0x4D76,   /* and where the ship is launched */
        SHIP_Y = 0x4D7A,
        INN_PRICE = 0x4D7E,   /* inn: price by town */
        INN_ROOMS = 0x4DC4,   /* how many companions it can keep */
        INN_TEXT = 0x4E6E,
        INN_BED_X = 0x4E7A,   /* and where the party wakes */
        INN_BED_Y = 0x4E80,
    };
    static uint8_t table_byte(unsigned ds_offset);
    static uint16_t table_word(unsigned ds_offset);

    /* ---- the settlement (town.cpp) ---- */

    int  npc_is_dead(int npc);                   /* 81d0 */
    void mark_npc_dead(int npc);                 /* 8222: also used when an NPC joins the party */
    void npc_make_hostile(int npc);              /* 8a2e */
    void npc_make_fleeing(int npc);              /* 8aa4 */
    void alert_town(void);                       /* 8b28: talk op 0x8B, attacking */
    void npc_begone(int npc);                    /* 92aa: "Begone, vermin!" */
    void remove_npc(int npc);                    /* 8280 */
    int  npc_for_object(int object);             /* 82ee: NPC slot of a map object, -1 if none */
    void update_lamps(void);                     /* 8340: at hours 5 and 20 */
    void take_stairs(int tile, unsigned facing); /* 86fe */
    void set_vehicle_facing(int dir);            /* 874c */
    int  town_move(int dir);                     /* 87d0: dir 1-4 (W E N S); 1 if the party left the town */
    int  is_hitching_tile(unsigned y, unsigned x);   /* 8e1a */
    void wander_horses(void);                    /* 8e48 */
    void enter_town(int fresh);                  /* 93c0 */
    void npc_encounter(int mode);                /* 9522 */
    void shadowlord_blight(void);                /* 83e2 */
    void check_shadowlord_arrival(void);         /* 847e */
    void fight_npc(int npc);                     /* 8b8c */
    int  cmd_attack_town(void);                  /* 8bb6 */
    int  cmd_klimb_town(void);                   /* 8d52 */
    unsigned town_read_key(int redraw);          /* 8f94 */
    int  town_number_key(int key);               /* 9004 */
    void town_tile_effects(void);                /* 90d2 */
    int  npc_swayed_by_shadowlord(int npc);      /* 92c2 */
    void apply_shadowlord_influence(void);       /* 9326 */
    void print_shadowlord_air(int shadowlord);   /* 9388 */
    int  arrest_party(void);                     /* 947e: 1 = the party resists */
    void town_main_loop(void);                   /* 95ee */
    void place_npcs(void);                       /* 9864 */
    void place_npc(unsigned z, unsigned y, unsigned x, int npc);   /* 98f6 */

    /* The end of a turn in town_main_loop (9764 on): clock, lamps, door, horses, NPCs, encounters.
     * key = the command key (Space lets a carpet keep pace), result = the command's result (1 normal,
     * 2 = NPC encounter forced, 0 = no turn). */
    void town_end_turn(int key, unsigned result);

    /* ---- the shops (shops.cpp; see shops.h) ---- */

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

    Town(const Town &) = delete;
    Town &operator=(const Town &) = delete;

private:
    Town();

    int location_bit(uint16_t base, int npc);
    int intel_of(int member);
    uint16_t adjust_price_u(uint16_t price, int intel);
    uint16_t adjust_price_s(uint16_t price, int intel);
    void draw_panel_box(int inner_rows);
    void restore_mp(PartyMember *p);
};

extern Town &town;

#endif

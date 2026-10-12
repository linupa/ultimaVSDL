/* party.h - the party: its members and what it carries. `party` is the one Party object.
 *
 * All of it is part of the saved game. Party holds it and registers each piece with SaveGame
 * (savegame.h), which puts it at its old place in SAVED.GAM; the ds offsets are given below.
 *
 * The roster holds all sixteen characters; the first size() of them are the party, the rest are
 * waiting somewhere (PartyMember::where) or dead. */
#ifndef U5_PARTY_H
#define U5_PARTY_H


/* A character of the roster: 32 bytes, as in SAVED.GAM. */
#pragma pack(push, 1)
struct PartyMember {
    char     name[9];
    uint8_t  sex;          /* 0x0B male, 0x0C female */
    char     cls;          /* 'A'vatar 'F'ighter 'B'ard 'M'age */
    char     status;       /* 'G'ood 'P'oisoned 'S'leeping 'D'ead */
    uint8_t  str, dex, intel, mp;
    uint16_t hp, max_hp, xp;
    uint8_t  level;
    uint8_t  inn_months;   /* months left at an inn */
    uint8_t  armour;       /* defence value used in combat (?) */
    uint8_t  helm, body, left_hand, right_hand, ring, amulet;  /* item ids, 0xFF = none */
    uint8_t  where;        /* 0 in party, else location left at (inn); 0x7F dead */

    bool good() const { return status == 'G'; }
    bool poisoned() const { return status == 'P'; }
    bool asleep() const { return status == 'S'; }
    bool dead() const { return status == 'D'; }
};
#pragma pack(pop)
static_assert(sizeof(PartyMember) == 32, "PartyMember size");

class Party {
public:
    enum { MAX_SIZE = 6, ROSTER_SIZE = 16 };

    static Party &instance();

    /* ---- the members ---- */

    PartyMember members[ROSTER_SIZE] = {};   /* ds:55A8  the roster */
    uint8_t active_player = 0;               /* ds:587B  0xFF = none ("Set active plr: None") */
    uint8_t karma = 0;                       /* ds:5888  the Avatar's karma, 0-99 */

    /* ---- spells in effect ---- */

    /* The letter of the lasting effect: 'T' time stop, 'Q' quickness, 'N' negate magic, 'C' ...;
     * 0 none. It wears off when active_spell_turns runs out. */
    uint8_t active_spell = 0;        /* ds:587A */
    uint8_t active_spell_turns = 0;  /* ds:588E */
    uint8_t light_spell = 0;         /* ds:58A6  turns of magic light left */
    uint8_t torch_light = 0;         /* ds:58A7  torch burning time left: 0xF0 when lit outside a dungeon,
                                      * more in one; 100 for the "Borrowed!" torch */

    /* ---- how the party travels ---- */

    /* What the party is: its tile on the map. 0x1C / 0x1D on foot, 0x10-0x13 a horse, 0x14 / 0x15
     * a carpet, 0x20-0x27 a ship (sails furled or set, by heading), 0x28-0x2B a skiff. */
    uint8_t transport = 0;           /* ds:587C */
    uint8_t sail_direction = 0;      /* ds:5955  under sail: the direction key that repeats */
    uint8_t sail_counter = 0;        /* ds:5883  turns sailed that way */
    uint8_t need_prompt = 0;         /* ds:5956  outdoor_read_key starts a new line and prompt first;
                                      * cleared while sailing so the moves don't each get one */

    /* ---- how far the quest has got ---- */

    uint8_t sacred_quest = 0;        /* ds:58CC  a bit per virtue: its quest was taken at the shrine */
    uint8_t codex_answered = 0;      /* ds:58CE  a bit per virtue answered at the Codex; all eight: the Codex is read */
    uint8_t summoned_shadowlord = 0; /* ds:58CB  the Shadowlord last called up by name */

    uint8_t size() const { return size_; }
    bool full() const { return size_ == MAX_SIZE; }
    bool alone() const { return size_ == 1; }
    unsigned living() const;   /* members that aren't dead */
    /* The first member who can act (good or poisoned): the one things happen to when nobody is
     * the active player. 0, the Avatar, if nobody can. */
    unsigned first_able() const;

    /* Roster entry i. The original indexes the table with whatever it has, at times with 0xFF
     * for "nobody"; an index past the roster gives a spare entry here, so nothing else is hit. */
    PartyMember &operator[](unsigned i) { return i < ROSTER_SIZE ? members[i] : nobody_; }
    PartyMember *begin() { return members; }
    PartyMember *end() { return members + size_; }

    /* Roster entry i joins as the last member. The entry that was in that slot takes its place
     * in the roster (talk_op_join_party). */
    PartyMember &swap_in(unsigned i);
    /* Roster entry i joins as the last member. The entries between move up by one, keeping
     * their order (inn_pick_up_companion). */
    PartyMember &move_in(unsigned i);
    /* Member i leaves for the end of the roster and the rest close up. The caller says where
     * the member now is (PartyMember::where). */
    PartyMember &move_out(unsigned i);

    /* ---- what the party carries ---- */

    uint16_t food = 0;               /* ds:57A8  "F:" on the status panel */
    uint16_t gold = 0;               /* ds:57AA */
    uint8_t keys = 0, gems = 0, torches = 0, grapples = 0;   /* ds:57AC-57AF */
    uint8_t magic_carpets = 0;       /* ds:57B0 */
    uint8_t odd_keys = 0;            /* ds:57B1  the skull keys */

    /* Counts by item id (Items), and the spells mixed (Spells). Giving item 0x30-0x3F in a
     * conversation adds a mixed spell: in the original the two tables run on. */
    uint8_t inventory[48] = {};      /* ds:57C0 */
    uint8_t mixed_spells[48] = {};   /* ds:57F0 */
    uint8_t scrolls[8] = {};         /* ds:5820 */
    uint8_t potions[8] = {};         /* ds:5828 */
    uint8_t reagents[8] = {};        /* ds:5850 */

    /* One of a kind: 0 or 0xFF. */
    uint8_t lb_amulet = 0, lb_crown = 0, lb_sceptre = 0;   /* ds:57B3  Lord British's */
    uint8_t shards[3] = {};          /* ds:57B6  Falsehood, Hatred, Cowardice */
    uint8_t spyglass = 0;            /* ds:57BA */
    uint8_t hms_cape_plans = 0;      /* ds:57BB */
    uint8_t sextant = 0;             /* ds:57BC */
    uint8_t pocket_watch = 0;        /* ds:57BD */
    uint8_t black_badge = 0;         /* ds:57BE */
    uint8_t sandalwood_box = 0;      /* ds:57BF */

    /* Read-only tables, from DATA.OVL. A class is its place in "AMBFDTPRS" (9 = none of them). */
    uint8_t class_tile(unsigned cls) const { return class_tiles_[cls < 10 ? cls : 9]; }   /* ds:1ADE */
    /* Where KARMA.DAT's saying for karma / 20 starts (0-5). */
    uint16_t karma_text(unsigned k) const { return karma_texts_[k < 6 ? k : 5]; }       /* ds:1A74 */

    Party(const Party &) = delete;
    Party &operator=(const Party &) = delete;

private:
    Party();

    uint8_t size_ = 0;               /* ds:585B  members in the party */
    uint8_t class_tiles_[10] = {};
    uint16_t karma_texts_[6] = {};
    PartyMember nobody_ = {};
};

/* The one Party object (Party::instance()). */
extern Party &party;

#endif

/* party.cpp - the party: its members and what it carries. See party.h. */
#include <string.h>
#include "gamedata.h"
#include "party.h"
#include "savegame.h"

Party::Party()
{
    SaveGame &save = SaveGame::instance();   /* not `savegame`: this runs before main, in no set order */
    save.add(0x55A8, members, "party.members");
    save.add(0x57A8, food, "party.food");
    save.add(0x57AA, gold, "party.gold");
    save.add(0x57AC, keys, "party.keys");
    save.add(0x57AD, gems, "party.gems");
    save.add(0x57AE, torches, "party.torches");
    save.add(0x57AF, grapples, "party.grapples");
    save.add(0x57B0, magic_carpets, "party.magic_carpets");
    save.add(0x57B1, odd_keys, "party.odd_keys");
    save.add(0x57B3, lb_amulet, "party.lb_amulet");
    save.add(0x57B4, lb_crown, "party.lb_crown");
    save.add(0x57B5, lb_sceptre, "party.lb_sceptre");
    save.add(0x57B6, shards, "party.shards");
    save.add(0x57BA, spyglass, "party.spyglass");
    save.add(0x57BB, hms_cape_plans, "party.hms_cape_plans");
    save.add(0x57BC, sextant, "party.sextant");
    save.add(0x57BD, pocket_watch, "party.pocket_watch");
    save.add(0x57BE, black_badge, "party.black_badge");
    save.add(0x57BF, sandalwood_box, "party.sandalwood_box");
    save.add(0x57C0, inventory, "party.inventory");
    save.add(0x57F0, mixed_spells, "party.mixed_spells");
    save.add(0x5820, scrolls, "party.scrolls");
    save.add(0x5828, potions, "party.potions");
    save.add(0x5850, reagents, "party.reagents");
    save.add(0x585B, size_, "party.size");
    save.add(0x587A, active_spell, "party.active_spell");
    save.add(0x587B, active_player, "party.active_player");
    save.add(0x587C, transport, "party.transport");
    save.add(0x5883, sail_counter, "party.sail_counter");
    save.add(0x5888, karma, "party.karma");
    save.add(0x588E, active_spell_turns, "party.active_spell_turns");
    save.add(0x58A6, light_spell, "party.light_spell");
    save.add(0x58A7, torch_light, "party.torch_light");
    save.add(0x58CB, summoned_shadowlord, "party.summoned_shadowlord");
    save.add(0x58CC, sacred_quest, "party.sacred_quest");
    save.add(0x58CE, codex_answered, "party.codex_answered");
    save.add(0x5955, sail_direction, "party.sail_direction");
    save.add(0x5956, need_prompt, "party.need_prompt");
    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x1A74, karma_texts_, "party.karma_texts");
    data.add(0x1ADE, class_tiles_, "party.class_tiles");
}

Party &Party::instance()
{
    static Party the_party;
    return the_party;
}

Party &party = Party::instance();

unsigned Party::living() const
{
    unsigned n = 0;
    for (unsigned i = 0; i < size_; i++) n += !members[i].dead();
    return n;
}

unsigned Party::first_able() const
{
    for (unsigned i = 0; i < size_; i++)
        if (members[i].good() || members[i].poisoned()) return i;
    return 0;
}

PartyMember &Party::swap_in(unsigned i)
{
    PartyMember tmp = members[i];
    tmp.where = 0;
    members[i] = members[size_];
    members[size_] = tmp;
    return members[size_++];
}

PartyMember &Party::move_in(unsigned i)
{
    PartyMember tmp = members[i];
    if (size_ < i)   /* make room: move the roster between up by one */
        memmove(&members[size_ + 1], &members[size_], (size_t)(i - size_) * sizeof tmp);
    tmp.where = 0;
    members[size_] = tmp;
    return members[size_++];
}

PartyMember &Party::move_out(unsigned i)
{
    PartyMember tmp = members[i];
    if (i < ROSTER_SIZE - 1)
        memmove(&members[i], &members[i + 1], (size_t)(ROSTER_SIZE - 1 - i) * sizeof tmp);
    members[ROSTER_SIZE - 1] = tmp;
    size_--;
    return members[ROSTER_SIZE - 1];
}

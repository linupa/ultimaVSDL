/* town.c - NPC placement, lamps, horses, stairs and movement in towns (TOWN.OVL). */
#include <string.h>
#include "combat.h"
#include "commands.h"
#include "data.h"
#include "endings.h"
#include "game.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "npc.h"
#include "objects.h"
#include "shrine.h"
#include "status.h"
#include "talk.h"
#include "text.h"
#include "town.h"
#include "view.h"
#include "world.h"

/* bit n of a per-location 32-bit table at ds offset base */
static int location_bit(uint16_t base, int npc)
{
    uint32_t bits = (uint32_t)ds_u16((uint16_t)(base + dg.location * 4)) |
                    (uint32_t)ds_u16((uint16_t)(base + dg.location * 4 + 2)) << 16;
    return (bits >> (npc & 31)) & 1;
}

/* Only people (type 0x0E and >= 0x40) can be dead (npc_dead_bits, set by mark_npc_dead). */
int npc_is_dead(int npc)
{
    if (dg.npc_type[npc] == 0x0E || dg.npc_type[npc] > 0x3F)
        return location_bit((uint16_t)offsetof(struct DGroup, npc_dead_bits), npc);
    return 0;
}

/* People only: not guards (0x70-0x73), not monsters (>= 0x80) except 0xB4-0xB7. */
void mark_npc_dead(int npc)
{
    if (npc < 0 || npc >= 0x20) return;
    uint8_t kind = dg.npc_type[npc] & 0xFC;
    if ((kind != 0x70 && kind < 0x80) || kind == 0xB4) dg.npc_dead_bits[dg.location] |= (uint32_t)1 << npc;
}

/* Guards and low types attack in place (6), others chase (7); the change times are cleared, so
 * the schedule stays on entry 0. */
void npc_make_hostile(int npc)
{
    uint8_t a = dg.npc_type[npc] < 0x2F ? 6 : 7;
    memset(dg.npc_schedules[npc].times, 0, 4);
    memset(dg.npc_schedules[npc].activity, a, 3);
}

/* People (0x40-0x73) who have a schedule, or are hostile (0xFE), become "Don't hurt me!" (0xFD)
 * and keep away (activity 3). */
void npc_make_fleeing(int npc)
{
    int has_times = 0;
    for (int i = 0; i < 4; i++) has_times |= dg.npc_schedules[npc].times[i] != 0;
    if (dg.npc_type[npc] < 0x74 && dg.npc_type[npc] > 0x3F && (dg.npc_state[npc].dialogue == 0xFE || has_times)) {
        dg.npc_state[npc].dialogue = 0xFD;
        memset(dg.npc_schedules[npc].activity, 3, 3);
    }
}

/* Guards (0x70), daemons (0xD8) and type 0xFC turn hostile; others flee half the time. */
void alert_town(void)
{
    for (int npc = 0; npc < 0x20; npc++) {
        if (dg.npc_state[npc].state == 0) continue;
        uint8_t t = dg.npc_type[npc];
        if (t == 0xFC || t == 0xD8 || t == 0x70) npc_make_hostile(npc);
        else if (random_between(0xFF, 0) < 0x80) npc_make_fleeing(npc);
    }
}

void npc_begone(int npc)
{
    print_text(ds_str(0x278A));   /* "Begone, vermin!" */
    damage_party();
    npc_make_fleeing(npc);
}

void remove_npc(int npc)
{
    NpcState *s = &dg.npc_state[npc];
    MapObject *o = &dg.objects[s->object];
    s->dialogue = s->z = s->y = s->x = s->state = 0;
    memset(o, 0, sizeof *o);
    memset(dg.npc_schedules[npc].activity, 0, 3);
    dg.npc_type[npc] = 0;
    dg.redraw_flags |= 2;
}

int npc_for_object(int object)
{
    for (int i = 0; i < 32; i++)
        if (dg.npc_state[i].object == object && dg.npc_type[i] != 0 && dg.npc_state[i].state != 0) return i;
    return -1;
}

/* Tiles under 0x87 toggle by XOR 0xDD; the lamps found at load (0x48/0x49) go to tile 3 at hour 20
 * and back at hour 5, unless the party stands on one. */
void update_lamps(void)
{
    for (unsigned x = 0; x < 0x20; x++)
        for (unsigned y = 0; y < 0x20; y++)
            if (*get_map_tile_ptr(y, x) == 0x87) *get_map_tile_ptr(y + 1, x) ^= 0xDD;
    if ((*get_map_tile_ptr(dg.party_y, dg.party_x) & 0xFE) != 0x48)
        for (unsigned i = 0; i < dg.lamp_count; i++)
            *get_map_tile_ptr(dg.lamp_y[i], dg.lamp_x[i]) = dg.hour == 5 ? dg.lamp_tile[i] : 3;
    dg.redraw_flags |= 2;
}

/* Stairs 0xC4-0xC7 face a direction: walking in that facing goes up, the opposite way down. */
void take_stairs(int tile, unsigned facing)
{
    if ((tile & 0xFC) != 0xC4) return;
    world_tick();
    if ((unsigned)(tile - 0xC4) == facing) {
        dg.z_level++;
        print_text(ds_str(0x265A));   /* "Up!" */
    } else if ((unsigned)(tile - 0xC4) == (facing ^ 2)) {
        dg.z_level--;
        print_text(ds_str(0x265F));   /* "Down!" */
    } else {
        return;
    }
    load_town_level(1);
}

/* Horses (0x10), carpets (0x14) and boats turn to face the way they move; dir 0 N 1 E 2 S 3 W. */
void set_vehicle_facing(int dir)
{
    uint8_t kind = dg.transport & 0xFC;
    if (kind == 0x10) {
        print_text(ds_str(0x2666));   /* "Ride " */
        if (dir == 1) dg.transport = 0x12;
        else if (dir == 3) dg.transport = 0x13;
    } else if (kind == 0x14) {
        print_text(ds_str(0x266C));   /* "Fly " */
        if (dir == 1) dg.transport = 0x14;
        else if (dir == 3) dg.transport = 0x15;
    } else if (kind == 0x20 || kind == 0x24 || kind == 0x28) {
        if (kind == 0x28) print_text(ds_str(0x2671));   /* "Row " */
        dg.transport = (uint8_t)(dir + kind);
    }
}

int town_move(int dir)
{
    int dx = 0, dy = 0, at_edge = 0, facing = 0;
    const char *text = NULL;
    if (dg.transport == 0x1C || (dg.transport & 0xFE) == 0x12) step_sound();
    switch (dir) {
    case 1: dx = -1; at_edge = dg.party_x == 0; facing = 3; text = ds_str(0x268A); break;   /* West */
    case 2: dx = 1; at_edge = dg.party_x > 0x1E; facing = 1; text = ds_str(0x2684); break;  /* East */
    case 3: dy = -1; at_edge = dg.party_y == 0; facing = 0; text = ds_str(0x2676); break;   /* North */
    case 4: dy = 1; at_edge = dg.party_y > 0x1E; facing = 2; text = ds_str(0x267D); break;  /* South */
    }
    if (text) {
        set_vehicle_facing(facing);
        print_text(text);
    }
    int free = 1;
    uint8_t tile = dg.view_mask[0xA5 + dx + dy * 0x20];   /* the square next to the party, as seen */
    int obj = find_object_at(dg.z_level, (uint8_t)(dg.party_y + dy), (uint8_t)(dg.party_x + dx));
    if (obj != 0) {
        free = 0;
        if (dg.transport >= 0x20 && dg.transport < 0x30) {   /* boats may push into other boats */
            if (!(dg.transport < 0x28 || obj < 0x24 || obj > 0x27)) free = 1;
        } else if ((obj >= 0x24 && obj <= 0x2B) || obj == 0x1B || (obj & 0xFE) == 0x10 || obj == 0x1E || obj == 0x1F) {
            free = 1;   /* boarding: ships, skiffs, horses, carpets */
        }
    }
    if (free && tile_passable(tile, dg.transport)) {
        if (!at_edge) {
            dg.party_x = (uint8_t)(dg.party_x + dx);
            dg.party_y = (uint8_t)(dg.party_y + dy);
            dg.redraw_flags = 1;
            take_stairs(tile, (unsigned)facing);
            return 0;
        }
        print_text(ds_str(0x2690));   /* "Dost thou wish to leave? " */
        int key;
        do {
            key = get_command_key();
        } while (key != 'Y' && key != 'N' && key != 0x1B);
        if (key == 'Y') {
            print_text(ds_str(0x26AB));   /* "Yes\n\nExit to\n" */
            if (dg.location == 0x19) {
                print_text(ds_str(0x26B9));   /* "Underworld!" */
                dg.z_level = 0xFF;
            } else {
                print_text(ds_str(0x26C6));   /* "Britannia!" */
                dg.z_level = 0;
            }
            unsigned loc = dg.location;
            dg.location = 0;
            dg.party_x = dg.town_exit_x[loc];
            dg.party_y = dg.town_exit_y[loc];
            dg.open_door_tile = 0;
            return at_edge;
        }
        print_text(ds_str(0x26D2));   /* "No" */
    } else {
        print_text(ds_str(0x26D6));   /* "Blocked!" */
        beep(200, 0xA5);
        flush_keyboard();
    }
    return 0;
}

int is_hitching_tile(unsigned y, unsigned x)
{
    uint8_t t = *get_map_tile_ptr(y, x);
    return t == 0xA2 || t == 0x43;
}

/* Horses not next to a hitching post move one square at random half the time. */
void wander_horses(void)
{
    for (int i = 0; i < 0x20; i++) {
        MapObject *o = &dg.objects[i];
        uint8_t tile = o->tile;
        if ((tile & 0xFE) != 0x10 || o->z != dg.z_level || random_between(1, 0) != 0) continue;
        int x = o->x, y = o->y;
        if (is_hitching_tile((unsigned)(y + 1), (unsigned)x) || is_hitching_tile((unsigned)y, (unsigned)(x + 1)) ||
            is_hitching_tile((unsigned)(y - 1), (unsigned)x) || is_hitching_tile((unsigned)y, (unsigned)(x - 1)))
            continue;
        if (random_between(1, 0) == 0) {
            y += random_between(1, 0) * 2 - 1;
        } else {
            int d = random_between(1, 0) * 2 - 1;
            x += d;
            tile = d < 1 ? 0x11 : 0x10;   /* face the way it moves */
        }
        if (x < 0x20 && y < 0x20 && x >= 0 && y >= 0 &&
            tile_passable(*get_map_tile_ptr((unsigned)y, (unsigned)x), 0x10) &&
            object_tile_at(dg.z_level, (unsigned)y, (unsigned)x) == 0) {
            o->tile2 = tile;
            o->tile = tile;
            o->x = (uint8_t)x;
            o->y = (uint8_t)y;
            dg.redraw_flags |= 2;
        }
    }
}

void place_npcs(void)
{
    for (int i = 1; i < 0x20; i++) {
        set_object_entry(i, 0, 0, 0, 0, 0, 0);
        dg.npc_state[i].object = 0;
    }
    for (int i = 1; i < 0x20; i++) {
        if (dg.npc_type[i] == 0) continue;
        uint8_t slot = current_schedule_slot(dg.hour, i);
        NpcSchedule *sc = &dg.npc_schedules[i];
        place_npc(sc->z[slot], sc->y[slot], sc->x[slot], i);
        dg.npc_state[i].state = 1;
        dg.npc_state[i].sched_slot = slot;
        dg.npc_path_pos[i] = 0xFFFF;
    }
}

/* Gives the NPC a map object while it is on the party's floor. The object's hull byte is 0x1E for
 * type 1, else 0xFF if the NPC's bit is set in the per-location table at ds:28C2 (meaning not
 * known yet), else 0. */
void place_npc(unsigned z, unsigned y, unsigned x, int npc)
{
    if (dg.npc_type[npc] == 0) return;
    NpcState *s = &dg.npc_state[npc];
    if (z != dg.z_level && s->object != 0) {
        dg.objects[s->object].tile = 0;
        s->object = 0;
    }
    if (z == dg.z_level && s->object == 0) {
        int slot = alloc_object_slot();
        uint8_t hull = dg.npc_type[npc] == 1 ? 0x1E : location_bit(0x28C2, npc) ? 0xFF : 0;
        set_object_entry(slot, hull, (uint8_t)z, (uint8_t)y, (uint8_t)x, dg.npc_type[npc], dg.npc_type[npc]);
        dg.objects[slot].flags = 0;
        s->object = (uint16_t)slot;
    }
    if (z == dg.z_level && s->object != 0) {
        MapObject *o = &dg.objects[s->object];
        o->x = (uint8_t)x;
        o->y = (uint8_t)y;
        o->z = (uint8_t)z;
    }
    s->x = (uint16_t)x;
    s->y = (uint16_t)y;
    s->z = (uint16_t)z;
    s->state = 1;
}

/* With a Shadowlord in town most of the flowers and crops (tiles 0x2D, 0x2E) are withered (0x2C,
 * 0x2B). The pattern is seeded by the day so it stays the same all day. */
void shadowlord_blight(void)
{
    if (dg.shadowlord_here == 0xFF) return;
    set_random_seed(dg.day);
    for (int i = 0; i < 0x400; i++) {
        if (dg.map_buffer[i] == 0x2D) {
            if (random_between(7, 0) != 0) dg.map_buffer[i] = 0x2C;
        } else if (dg.map_buffer[i] == 0x2E && random_between(7, 0) != 0) {
            dg.map_buffer[i] = 0x2B;
        }
    }
    dg.redraw_flags |= 2;
    set_random_seed((uint16_t)time_seed());
}

/* A Shadowlord in this town (not when the party arrives at row 4, i.e. in a cell) gets the highest
 * free NPC slot: an object 0xFC at column 15, at the row from ds:13A5[location], standing still. */
void check_shadowlord_arrival(void)
{
    dg.shadowlord_here = 0xFF;
    if (dg.party_y != 4)
        for (int i = 0; i < 3; i++)
            if (dg.shadowlord_locations[i] == dg.location) {
                dg.shadowlord_here = (uint8_t)i;
                break;
            }
    if (dg.shadowlord_here == 0xFF) return;
    shadowlord_blight();
    for (int i = 0; i < 0x20; i++)
        if (dg.objects[i].tile == 0xFC) return;
    int obj = alloc_object_slot();
    int npc = 0x1F;
    for (int i = 0x1F; i >= 0; i--)
        if (dg.npc_type[i] == 0) {
            npc = i;
            break;
        }
    MapObject *o = &dg.objects[obj];
    NpcState *st = &dg.npc_state[npc];
    NpcSchedule *sc = &dg.npc_schedules[npc];
    st->state = 1;
    st->object = (uint16_t)obj;
    o->x = 0x0F;
    st->x = 0x0F;
    o->y = ds_u8((uint16_t)(0x13A5 + dg.location));
    st->y = o->y;
    o->skiffs = 0;
    o->z = 0;
    o->hull = 0;
    o->flags = 0;
    st->z = 0;
    o->tile = o->tile2 = 0xFC;
    memset(sc->times, 0, 4);
    for (int i = 0; i < 3; i++) {
        sc->activity[i] = 6;
        sc->x[i] = 0x0F;
        sc->y[i] = o->y;
        sc->z[i] = 0;
    }
    dg.npc_type[npc] = 0xFC;
}

void fight_npc(int npc)
{
    mark_npc_dead(npc);
    combat_with_object(dg.npc_state[npc].object);
    remove_npc(npc);
    load_town_level(0);
    check_shadowlord_arrival();
}

/* Only NPCs with a schedule can be swayed, half the time. The type test reads npc_type[4]
 * whatever the NPC (the loop counter is left in the index register): a bug in the original. */
int npc_swayed_by_shadowlord(int npc)
{
    int swayed = 0;
    for (int i = 0; i < 4; i++)
        if (dg.npc_schedules[npc].times[i] != 0) swayed = 1;
    if (dg.npc_type[4] < 0x40 || dg.npc_type[4] > 0x73) swayed = 0;
    if (random_between(1, 0) != 0) swayed = 0;
    return swayed;
}

/* Hatred (1) turns townspeople hostile (dialogue 0xFE), Cowardice (2) makes them flee (0xFD).
 * Falsehood (0) acts in the shops. */
void apply_shadowlord_influence(void)
{
    if (dg.shadowlord_here != 1 && dg.shadowlord_here != 2) return;
    for (int i = 0; i < 0x20; i++) {
        if (!npc_swayed_by_shadowlord(i)) continue;
        if (dg.shadowlord_here == 1) {
            npc_make_hostile(i);
            dg.npc_state[i].dialogue = 0xFE;
        } else {
            npc_make_fleeing(i);
            dg.npc_state[i].dialogue = 0xFD;
        }
    }
}

void print_shadowlord_air(int shadowlord)
{
    print_text(ds_str(0x27B8));   /* "An air of " */
    print_text(ds_str(ds_u16((uint16_t)(0x27DC + shadowlord * 2))));
    print_text(ds_str(0x27C4));   /* " doth surround thee..." */
    sound_sweep(1, 2000, -0x15A0, 1, 0x19C8);
}

void enter_town(int fresh)
{
    dg.redraw_flags = 1;
    dg.town_just_entered = 1;
    if (fresh) {
        for (int i = 1; i < 0x20; i++) dg.objects[i].tile = 0;
        dg.drunk_turns = 0;
        dg.world_tick_enabled = 1;
        load_town_npcs();
        init_npc_states(dg.hour);
    }
    dg.shadowlord_here = 0xFF;
    load_town_level(fresh);
    check_shadowlord_arrival();
    for (int i = 0; i < 0x20; i++)
        if (npc_is_dead(i)) remove_npc(i);
    if (dg.location == 0x1D && dg.lb_sceptre != 0) remove_npc(9);
    draw_status_bar();
    if (check_party_state() < 0) return;
    world_tick();
    if (dg.location == 0x1D) {   /* Stonegate: every Shadowlord still about */
        for (int i = 2; i >= 0; i--)
            if (dg.shadowlord_locations[i] < 0x80) print_shadowlord_air(i);
    } else if (dg.shadowlord_here != 0xFF) {
        print_shadowlord_air(dg.shadowlord_here);
        apply_shadowlord_influence();
    }
}

/* In Blackthorn's castle capture means the interrogation. Elsewhere: going quietly puts the
 * party in the gaol at Yew (location 4) at 8:00 without its keys; refusing alerts the town
 * (returns 1: the guard attacks). */
int arrest_party(void)
{
    if (dg.location == 0x12) {
        if (check_party_state() < 0) return 0;
        captured_by_blackthorn();
    } else {
        print_text(ds_str(0x27E2));   /* "\"Thou art under arrest!\"" */
        print_text(ds_str(0x27FE));   /* "\"Wilt thou come quietly?\"" */
        char c;
        do {
            c = (char)get_command_key();
            if (game_quit()) return 0;
        } while (c != 'N' && c != 'Y');
        if (c != 'Y') {
            print_text(ds_str(0x285E));   /* "No! Then defend thyself, rogue!" */
            alert_town();
            return 1;
        }
        print_text(ds_str(0x281B));   /* "Yes The guard strikes thee unconscious!" */
        set_draw_color(0);
        print_text(ds_str(0x2845));   /* "Thou dost awaken to..." */
        dg.location = 4;
        dg.party_x = 0x19;
        dg.party_y = 4;
        dg.redraw_flags = 1;
        while (dg.hour != 8) pass_time(0x14);
        dg.keys = 0;
        dg.z_level = 0;
    }
    enter_town(1);
    return 0;
}

/* An NPC reached the party (npc_event_slot), or a conversation ended badly (mode 1). An attacker
 * ('a') who was turned by the Shadowlord of Hatred only shoves; a guard (tile 0x70) arrests;
 * anyone else fights. Otherwise the NPC talks first (mode 0) and a bad end means arrest. */
void npc_encounter(int mode)
{
    int fight = 0, npc = dg.npc_event_slot;
    int obj = dg.npc_state[npc].object;
    world_tick();
    if (dg.npc_event == 'a') {
        if (dg.npc_state[npc].dialogue == 0xFE) {
            put_char('\n');
            npc_begone(npc);
        } else if (dg.objects[obj].tile != 0x70) {
            fight = 1;
        } else {
            fight = arrest_party();
        }
    } else if (mode == 0) {
        if (dg.npc_state[npc].dialogue != 0 && talk_to_npc(npc) != 0) fight = arrest_party();
    } else {
        fight = arrest_party();
    }
    if (fight) {
        npc = dg.npc_event_slot;
        if (dg.objects[dg.npc_state[npc].object].tile < 0x40) {
            remove_npc(npc);
        } else {
            print_text(ds_str(0x2881));   /* "Attacked!" */
            fight_npc(npc);
        }
    }
}

int cmd_attack_town(void)
{
    print_text(ds_str(0x26E0));   /* "Attack-" */
    if (*get_map_tile_ptr(dg.party_y, dg.party_x) < 4 && dg.transport != 0x1C) {
        print_text(ds_str(0x26E8));   /* "On foot!\n" */
        return 0;
    }
    if (ask_direction() == 0) return 1;
    unsigned x = (unsigned)(dg.party_x + dg.dir_x) & 0xFFFF, y = (unsigned)(dg.party_y + dg.dir_y) & 0xFFFF;
    if (*get_map_tile_ptr(y, x) == 0x9D) {   /* a mirror */
        *get_map_tile_ptr(y, x) = 0x9F;
        print_text(ds_str(0x26F2));   /* "Broken!\n" */
        for (int f = 2000; f < 20000; f += 1000) sound_noise(f, 0x78, 0x28);
        dg.redraw_flags |= 2;
        return 1;
    }
    int tile = find_object_at(dg.z_level, (uint8_t)y, (uint8_t)x);
    int nothing = 1, npc = -1;
    if (tile != 0) {
        npc = npc_for_object((int)dg.dir_x);
        if (tile > 0x3F && (tile < 0xE8 || tile > 0xEF) && (tile & 0xFC) != 0xB4) nothing = 0;
    }
    if (nothing) {
        print_text(ds_str(0x26FB));   /* "Nothing to attack!\n" */
        return 1;
    }
    if (tile < 0x80) {   /* a person: karma -5 and the town turns on the party */
        sub_byte_floor0(5, &dg.karma);
        alert_town();
    } else if ((tile & 0xFC) == 0xD8) {
        alert_town();
    }
    uint8_t ground = *get_map_tile_ptr(y, x);
    if (ground < 0x84 || (ground > 0x85 && ground != 0x9F && ground != 0xAB)) {
        if (npc < 0) return 1;
        mark_npc_dead(npc);
        fight_npc(npc);
        return 1;
    }
    /* someone in the stocks, behind the broken mirror or in bed can't fight back */
    if (tile == 0x78) {
        print_text(ds_str(0x270F));   /* "Missed!\n" */
        return 1;
    }
    print_text(ds_str(0x2718));   /* "Murdered!\n" */
    sub_byte_floor0(5, &dg.karma);
    flash_square((int)y, (int)x);
    if (npc < 0) return 1;
    mark_npc_dead(npc);
    remove_npc(npc);
    return 1;
}

/* On a ladder (0xC8 up, 0xC9 down) or a grate (0x86, down) the party climbs at once; otherwise
 * a direction is asked and fences and such (0x4C, 0xCA, 0xCB) can be climbed over. */
int cmd_klimb_town(void)
{
    print_text(ds_str(0x2723));   /* "Klimb-" */
    if ((dg.transport & 0xFE) == 0x12) {
        print_text(ds_str(0x272A));   /* "-On foot!\n" */
        return 0;
    }
    uint8_t t = *get_map_tile_ptr(dg.party_y, dg.party_x);
    if (t == 0x86 || t == 0xC9 || t == 0xC8) {
        dg.facing = t == 0xC8 ? 0 : 2;
        take_stairs(0xC4, dg.facing);
        return 1;
    }
    if (ask_direction() != 0) {
        t = *get_map_tile_ptr((unsigned)(dg.party_y + dg.dir_y) & 0xFFFF, (unsigned)(dg.party_x + dg.dir_x) & 0xFFFF);
        if (t != 0x4C && t != 0xCA && t != 0xCB) {
            print_text(ds_str(0x2735));   /* "What?\n" */
            return 0;
        }
        dg.party_x = (uint8_t)(dg.party_x + dg.dir_x);
        dg.party_y = (uint8_t)(dg.party_y + dg.dir_y);
        dg.redraw_flags = 1;
        world_tick();
    }
    return 1;
}

/* While drunk, half the keys are replaced by a random direction ("Hic!") and the town is
 * alerted. */
unsigned town_read_key(int redraw)
{
    if (redraw != 0) {
        world_tick();
        if (dg.status_dirty != 0) {
            draw_status_bar();
            dg.status_dirty = 0;
        }
        put_char('\n');
        draw_label_left();
    }
    unsigned key = (unsigned)get_command_key();
    if (dg.drunk_turns != 0 && random_between(1, 0) != 0) {
        alert_town();
        dg.drunk_turns--;
        print_text(ds_str(0x273C));   /* "Hic!\n" */
        key = ds_u8((uint16_t)(0x2742 + random_between(3, 0)));   /* 3 4 2 1 */
    }
    return key;
}

/* Digits set the active player, except facing a harpsichord (0x8D to the south), where they
 * play notes: the 13 notes of secret_tune (Stones) open the secret panel in Lord British's
 * castle (location 0x11, level 2). Returns 3: no turn passes and the prompt isn't redrawn. */
int town_number_key(int key)
{
    if (dg.view_mask[6 * 0x20 + 5] != 0x8D) return cmd_set_active_player(key);
    unsigned note = (unsigned)(key - '0');
    if (dg.sound_enabled != 0) sound_sweep(-4, 20000, 4000, 1, ds_u16((uint16_t)(0x2746 + note * 2)));
    if (dg.secret_tune[dg.tune_progress] == note) {
        if (++dg.tune_progress == 0x0D) {
            dg.tune_progress = 0;
            if (dg.location == 0x11 && dg.z_level == 2) {
                dg.map_buffer[0x1B1] ^= 0x0B;
                screen_shake();
                dg.redraw_flags = 1;
            }
        }
    } else if (dg.tune_progress == 10 && note == 8) {
        dg.tune_progress = 3;
    } else if (dg.tune_progress == 11 && note == 7) {
        dg.tune_progress = 2;
    } else if (note == dg.secret_tune[0]) {
        dg.tune_progress = 1;
    } else {
        dg.tune_progress = 0;
    }
    return 3;
}

/* Sleepers wake 1 time in 16. A trapdoor (0x8C, not on a carpet) drops the party a level; in
 * Stonegate (0x1D) it drops them into lava and everyone dies. Swamp poisons, fire and lava burn. */
void town_tile_effects(void)
{
    for (unsigned i = 0; i < dg.party_size; i++)
        if (dg.party[i].status == 'S' && random_between(0x0F, 0) == 0x0F) dg.party[i].status = 'G';
    int again;
    do {
        again = 0;
        uint8_t t = *get_map_tile_ptr(dg.party_y, dg.party_x);
        if (t == 0x8C && (dg.transport & 0xFE) != 0x14) {
            print_text(ds_str(0x2768));   /* "A TRAPDOOR!\n" */
            uint8_t transport = dg.transport;
            dg.transport = 0;
            world_tick();
            damage_party();
            dg.transport = transport;
            if (dg.location == 0x1D) {
                set_draw_color(0);
                fill_rect(0xB7, 0xB7, 8, 8);
                for (int f = 1000; f > 0xFA; f--) {   /* a long falling whistle */
                    speaker_on((unsigned)f);
                    delay_loop(0x28, 1);
                    if (game_quit()) break;
                }
                speaker_off();
                memset(dg.map_buffer, 0x8F, 0x400);
                dg.redraw_flags = 1;
                for (int i = 0; i < 0x20; i++) dg.objects[i].tile = 0;   /* the original clears 0x100 bytes, 1 apart */
                for (unsigned i = 0; i < dg.party_size; i++) {
                    dg.party[i].hp = 0;
                    dg.party[i].status = 'D';
                    sound_noise(500, 3000, 0x28);
                    draw_status_bar();
                }
            } else {
                dg.z_level--;
                load_town_level(1);
                again = 1;
            }
        } else if (t == 4 && dg.transport == 0x1C) {
            for (unsigned i = 0; i < dg.party_size; i++) {
                PartyMember *m = &dg.party[i];
                if (m->status != 'D' && m->status != 'P' && m->dex < (unsigned)random_between(0x1D, 0)) {
                    print_text(ds_str(0x2775));   /* "Poisoned!\n" */
                    m->status = 'P';
                    draw_status_bar();
                }
            }
        } else if (t == 0xBC || t == 0x8F) {
            world_tick();
            print_text(ds_str(0x2780));   /* "Burning!\n" */
            damage_party();
        }
    } while (again);
    per_turn_status();
}

/* The end of a town turn: a minute passes, the lamps, the tile under the party, the open door,
 * then the townspeople. On a carpet or horse the town moves only every other turn (unless the
 * party passed); Time Stop freezes it and Quickness halves it. result: 1 a turn, 2 a talk that
 * ended in arrest. */
void town_end_turn(int key, unsigned result)
{
    static int carpet_skip, quick_skip;
    if (dg.location == 0 || result == 0) return;
    if (check_party_state() == -1) return;
    uint8_t hour = dg.hour;
    pass_time(1);
    if (hour != dg.hour && (dg.hour == 0x14 || dg.hour == 5)) update_lamps();
    town_tile_effects();
    if (dg.open_door_tile != 0 && --dg.open_door_timer == 0)
        restore_open_door(dg.open_door_y, dg.open_door_x, dg.open_door_tile);
    dg.objects[0].x = dg.party_x;
    dg.objects[0].y = dg.party_y;
    dg.objects[0].z = dg.z_level;
    int go = dg.transport < 0x12 || dg.transport > 0x15 || key == ' ' || (carpet_skip = !carpet_skip, !carpet_skip);
    go = go && dg.active_spell != 0x54 && (dg.active_spell != 0x51 || (quick_skip = !quick_skip, !quick_skip));
    if (go) {
        wander_horses();
        if ((int)result < 2) update_npcs(dg.hour);
        if (dg.npc_event_slot != 0 || result == 2) npc_encounter((int)result - 1);
    }
}

void town_main_loop(void)
{
    for (;;) {
        unsigned result = 1, key = 0;
        int state = check_party_state();
        if (state == 1) {
            put_char('\n');
            draw_label_left();
            print_text(ds_str(0x288D));   /* "Zzzzzz...\n" */
            wait_turns(1);                /* port: lets the frame show (the original just loops) */
        } else if (state == -1) {
            party_death();
            result = 0;
        } else {
            if (dg.town_just_entered == 0) {
                if (check_moongate() != 0) visit_shrine();
            } else {
                dg.town_just_entered = 0;
            }
            if (dg.location == 0) return;
            int redraw = 1;
            do {
                key = town_read_key(redraw) & 0xFF;
                redraw = 0;
                if (game_quit()) return;
                if (key > 0x1F) {
                    result = (key < '0' || key > '9') ? (unsigned)dispatch_command((int)key) : (unsigned)town_number_key((int)key);
                    continue;
                }
                switch (key) {
                case 1: case 2: case 3: case 4:
                    result = town_move((int)key) == 0;
                    break;
                case 5:   /* Ctrl-E */
                    print_text(ds_str(0x2898));   /* "Exit to DOS? " */
                    if ((get_command_key() & 0xFF) == 'Y') {
                        exit_game(0);
                        return;
                    }
                    print_text(ds_str(0x28A6));   /* "N\n" */
                    break;
                case 0x0B:   /* Ctrl-K */
                    print_number(' ', 1, dg.karma);
                    put_char('\n');
                    result = 0;
                    break;
                case 0x13:   /* Ctrl-S */
                    print_text(ds_str(0x28AE));   /* "Sound " */
                    print_text(ds_str(dg.sound_enabled == 0 ? 0x28BA : 0x28B5));   /* "On\n" / "Off\n" */
                    dg.sound_enabled = dg.sound_enabled == 0;
                    result = 0;
                    break;
                case 0x16:   /* Ctrl-V */
                    print_text(ds_str(0x28A9));   /* "1.16" */
                    put_char('\n');
                    result = 0;
                    break;
                default:
                    print_text(ds_str(0x28BE));   /* "What?\n" */
                    result = 0;
                    break;
                }
            } while (result == 3);
        }
        if (game_quit()) return;
        if (dg.location == 0) return;
        town_end_turn((int)key, result);
        if (dg.location == 0 || game_quit()) return;
    }
}

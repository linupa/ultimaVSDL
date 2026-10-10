/* npc.c - town NPC schedules, movement and path finding (NPC.OVL). See npc.h. */
#include <string.h>
#include "data.h"
#include "gfx.h"
#include "map.h"
#include "npc.h"
#include "objects.h"
#include "town.h"

/* The path grid is 32x32 bytes at the address in path_grid (text_buffer, set at startup). Per
 * square: high nibble = direction it was reached from (1 W, 2 S, 3 E, 4 N as searched), low
 * nibble 5 = goal, 6 = start; 0x90 = blocked. The search's bounds check lets x or y reach 32, so it
 * can touch the bytes just past the grid, as in the original. */
#define GRID(y, x) (*ds_at((long)dg.path_grid + (long)(y) * 0x20 + (x)))

static int sx(int npc) { return (int16_t)dg.npc_state[npc].x; }
static int sy(int npc) { return (int16_t)dg.npc_state[npc].y; }

void load_town_npcs(void)
{
    static const uint16_t files[4] = {0x6D46, 0x6D50, 0x6D5E, 0x6D6A};   /* towne/dwelling/castle/keep.npc */
    uint8_t dialogue[32];
    unsigned group = (unsigned)(dg.location - 1) >> 3;
    unsigned offset = ((unsigned)(dg.location - 1) - group * 8) * 0x240;
    const char *file = ds_str(files[group & 3]);
    load_file_part(offset, 0x200, (uint8_t *)dg.npc_schedules, file);
    load_file_part(offset + 0x200, 0x20, dg.npc_type, file);
    load_file_part(offset + 0x220, 0x20, dialogue, file);
    for (int i = 0; i < 32; i++) dg.npc_state[i].dialogue = dialogue[i];
}

void init_npc_states(unsigned hour)
{
    for (int npc = 1; npc < 32; npc++) {
        NpcState *s = &dg.npc_state[npc];
        if (dg.npc_type[npc] != 0) {
            uint8_t slot = current_schedule_slot((uint8_t)hour, npc);
            NpcSchedule *sc = &dg.npc_schedules[npc];
            s->state = 1;
            s->x = sc->x[slot];
            s->y = sc->y[slot];
            s->z = sc->z[slot];
            s->type = dg.npc_type[npc];
            s->sched_slot = slot;
            s->object = 0;
            dg.npc_path_pos[npc] = 0xFFFF;
            dg.npc_paths[npc * 0x20] = 0;
            dg.npc_blocked_turns[npc] = 0;
        } else {
            s->state = 0;
        }
    }
}

/* Mode -1: to the nearest up stairs (0xC8), -2: down stairs (0xC9). */
int npc_path_to_stairs(int npc, int up, unsigned y, unsigned x)
{
    return npc_find_path(npc, up ? -1 : -2, 0, 0, (int)y, (int)x);
}

void build_path_grid(int npc, int mode, int dest_y, int dest_x, int start_y, int start_x)
{
    uint8_t stairs = mode == -1 ? 0xC8 : 0xC9;
    int slot = current_schedule_slot(dg.hour, npc);
    for (int y = 0; y < 0x20; y++)
        for (int x = 0; x < 0x20; x++) {
            GRID(y, x) = npc_square_walkable(slot, npc, dg.z_level, (unsigned)y, (unsigned)x) == 0 ? 0x90 : 0;
            if (mode < 0 && dg.map_buffer[y * 0x20 + x] == stairs) GRID(y, x) = 5;
        }
    /* creatures (and the party) within 4 squares block */
    for (int i = 31; i > 0; i--) {
        MapObject *o = &dg.objects[i];
        if (o->tile > 0x3F && manhattan_distance(sy(npc), sx(npc), o->y, o->x) < 4) GRID(o->y, o->x) = 0x90;
    }
    if (manhattan_distance(sy(npc), sx(npc), dg.objects[0].y, dg.objects[0].x) < 4)
        GRID(dg.objects[0].y, dg.objects[0].x) = 0x90;
    if (mode >= 0) GRID(dest_y, dest_x) = 5;
    GRID(start_y, start_x) = 0x46;
}

/* Breadth-first search from the start over a 32-entry circular queue. Each square reached is
 * marked with the direction it was reached in; the first goal square found goes to dir_x/dir_y.
 * Returns 1 if a goal was reached. */
int npc_find_path(int npc, int mode, int dest_y, int dest_x, int start_y, int start_x)
{
    uint8_t qx[32], qy[32];
    int found = 0, head = 0, tail = 1;
    build_path_grid(npc, mode, dest_y, dest_x, start_y, start_x);
    qx[0] = (uint8_t)start_x;
    qy[0] = (uint8_t)start_y;
    for (;;) {
        int x = qx[head], y = qy[head];
        int dir = GRID(y, x) >> 4;
        for (int n = 0; n < 4; n++) {
            int out = 0;
            switch (dir) {
            case 1: if (--x < 0) out = 1; break;
            case 2: if (++y > 0x20) out = 1; break;
            case 3: if (++x > 0x20) out = 1; break;
            case 4: if (--y < 0) out = 1; break;
            }
            if (!out && GRID(y, x) < 0x10) {
                uint8_t old = GRID(y, x) & 0x0F;
                GRID(y, x) = (uint8_t)(dir << 4);
                if (old == 5) {
                    dg.dir_x = (uint16_t)x;
                    dg.dir_y = (uint16_t)y;
                    found = 1;
                    break;
                }
                if (tail != head) {   /* a full queue drops the square */
                    qx[tail] = (uint8_t)x;
                    qy[tail] = (uint8_t)y;
                    tail++;
                }
                if (tail >= 0x20) tail = 0;
            }
            dir = (dir & 3) + 1;
            x = qx[head];
            y = qy[head];
        }
        if (found) return 1;
        if (++head == 0x20) head = 0;
        if (head == tail) return 0;
    }
}

/* Walks back from the goal to the start square (low nibble 6), collecting (count, direction) runs,
 * then reverses them into walking order and converts the directions to step_in_dir's numbering.
 * Returns the number of bytes stored. */
int npc_store_path(int dest_y, int dest_x, int mode, int npc)
{
    (void)mode;
    uint8_t *path = &dg.npc_paths[npc * 0x20];
    uint8_t count = 0;
    int n = 0;
    dg.npc_path_pos[npc] = 0;
    uint8_t cell = GRID(dest_y, dest_x), dir = cell >> 4, run_dir = dir;
    do {
        cell &= 0x0F;
        switch (dir) {   /* undo the search step */
        case 1: dest_x++; break;
        case 2: dest_y--; break;
        case 3: dest_x--; break;
        case 4: dest_y++; break;
        }
        if (run_dir == dir && cell != 6) count++;
        if (run_dir != dir || cell == 6) {
            path[n] = count;
            path[n + 1] = run_dir;
            n += 2;
            if (cell == 6) break;
            count = 1;
            run_dir = dir;
        }
        cell = GRID(dest_y, dest_x);
        dir = cell >> 4;
    } while (n < 0x20);
    for (int i = 0, j = n - 2; i <= j; i += 2, j -= 2) {
        uint8_t c = path[i], d = path[i + 1];
        path[i] = path[j];
        path[i + 1] = (uint8_t)(((path[j + 1] + 1) & 3) + 1);
        path[j] = c;
        path[j + 1] = (uint8_t)(((d + 1) & 3) + 1);
    }
    return n;
}

/* The original clamps against the other coordinate (east checks dir_y > 32, ...); kept. */
void step_in_dir(int dir)
{
    switch (dir) {
    case 1: dg.dir_x++; if ((int16_t)dg.dir_y > 0x20) dg.dir_x = 0x20; break;
    case 2: dg.dir_y--; if ((int16_t)dg.dir_x < 0) dg.dir_y = 0; break;
    case 3: dg.dir_x--; if ((int16_t)dg.dir_y < 0) dg.dir_x = 0; break;
    case 4: dg.dir_y++; if ((int16_t)dg.dir_x > 0x20) dg.dir_y = 0x20; break;
    }
}

int manhattan_distance(int y1, int x1, int y2, int x2)
{
    int dx = x2 - x1, dy = y2 - y1;
    return (dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy);
}

/* Activities 3/6 keep away from the party, 4/5/7 approach it. Next to the party, 4/5 ask to talk
 * (if the NPC has a conversation) and 6/7 attack, through npc_event / npc_event_slot. */
void npc_follow_party(int slot, int npc)
{
    NpcState *s = &dg.npc_state[npc];
    unsigned activity = dg.npc_schedules[npc].activity[slot];
    int dist[4];
    int d = manhattan_distance(sy(npc), sx(npc), dg.objects[0].y, dg.objects[0].x);
    if (d == 1 && activity > 3) {
        if (activity == 4 || activity == 5) {
            if (s->dialogue != 0) {
                dg.npc_event = 't';
                dg.npc_event_slot = (uint8_t)npc;
                return;
            }
        } else {
            dg.npc_event = 'a';
            dg.npc_event_slot = (uint8_t)npc;
            return;
        }
    }
    uint8_t py = dg.objects[0].y, px = dg.objects[0].x;
    for (int dir = 1; dir < 5; dir++) {   /* distance to the party after a step each way, 99 = can't */
        dg.dir_x = s->x;
        dg.dir_y = s->y;
        step_in_dir(dir);
        int nx = (int16_t)dg.dir_x, ny = (int16_t)dg.dir_y;
        dist[dir - 1] = npc_can_step(-1, npc, (unsigned)ny, (unsigned)nx) ? manhattan_distance(ny, nx, py, px) : 99;
    }
    d = manhattan_distance(sy(npc), sx(npc), dg.objects[0].y, dg.objects[0].x);
    int best = -1, i;
    for (i = 1; i < 8; i++) {
        if (i < 5 && dist[i - 1] != 99) {
            if (activity == 3) {
                if (dist[i - 1] > d) {
                    if (best == -1 || random_between(1, 0) != 0) best = i;
                    if (i == 4) break;
                }
            } else if (dist[i - 1] < d) {
                best = i;
                break;
            }
        }
        /* Steps 5-7 read past the distance array in the original (its other locals: a pointer,
         * the distance and the activity, none 99) and compare dist[i - 5] with the current
         * distance; step_in_dir(5-7) then doesn't move. Net effect: stay put. */
        if (i > 4 && dist[i - 5] == d) {
            best = i;
            break;
        }
    }
    if ((activity == 5 || activity == 7) && random_between(0x3F, 0) < 0x10) {   /* sometimes stray */
        int alt = best;
        for (int j = 1; j < 5; j++)
            if (j != best && dist[j - 1] != 99 && (alt == best || random_between(0x3F, 0) < 0x10)) alt = j;
        best = alt;
    }
    if (best > -1) {
        dg.dir_x = s->x;
        dg.dir_y = s->y;
        step_in_dir(best);
        MapObject *o = &dg.objects[s->object];
        s->x = dg.dir_x;
        o->x = (uint8_t)dg.dir_x;
        s->y = dg.dir_y;
        o->y = (uint8_t)dg.dir_y;
        dg.redraw_flags |= 2;
    }
}

/* At one of the NPC's change hours, decides how it gets to the new spot: same spot -> 1, same
 * floor -> 2 (walk), else stairs states 4-8. Returns the new slot + 1, or 0 if nothing changed
 * (or it is already there). */
int npc_check_schedule(unsigned hour, int npc)
{
    NpcSchedule *sc = &dg.npc_schedules[npc];
    NpcState *s = &dg.npc_state[npc];
    int slot = -1;
    for (int i = 0; i < 4; i++) {
        if (sc->times[i] != hour) continue;
        slot = current_schedule_slot((uint8_t)hour, npc);
        if ((int)s->sched_slot == slot) {
            s->state = 1;
        } else if (s->z == dg.z_level || sc->z[slot] == dg.z_level) {
            if (s->z == dg.z_level) {
                if (sc->z[slot] == dg.z_level) s->state = 2;
                else if ((int8_t)dg.z_level < (int8_t)sc->z[slot]) s->state = 6;
                else s->state = 7;
            } else if ((int8_t)dg.z_level < (int8_t)s->z) {
                s->state = 4;
            } else {
                s->state = 5;
            }
        } else {
            s->state = 8;
        }
        break;
    }
    if (slot > -1 && s->x == sc->x[slot] && s->y == sc->y[slot] && s->z == sc->z[slot]) {
        slot = 0;
        s->state = 1;
    }
    return slot == -1 ? 0 : slot + 1;
}

int npc_on_stairs(int slot, int npc)
{
    MapObject *o = &dg.objects[dg.npc_state[npc].object];
    uint8_t t = *get_map_tile_ptr(o->y, o->x);
    if ((int8_t)dg.npc_schedules[npc].z[slot] < (int8_t)dg.z_level) {
        if (t == 0xC9) return 1;
    } else if (t == 0xC8) {
        return 1;
    }
    return (t & 0xF4) == 0xC4;
}

/* 2 = the NPC's destination for this slot, 1 = walkable for NPCs (bit clear in ds:367E), 0 = not.
 * Stairs count as walkable while the NPC is heading for them (state 3). */
uint8_t npc_square_walkable(int slot, int npc, unsigned z, unsigned y, unsigned x)
{
    NpcSchedule *sc = &dg.npc_schedules[npc];
    if (slot >= 0 && sc->x[slot] == x && sc->y[slot] == y && sc->z[slot] == z) return 2;
    uint8_t t = dg.map_buffer[0x3FF];
    if ((int)x >= 0 && (int)y >= 0 && (int)x < 0x20 && (int)y < 0x20) t = dg.map_buffer[x + y * 0x20];
    if (dg.npc_state[npc].state == 3 && (t == 0xC8 || t == 0xC9)) return 1;
    return (ds_u8(0x367E + (t >> 3)) & (0x80 >> (t & 7))) == 0;
}

/* Inside the map, walkable for this NPC and no object there. Type 0xFC walks anywhere, and
 * anyone onto 0x30-0x33; chairs (0x90-0x93) only when walking a path (state 2). */
int npc_can_step(int slot, int npc, unsigned y, unsigned x)
{
    if ((int)x < 0 || (int)x > 0x1F || (int)y < 0 || (int)y > 0x1F) return 0;
    uint8_t t = *get_map_tile_ptr(y, x);
    int ok;
    if (dg.npc_type[npc] == 0xFC || (t & 0xFC) == 0x30) ok = 1;
    else if ((t & 0xFC) == 0x90 && dg.npc_state[npc].state != 2) ok = 0;
    else ok = npc_square_walkable(slot, npc, dg.z_level, y, x);
    if (ok == 0 || object_tile_at(dg.z_level, y, x) != 0) return 0;
    return ok;
}

/* Half the time, one random step, staying within radius of its spot (0 = any distance). */
void npc_wander(NpcSchedule *sched, int npc, int slot, int radius, NpcState *state)
{
    if (!(random_between(0xFF, 0) & 8)) return;
    int r = random_upto(0x40);
    dg.dir_x = state->x;
    dg.dir_y = state->y;
    step_in_dir((r & 3) + 1);
    uint16_t nx = dg.dir_x, ny = dg.dir_y;
    if ((radius == 0 || manhattan_distance((int16_t)ny, (int16_t)nx, sched->y[slot], sched->x[slot]) <= radius) &&
        npc_can_step(slot, npc, ny, nx)) {
        MapObject *o = &dg.objects[state->object];
        state->x = nx;
        o->x = (uint8_t)nx;
        state->y = ny;
        o->y = (uint8_t)ny;
        dg.redraw_flags |= 2;
    }
}

/* Activity: 0 stand, 1 wander within 3, 2 wander, 3/6 keep away (6 attacks) when within 3,
 * 4 approach to talk when the party is within 3 of its spot (else wander within 3), 5/7 approach. */
void npc_do_activity(int slot, int npc)
{
    NpcSchedule *sc = &dg.npc_schedules[npc];
    switch (sc->activity[slot]) {
    case 2:
        npc_wander(sc, npc, slot, 0, &dg.npc_state[npc]);
        break;
    case 3: case 6:
        if (manhattan_distance(sy(npc), sx(npc), dg.party_y, dg.party_x) > 3) return;
        npc_follow_party(slot, npc);
        break;
    case 5: case 7:
        npc_follow_party(slot, npc);
        break;
    case 4:
        if (manhattan_distance(sc->y[slot], sc->x[slot], dg.party_y, dg.party_x) < 4) {
            npc_follow_party(slot, npc);
            break;
        }
        /* fall through */
    case 1:
        npc_wander(sc, npc, slot, 3, &dg.npc_state[npc]);
        break;
    default:
        break;
    }
}

void update_npcs(unsigned hour)
{
    int searched = 0;   /* only one path search per turn */
    dg.npc_event = 0;
    dg.npc_event_slot = 0;
    for (int npc = 1; npc < 0x20; npc++) {
        if (dg.npc_type[npc] == 0) continue;
        int slot = current_schedule_slot((uint8_t)hour, npc);
        NpcState *s = &dg.npc_state[npc];
        NpcSchedule *sc = &dg.npc_schedules[npc];
        uint8_t *path = &dg.npc_paths[npc * 0x20];

        if ((int16_t)s->state <= 1 && npc_check_schedule(hour, npc) == 0) {
            if (s->z == dg.z_level) npc_do_activity(slot, npc);
            continue;
        }
        int state = (int16_t)s->state;
        if (state <= 3) {
            if (dg.npc_path_pos[npc] < 0x8000 && path[dg.npc_path_pos[npc]] != 0) {
                /* follow the stored path one step */
                dg.dir_x = s->x;
                dg.dir_y = s->y;
                step_in_dir(path[dg.npc_path_pos[npc] + 1]);
                uint16_t nx = dg.dir_x, ny = dg.dir_y;
                int can = npc_can_step(slot, npc, ny, nx);
                if (can == 0) {
                    dg.npc_blocked_turns[npc]++;
                    npc_wander(sc, npc, slot, 0, s);
                    if ((int16_t)dg.npc_blocked_turns[npc] > 3) {
                        dg.npc_path_pos[npc] = 0xFFFF;
                        dg.npc_blocked_turns[npc] = 0;
                    }
                } else {
                    MapObject *o = &dg.objects[s->object];
                    o->x = (uint8_t)nx;
                    s->x = nx & 0xFF;
                    o->y = (uint8_t)ny;
                    s->y = ny & 0xFF;
                    dg.redraw_flags |= 2;
                    uint16_t pos = dg.npc_path_pos[npc];
                    path[pos]--;
                    dg.npc_blocked_turns[npc] = 0;
                    if (path[pos] == 0) {   /* run finished: next pair */
                        dg.npc_path_pos[npc]++;
                        path[dg.npc_path_pos[npc]++] = 0;
                        if ((int16_t)dg.npc_path_pos[npc] > 0x1F || path[dg.npc_path_pos[npc]] == 0)
                            dg.npc_path_pos[npc] = 0xFFFF;
                        if (can == 2) {   /* arrived */
                            s->sched_slot = (uint16_t)slot;
                            s->state = 1;
                            dg.npc_path_pos[npc] = 0xFFFF;
                        }
                    }
                }
            } else if (dg.npc_path_pos[npc] == 0xFFFF && s->state == 3) {
                /* reached the stairs: next turn it changes floor. The original returns here,
                 * skipping the remaining NPCs this turn. */
                s->state = (int8_t)dg.z_level < (int8_t)sc->z[current_schedule_slot((uint8_t)hour, npc)] ? 6 : 7;
                return;
            } else if (searched < 1) {
                if (s->state != 1) {
                    if ((int16_t)dg.npc_blocked_turns[npc] < 200 &&
                        (dg.npc_blocked_turns[npc] == 0 || random_between(2, 0) == 1)) {
                        if (dg.npc_path_pos[npc] == 0xFFFF) {
                            searched++;
                            if (npc_find_path(npc, 0, sc->y[slot], sc->x[slot], sy(npc), sx(npc))) {
                                npc_store_path(sc->y[slot], sc->x[slot], 0, npc);
                                dg.npc_blocked_turns[npc] = 0;
                                continue;
                            }
                            dg.npc_blocked_turns[npc] = 200;   /* no path: give up for a while */
                        }
                        npc_wander(sc, npc, slot, 0, s);
                    } else {
                        if ((int16_t)dg.npc_blocked_turns[npc] >= 200) dg.npc_blocked_turns[npc]++;
                        if ((int16_t)dg.npc_blocked_turns[npc] > 0xCC) dg.npc_blocked_turns[npc] = 0;
                    }
                }
            } else if (s->z == dg.z_level) {
                npc_do_activity((int16_t)s->sched_slot, npc);
            }
        } else if (state == 4 || state == 5) {
            /* coming from another floor: appear on the stairs nearest its spot, then walk */
            if (searched == 1) continue;
            searched = 1;
            int mode = state == 4 ? 3 : 4;
            int found = npc_path_to_stairs(npc, mode == 3, sc->y[slot], sc->x[slot]);
            uint16_t ax = 0, ay = 0;
            if (found) {
                ax = dg.dir_x;
                ay = dg.dir_y;
                found = npc_find_path(npc, mode, sc->y[slot], sc->x[slot], (int16_t)ay, (int16_t)ax);
            }
            if (!found) continue;
            npc_store_path(sc->y[slot], sc->x[slot], mode, npc);
            uint8_t t = *ds_at((long)offsetof(struct DGroup, map_buffer) + ay * 0x20 + ax);
            if ((mode == 3 && t == 0xC8) || (mode == 4 && t == 0xC9) || (t & 0xFC) == 0xC4)
                place_npc(dg.z_level, ay, ax, npc);
            s->state = 2;
        } else if ((state == 6 || state == 7) && !npc_on_stairs(slot, npc)) {
            /* walk to the stairs on this floor */
            if (s->state == 2 || searched == 1) continue;
            searched = 1;
            int mode = state == 6 ? 1 : 2;
            int found = npc_path_to_stairs(npc, mode == 1, s->y, s->x);
            uint16_t ax = 0, ay = 0;
            if (found) {
                ax = dg.dir_x;
                ay = dg.dir_y;
                found = npc_find_path(npc, mode, (int16_t)ay, (int16_t)ax, sy(npc), sx(npc));
            }
            if (found) {
                npc_store_path((int16_t)ay, (int16_t)ax, mode, npc);
                s->state = 3;
            }
        } else {
            /* on the stairs, or both spots on other floors: jump to the spot */
            place_npc(sc->z[slot], sc->y[slot], sc->x[slot], npc);
            s->sched_slot = (uint16_t)slot;
            dg.npc_path_pos[npc] = 0xFFFF;
            s->state = 1;
        }
    }
}

/* The entry whose change time passed most recently (hours since times[0..3]; the fourth time
 * goes back to entry 1). */
uint8_t current_schedule_slot(uint8_t hour, int npc)
{
    const uint8_t *t = dg.npc_schedules[npc].times;
    uint8_t best = (uint8_t)(hour - t[0]), slot = 0;
    if ((uint8_t)(hour - t[1]) < best) { best = (uint8_t)(hour - t[1]); slot = 1; }
    if ((uint8_t)(hour - t[2]) < best) { best = (uint8_t)(hour - t[2]); slot = 2; }
    if ((uint8_t)(hour - t[3]) < best) slot = 1;
    return slot;
}

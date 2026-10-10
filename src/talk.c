/* talk.c - conversations (TALK.OVL). See talk.h and docs/conversations.md. */
#include <string.h>
#include "commands.h"
#include "data.h"
#include "sound.h"
#include "gfx.h"
#include "input.h"
#include "map.h"
#include "misc.h"
#include "npc.h"
#include "objects.h"
#include "shops.h"
#include "status.h"
#include "talk.h"
#include "text.h"
#include "town.h"
#include "view.h"

#define TEXT_BUFFER ((uint16_t)offsetof(struct DGroup, text_buffer))
#define TP          (*ds_at(dg.talk_ptr))          /* byte at the script pointer */

/* talk_input[i - 1] == ' ' : the original reads it as ds:BCF7 + i */
static int word_start(int pos) { return pos == 0 || (pos > 0 && dg.talk_input[pos - 1] == ' '); }

static int find_word(const char *needle)
{
    int pos = str_find_nocase(dg.talk_input, needle);
    return pos != -1 && word_start(pos);
}

/* The script string at talk_ptr as a C string (scripts end strings with NUL). */
static const char *script_str(void) { return (const char *)ds_at(dg.talk_ptr); }

int str_equal_nocase(const char *a, const char *b)
{
    for (;;) {
        if (to_upper((uint8_t)(*a & 0x7F)) != to_upper((uint8_t)(*b & 0x7F))) return 0;
        a++;
        b++;
        if (*a == 0 && *b == 0) return 1;
    }
}

/* Counters and tables: 0x29, 0x94-0x9C, 0xA5, 0xAE, 0xBA, 0xBB, 0xBE, 0xCA, 0xCB. */
int is_talk_through_tile(unsigned y, unsigned x)
{
    uint8_t t = *get_map_tile_ptr(y, x);
    return t == 0x29 || (t >= 0x94 && t <= 0x9C) || t == 0xA5 || t == 0xAE || t == 0xBA || t == 0xBB ||
           t == 0xBE || t == 0xCA || t == 0xCB;
}

int ask_pay_yes_no(void)
{
    print_text(ds_str(0x9052));   /* "Dost thou pay?" */
    for (;;) {
        unsigned key = to_upper((uint8_t)get_command_key());
        if (key == 'Y') {
            print_text(ds_str(0x9066));
            return 0;
        }
        if (key == 'N' || platform_quit_requested()) break;
    }
    print_text(ds_str(0x906C));
    return 1;
}

/* Merchant 0x81-0x88 = shop type 0-7. shop_town is this location's index in the type's list
 * (ds:23CA, 16 per type); it selects the keeper and shop names (ds:21CA / 22CA) and the prices. */
void open_shop(int merchant)
{
    if ((dg.transport & 0xFE) == 0x12 && merchant != 0x83) {
        print_text(ds_str(0x9072));   /* "A merchant says: GET THAT HORSE OUT..." */
        return;
    }
    int member;
    if (dg.active_player == 0xFF) {
        check_party_state();
        member = (int16_t)dg.dir_x;
    } else {
        member = dg.active_player;
    }
    dg.shop_type = (uint16_t)(merchant - 0x81);
    dg.shop_town = 0;
    if (ds_u8((uint16_t)(0x23CA + dg.shop_type * 0x10)) != dg.location) {
        dg.shop_town = 0;
        do {
            if ((int16_t)dg.shop_town > 0x0F) break;
            dg.shop_town++;
        } while (ds_u8((uint16_t)(0x23CA + dg.shop_type * 0x10 + dg.shop_town)) != dg.location);
    }
    uint16_t i = (uint16_t)(dg.shop_type * 0x20 + dg.shop_town * 2);
    dg.shop_name = ds_u16((uint16_t)(0x21CA + i));
    dg.shopkeeper_name = ds_u16((uint16_t)(0x22CA + i));
    switch (dg.shop_type) {
    case 0: shop_armoury(member); break;
    case 1: shop_tavern(member); break;
    case 2: shop_stables(member); break;
    case 3: shop_shipwright(member); break;
    case 4: shop_reagents(member); break;
    case 5: shop_provisioner(member); break;
    case 6: shop_healer(); break;
    case 7: shop_inn(member); break;
    }
}

int guard_toll(void)
{
    if (dg.location == 0x12) {
        if (dg.active_spell == 0x1D) {   /* password check (the active_spell byte doubles as a flag here) */
            char answer[16];
            put_char('"');
            print_text(ds_str(0x90FC));   /* "Give now the password, bearer of..." */
            put_char('"');
            print_text(ds_str(0x9128));   /* "Your response:" */
            read_line(0x0E, answer);      /* into a 4-byte buffer in the original; cut to 4 */
            put_char('\n');
            answer[4] = 0;
            if (str_equal_nocase(answer, ds_str(0x4A9A))) {   /* "IMPE" */
                put_char('\n');
                put_char('"');
                print_text(ds_str(0x913A));   /* "Pass, friend." */
                put_char('"');
                put_char('\n');
                return 0;
            }
        }
        return 1;
    }
    if (dg.location == 5) {
        put_char('"');
        print_text(ds_str(0x90A2));   /* "Thou wilt give half thy gold to ..." */
        put_char('"');
        if (ask_pay_yes_no()) return 1;
        dg.gold = (uint16_t)((int16_t)dg.gold / 2);
    } else {
        unsigned toll = 0;
        for (int i = 0; i < dg.party_size; i++)
            if (dg.party[i].status != 'D') toll += 10;
        print_text(ds_str(0x90CC));   /* "A guard demands a" */
        print_number(' ', 2, (int)toll);
        print_text(ds_str(0x90E0));   /* "gp tribute to Blackthorn!" */
        if (ask_pay_yes_no() || dg.gold < toll) return 1;
        dg.gold = (uint16_t)(dg.gold - toll);
    }
    return 0;
}

int talk_to_npc(int npc)
{
    put_char('\n');
    dg.talk_npc = (uint16_t)npc;
    NpcState *s = &dg.npc_state[npc];
    uint8_t *activity = &dg.npc_schedules[npc].activity[s->sched_slot];
    if (*activity == 4) {
        *activity = 1;   /* came to talk: now just wanders */
    } else if (dg.objects[s->object].tile == 0x70 && ((s->sched_slot & 1) == 0 || s->dialogue == 0)) {
        print_text(ds_str(0x9148));   /* "The guard offers no response!" */
        return 0;
    }
    int d = (int16_t)s->dialogue;
    if (d == 0) {
        print_text(ds_str(0x9168));   /* "No response!" */
    } else if (d < 0x80) {
        talk_run_conversation(d);
    } else if (d == 0xFD) {
        put_char('"');
        print_text(ds_str(0x9176));   /* "Don't hurt me! Please go away!" */
        put_char('"');
        put_char('\n');
    } else if (d == 0xFE) {
        npc_begone(npc);
    } else if (d == 0xFF) {
        return guard_toll();
    } else if ((s->sched_slot & 1) && (current_schedule_slot(dg.hour, npc) & 1)) {
        open_shop(d);   /* merchants are at the shop in schedule entry 1 */
    } else {
        print_text(ds_str(0x9196));   /* "A merchant says: Come see me at my shoppe, " */
        print_text(ds_str(0x91C4));   /* "when it's open!" */
    }
    return 0;
}

int cmd_talk(void)
{
    if (!ask_direction()) return 0;
    int16_t dx = (int16_t)dg.dir_x, dy = (int16_t)dg.dir_y;
    unsigned x = (unsigned)(dx + dg.party_x), y = (unsigned)(dy + dg.party_y);
    if (find_object_at(dg.z_level, (uint8_t)y, (uint8_t)x) == 0 && is_talk_through_tile(y, x)) {
        x += (unsigned)dx;
        y += (unsigned)dy;
    }
    if (find_object_at(dg.z_level, (uint8_t)y, (uint8_t)x) == 0) {
        print_text(ds_str(0x91D6));   /* "Nobody's here!" */
        return 0;
    }
    int npc = npc_for_object((int16_t)dg.dir_x);
    uint8_t t = *get_map_tile_ptr(y, x);
    if (t == 0x9D) print_text(ds_str(0x91F6));        /* in bed: "No response!" */
    else if (t == 0xAB) print_text(ds_str(0x91E8));   /* "Zzzzzz..." */
    else if (npc < 0) print_text(ds_str(0x91F6));     /* not an NPC (horse ...); the original reads slot -1 */
    else return talk_to_npc(npc);
    return 0;
}

void talk_emit_newline(void) { talk_exec_op(0x8D); }
void talk_emit_quote(void) { talk_exec_op(0xA2); }

/* Prints the buffered word. A space at column 0 is dropped, and a newline right after the window
 * wrapped by itself (last character at column 15) is dropped. */
void talk_flush_word(void)
{
    if (dg.talk_word_len == 0) return;
    for (unsigned i = 0; i < dg.talk_word_len;) {
        select_font((dg.talk_word_buf[i] & 0x80) == 0);
        uint8_t c = dg.talk_word_buf[i] & 0x7F;
        i++;
        if (get_cursor_col() == 0 && c == ' ') continue;
        if (dg.talk_last_col >= 0x0F && c == '\n') {
            dg.talk_last_col = 0;
            continue;
        }
        if (c != '\n') dg.talk_last_col = get_cursor_col();
        put_char(c);
    }
    dg.talk_word_len = 0;
    select_font(0);
}

/* Up to 16 characters; a space (0xA0) or newline (0x8A) ends the word: a new line first if it
 * wouldn't fit (column + length >= 18), then it is printed. A 17th character is dropped. */
void talk_buffer_char(uint8_t c)
{
    if (dg.talk_word_len != 0x10) {
        dg.talk_word_buf[dg.talk_word_len++] = c;
        if (c != 0x8A && c != 0xA0) return;
        if (get_cursor_col() + dg.talk_word_len >= 0x12) put_char('\n');
    }
    talk_flush_word();
}

/* Three digit operands (ASCII with bit 7); beggars (0x6C-0x6F) give karma for alms. */
int talk_op_pay_gold(void)
{
    int amount = (dg.talk_operands[0] & 0x7F) * 100 + (dg.talk_operands[1] & 0x7F) * 10 +
                 (dg.talk_operands[2] & 0x7F) - 0x14D0;
    if ((int16_t)dg.gold < amount) {
        dg.talk_word_len = 0;
        put_char('"');
        print_text(ds_str(0x9328));   /* "Thou hast not enough gold!" */
        put_char('"');
        print_text(ds_str(0x9344));
        dg.talk_pending_op = 0;
        dg.talk_operand_count = 0;
        return talk_main_loop();
    }
    dg.gold = (uint16_t)(dg.gold - amount);
    if ((dg.objects[dg.npc_state[dg.talk_npc].object].tile & 0xFC) == 0x6C && dg.alms_counter > 99) {
        dg.alms_counter = 0;
        add_byte_capped(99, 1, &dg.karma);
        if (dg.gold == 0) add_byte_capped(99, 2, &dg.karma);
    }
    return 0;
}

/* < 0x40 inventory, 0x41 food, 0x42 gold, 0x43 keys, 0x44 gems, 0x45 torches, 0x46 grapples,
 * 0x47 magic carpets, 0x48-0x4A quest items, 0x4B odd keys. */
void talk_op_give_item(uint8_t item)
{
    uint8_t *p = NULL;
    if (item < 0x40) {
        p = &dg.inventory[item];
    } else {
        switch (item) {
        case 0x41: add_word_capped(9999, 1, &dg.food); return;
        case 0x42: add_word_capped(9999, 1, &dg.gold); return;
        case 0x43: p = &dg.keys; break;
        case 0x44: p = &dg.gems; break;
        case 0x45: p = &dg.torches; break;
        case 0x46: p = &dg.grapples; break;
        case 0x47: p = &dg.magic_carpets; break;
        case 0x48: dg.quest_item_57bc = 0xFF; return;
        case 0x49: dg.quest_item_57ba = 0xFF; return;
        case 0x4A: dg.quest_item_57be = 0xFF; return;
        case 0x4B: p = &dg.odd_keys; break;
        default: return;
        }
    }
    add_byte_capped(99, 1, p);
}

/* Advances past the next match byte (1) or stop byte (0). Stops at the end of the buffer too
 * (the original would run on). */
int talk_skip_to(uint8_t stop, uint8_t match)
{
    for (;;) {
        if (dg.talk_ptr >= TEXT_BUFFER + 0x400) return 0;
        uint8_t b = TP;
        dg.talk_ptr++;
        if (b == match) return 1;
        if (b == stop) return 0;
    }
}

void talk_seek_string(int index)
{
    dg.talk_ptr = TEXT_BUFFER;
    for (; index != 0; index--) talk_skip_to(0x90, 0);
}

int talk_run_string(void)
{
    while (TP != 0) {
        uint8_t op = TP;
        dg.talk_ptr++;
        if (talk_exec_op(op)) return 1;
        if (dg.talk_ptr >= TEXT_BUFFER + 0x400) return 0;
    }
    return 0;
}

int talk_say_string(int index)
{
    talk_seek_string(index & 0xFF);
    return talk_run_string();
}

/* "Same answer as the next keyword": skip to the next string (the keyword), past it, run its answer. */
int talk_op_or(void)
{
    uint16_t p = (uint16_t)(dg.talk_ptr + 1);
    while (*ds_at(p) == 0 && p < TEXT_BUFFER + 0x400) p++;
    do {
        p++;
    } while (*ds_at(p - 1) != 0 && p < TEXT_BUFFER + 0x400);
    dg.talk_ptr = p;
    return talk_run_string();
}

void talk_op_avatar_name(void)
{
    for (const char *n = dg.party[0].name; *n; n++) talk_exec_op((uint8_t)(*n | 0x80));
}

/* The NPC joins if a party member (1-15) has the same first three letters as its name: that
 * member is swapped into the next party slot. */
int talk_op_join_party(void)
{
    uint16_t saved = dg.talk_ptr;
    if (dg.party_size == 6) {
        get_command_key();
        print_text(ds_str(0x9348));   /* "Thou hast no room for me in thy party! " */
        print_text(ds_str(0x9372));   /* "Seek me again if one of thy members doth leave thee." */
        return 0;
    }
    char npc_name[4], member[4];
    talk_seek_string(0);
    memcpy(npc_name, script_str(), 3);
    npc_name[3] = 0;
    dg.talk_ptr += 3;
    for (int i = 15; i > 0; i--) {
        memcpy(member, dg.party[i].name, 3);
        member[3] = 0;
        if (str_equal_nocase(member, npc_name)) {
            PartyMember tmp = dg.party[i];
            tmp.where = 0;
            dg.party[i] = dg.party[dg.party_size];
            dg.party[dg.party_size] = tmp;
            dg.party_size++;
            mark_npc_dead(dg.talk_npc);
            remove_npc(dg.talk_npc);
            talk_flush_word();
            dg.talk_ptr = saved;
            return 1;
        }
    }
    talk_buffer_char(0x22);
    talk_buffer_char('\n');
    print_text(ds_str(0x93A8));   /* "System Error - No Match!" */
    dg.talk_ptr = saved;
    return 1;
}

/* Label sections follow the keywords, each after a 0x90 byte; 0x9F ends them. */
static void seek_label(void)
{
    dg.talk_ptr = TEXT_BUFFER;
    while (TP != dg.talk_label && dg.talk_ptr < TEXT_BUFFER + 0x400) talk_skip_to(0x9F, 0x90);
}

int talk_goto_label(void)
{
    seek_label();
    dg.talk_ptr++;
    return talk_run_string();
}

int talk_run_next_string(void)
{
    talk_skip_to(0x90, 0);
    return talk_run_string();
}

int talk_goto_label_default(void)
{
    seek_label();
    talk_skip_to(0x9F, 0);
    return talk_run_string();
}

int talk_seek_keyword(int index)
{
    dg.talk_ptr = TEXT_BUFFER;
    for (int n = index * 2 + 5; n != 0; n--)
        if (!talk_skip_to(0x90, 0)) return 0;
    return 1;
}

int talk_match_keyword(void)
{
    for (dg.talk_keyword_index = 0;; dg.talk_keyword_index++) {
        if (!talk_seek_keyword(dg.talk_keyword_index)) return 0;
        if (find_word(script_str())) return 1;
        dg.talk_ptr++;
    }
}

void talk_read_input(void)
{
    talk_flush_word();
    read_line(0x0F, dg.talk_input);
}

int talk_say_bye(void)
{
    talk_emit_quote();
    if (talk_say_string(4) == 0) {
        talk_emit_quote();
        talk_emit_newline();
    }
    return 1;
}

static void rebuke_pause(void)
{
    for (int i = 0; i < 0x1C; i++) {   /* about 1.5 s, or a key */
        world_tick();
        if (poll_key() != 0) break;
        delay_ticks(1);
    }
    flush_keyboard();
}

/* Built-in words (ds:4AA8): 0 NAME, 1-2 JOB/WORK, 3-4 BYE/THANK, 5-33 swearing. Returns 2 while a
 * question is pending (the word is then taken as an answer), 1 if the conversation ended. */
int talk_builtin_keyword(uint8_t index)
{
    int r;
    if (index < 5 && dg.talk_question_pending) return 2;
    if (index == 0) {
        print_text(ds_str(0x93C2));   /* "\"My name is " */
        r = talk_say_string(0);
    } else if (index < 3) {
        talk_emit_quote();
        r = talk_say_string(3);
    } else if (index < 5) {
        return talk_say_bye();
    } else {
        print_text(ds_str(0x93D0));   /* "With language like that, how did you become an Avatar?" */
        talk_emit_quote();
        talk_emit_newline();
        talk_emit_newline();
        rebuke_pause();
        return 0;
    }
    if (r) return 1;
    talk_emit_quote();
    talk_emit_newline();
    talk_emit_newline();
    return 0;
}

/* -1 none matched, 0 handled, 1 conversation ended, 2 ignored (question pending) */
static int check_builtins(void)
{
    int r = -1;
    for (uint8_t i = 0; i < 0x22; i++) {
        if (!find_word(ds_str(dg.talk_builtin_keywords[i]))) continue;
        r = talk_builtin_keyword(i);
        if (r == 0 || r == 1) return r;
    }
    return r;
}

int talk_main_loop(void)
{
    for (;;) {
        dg.talk_question_pending = 0;
        print_text(ds_str(0x9408));   /* "Your interest?\n:" */
        talk_read_input();
        if (dg.talk_input[0] == 0 || platform_quit_requested()) {
            print_text(ds_str(0x941A));   /* "BYE" */
            return talk_say_bye();
        }
        talk_emit_newline();
        talk_emit_newline();
        int r = check_builtins();
        if (r == 1) return 1;
        if (r == 0) continue;
        if (!talk_match_keyword()) {
            print_text(ds_str(0x9420));   /* "\"I cannot help thee with that." */
        } else {
            talk_emit_quote();
            if (talk_say_string(dg.talk_keyword_index * 2 + 6)) return 1;
        }
        talk_emit_quote();
        talk_emit_newline();
        talk_emit_newline();
    }
}

/* After the label: the question, the default reply, then (answer, reply) pairs up to 0x90. */
int talk_match_label_answer(void)
{
    seek_label();
    talk_skip_to(0x9F, 0);
    talk_skip_to(0x9F, 0);
    do {
        if (find_word(script_str())) return 1;
    } while (talk_skip_to(0x90, 0) && talk_skip_to(0x90, 0));
    return 0;
}

int talk_ask_question(void)
{
    int r;
    do {
        talk_emit_quote();
        if (talk_goto_label()) return 1;
        talk_emit_quote();
        do {
            talk_emit_newline();
            talk_emit_newline();
            dg.talk_question_pending = 0xFF;
            print_text(ds_str(0x9440));   /* "You respond-\n:" */
            talk_read_input();
            if (platform_quit_requested()) return 1;
            if (dg.talk_input[0] == 0) print_text(ds_str(0x9450));   /* "What didst thou say?" */
        } while (dg.talk_input[0] == 0);
        talk_emit_newline();
        talk_emit_newline();
        r = check_builtins();
        if (r == 1) return 1;
    } while (r == 0);   /* swearing asks again */
    talk_emit_quote();
    r = talk_match_label_answer() ? talk_run_next_string() : talk_goto_label_default();
    if (r) return 1;
    talk_emit_quote();
    talk_emit_newline();
    talk_emit_newline();
    return talk_main_loop();
}

void talk_mark_met(int npc) { dg.npc_met_bits[dg.location] |= (uint32_t)1 << (npc & 31); }
int talk_has_met(int npc) { return (dg.npc_met_bits[dg.location] >> (npc & 31)) & 1; }

static void clear_pending(void)
{
    dg.talk_operand_count = 0;
    dg.talk_pending_op = 0;
}

/* Operands of 0x85 (3 digits), 0x86 (item), 0x8C (label if met; 0xFF = main loop) and 0xFE
 * (karma, label: ask the question if karma >= the first operand). */
int talk_op_operands(uint8_t b)
{
    switch (dg.talk_pending_op) {
    case 0x85:
        dg.talk_operands[dg.talk_operand_count++] = b;
        if (dg.talk_operand_count != 3) return 0;
        if (talk_op_pay_gold()) return 1;
        break;
    case 0x86:
        talk_op_give_item(b & 0x7F);
        break;
    case 0x8C:
        clear_pending();
        if (!talk_has_met(dg.talk_npc)) return 0;
        if (b == 0xFF) return talk_main_loop();
        dg.talk_label = b;
        return talk_ask_question();
    case 0xFE:
        dg.talk_operands[dg.talk_operand_count++] = b;
        if (dg.talk_operand_count != 2) return 0;
        clear_pending();
        if (dg.karma < dg.talk_operands[0]) return 0;
        dg.talk_label = dg.talk_operands[1];
        return talk_ask_question();
    }
    clear_pending();
    return 0;
}

/* Matches the first four letters of each party member's name: the NPC now knows the party. */
void talk_op_ask_name(void)
{
    talk_emit_quote();
    talk_flush_word();
    print_text(ds_str(0x9468));   /* "What is thy name?\"" */
    print_text(ds_str(0x947C));   /* "You respond-" */
    talk_read_input();
    clear_pending();
    if (dg.talk_input[0] != 0) {
        for (unsigned i = 0; i < dg.party_size; i++) {
            char name[5];
            memcpy(name, dg.party[i].name, 4);
            name[4] = 0;
            if (find_word(name)) {
                talk_mark_met(dg.talk_npc);
                print_text(ds_str(0x94A0));   /* "A pleasure!" */
                return;
            }
        }
        print_text(ds_str(0x94B0));   /* "If you say so..." */
        return;
    }
    print_text(ds_str(0x948C));
}

/* One script byte. Returns 1 if the conversation has ended. */
int talk_exec_op(uint8_t op)
{
    uint16_t resume = dg.talk_ptr;   /* most ops leave the pointer where it was */
    if (dg.talk_pending_op != 0) return talk_op_operands(op);
    dg.talk_operand_count = 0;
    if (op == 0xA2 && dg.talk_last_op == 0xA2) return 0;   /* no doubled quotes */
    switch (op) {
    case 0x81: talk_op_avatar_name(); resume = dg.talk_ptr; break;
    case 0x82: return 1;                                    /* end */
    case 0x83: rebuke_pause(); resume = dg.talk_ptr; break; /* pause */
    case 0x84: return talk_op_join_party();
    case 0x85: case 0x86: case 0x8C: case 0xFE: dg.talk_pending_op = op; break;
    case 0x87:
        if (talk_op_or()) return 1;
        break;
    case 0x88: talk_op_ask_name(); resume = dg.talk_ptr; break;
    case 0x89: add_byte_capped(99, 1, &dg.karma); return 0;
    case 0x8A: sub_byte_floor0(1, &dg.karma); return 0;
    case 0x8B: alert_town(); return 0;
    case 0x8E: dg.talk_font_mask ^= 0x80; return 0;
    case 0x8F: get_command_key(); return 0;
    case 0xFF:
        talk_flush_word();
        return talk_main_loop();
    default:
        if (op > 0x90 && op < 0xA0) {   /* a label inside text: ask its question */
            dg.talk_label = op;
            return talk_ask_question();
        }
        if (op < 0x81) {   /* a word from the table, with spaces around it */
            const char *w = ds_str(dg.talk_words[op]);
            talk_buffer_char(0xA0);
            for (const char *p = w; *p; p++) talk_buffer_char((uint8_t)(*p | 0x80));
            if (*w == 0) talk_buffer_char(op);
            else dg.talk_need_space = 1;
        } else {   /* a character; 0x8D = new line */
            op |= 0x80;
            if (op == 0x8D) op = 0x8A;
            uint8_t c = op & dg.talk_font_mask;
            dg.talk_last_op = c;
            if (dg.talk_need_space) talk_buffer_char(0xA0);
            talk_buffer_char(c);
            dg.talk_need_space = 0;
        }
        resume = dg.talk_ptr;
        break;
    }
    dg.talk_ptr = resume;
    return 0;
}

/* "You see <description>", then the greeting if the NPC knows the party, else half the time
 * "I am called <name>". */
int talk_greeting(void)
{
    print_text(ds_str(0x94C4));   /* "You see " */
    if (talk_say_string(1)) return 1;
    talk_emit_newline();
    talk_emit_newline();
    int r;
    if (!talk_has_met(dg.talk_npc)) {
        set_random_seed((uint16_t)time_seed());
        if (random_between(1, 0) == 0) return 0;
        print_text(ds_str(0x94CE));   /* "\"I am called " */
        r = talk_say_string(0);
    } else {
        talk_emit_quote();
        r = talk_say_string(2);
    }
    if (r) return 1;
    talk_emit_quote();
    talk_emit_newline();
    talk_emit_newline();
    return 0;
}

/* With the Shadowlord of Falsehood (0) in town, something is stolen after each conversation. */
void talk_maybe_steal(void)
{
    if (dg.shadowlord_here != 0) return;
    print_text(ds_str(0x94DC));   /* "Something was stolen!" */
    sound_slide(0x32, 1, 2000, 800);
    set_random_seed((uint16_t)time_seed());
    if (dg.keys == 0 && dg.gems == 0 && dg.torches == 0) {
        for (int i = 0x2F; i >= 0; i--)
            if (dg.inventory[i]) { sub_byte_floor0(1, &dg.inventory[i]); return; }
        for (int i = 7; i >= 0; i--)
            if (dg.potions[i]) { sub_byte_floor0(1, &dg.potions[i]); return; }
        for (int i = 7; i >= 0; i--)
            if (dg.scrolls[i]) { sub_byte_floor0(1, &dg.scrolls[i]); return; }
        sub_word_floor0((uint16_t)random_between(0x0F, 1), &dg.gold);
        return;
    }
    for (;;) {
        int r = random_between(2, 0);
        uint8_t *p = r == 0 ? &dg.keys : r == 1 ? &dg.gems : &dg.torches;
        if (*p) { sub_byte_floor0(1, p); return; }
    }
}

/* The .TLK index (count, then (script, offset) pairs) is read into text_buffer, then the script. */
void talk_run_conversation(int script)
{
    const char *file = ds_str(dg.tlk_files[((unsigned)dg.location - 1) >> 3]);
    load_file_part(0, 0x200, dg.text_buffer, file);
    unsigned count = ds_u16(TEXT_BUFFER), entry = 0;
    for (unsigned i = 1; i <= count && i < 0x80; i++, entry++)
        if (ds_u16((uint16_t)(TEXT_BUFFER + 2 + entry * 4)) == script) break;
    uint16_t offset = ds_u16((uint16_t)(TEXT_BUFFER + 2 + entry * 4 + 2));
    memset(dg.text_buffer, 0, 0x400);
    load_file_part(offset, 0x400, dg.text_buffer, file);   /* may be short at the end of the file */
    dg.talk_word_len = 0;
    dg.talk_need_space = 0;
    if (talk_greeting() == 0 && talk_main_loop() == 0) talk_say_bye();
    talk_maybe_steal();
}

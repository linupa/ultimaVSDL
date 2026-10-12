/* talk.cpp - conversations (TALK.OVL). See talk.h and docs/conversations.md. */
#include <string.h>
#include "commands.h"
#include "gamedata.h"
#include "gametime.h"
#include "map.h"
#include "party.h"
#include "savegame.h"
#include "sound.h"
#include "gfx.h"
#include "input.h"
#include "misc.h"
#include "npc.h"
#include "objects.h"
#include "shops.h"
#include "status.h"
#include "strings.h"
#include "talk.h"
#include "text.h"
#include "textbuf.h"
#include "town.h"
#include "view.h"

#define TP          (*text_buffer.at(conversation.ptr))   /* byte at the script pointer */

Conversation::Conversation()
{
    SaveGame &save = SaveGame::instance();   /* not `savegame`: this runs before main, in no set order */
    save.add(0x588B, alms_counter, "conversation.alms_counter");
    save.add(0x5BDA, &met_bits[1], 32 * sizeof met_bits[0], "conversation.met_bits");

    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x24E8, words_, "conversation.words");
    data.add(0x4AA8, builtin_keywords_, "conversation.builtin_keywords");
}

Conversation &Conversation::instance()
{
    static Conversation the_conversation;
    return the_conversation;
}

Conversation &conversation = Conversation::instance();

Strings::Id Conversation::words_[Conversation::WORDS];   /* ds:24E8 */

Strings::Id Conversation::builtin_keywords_[Conversation::BUILTIN_KEYWORDS];   /* ds:4AA8 */

/* talk_input[i - 1] == ' ' : the original reads it as ds:BCF7 + i */
int Conversation::word_start(int pos) { return pos == 0 || (pos > 0 && conversation.input[pos - 1] == ' '); }

int Conversation::find_word(const char *needle)
{
    int pos = str_find_nocase(conversation.input, needle);
    return pos != -1 && word_start(pos);
}

/* The script string at talk_ptr as a C string (scripts end strings with NUL). */
const char *Conversation::script_str(void) { return (const char *)text_buffer.at(conversation.ptr); }

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
int Conversation::is_talk_through_tile(unsigned y, unsigned x)
{
    uint8_t t = *map.get_map_tile_ptr(y, x);
    return t == 0x29 || (t >= 0x94 && t <= 0x9C) || t == 0xA5 || t == 0xAE || t == 0xBA || t == 0xBB ||
           t == 0xBE || t == 0xCA || t == 0xCB;
}

int Conversation::ask_pay_yes_no(void)
{
    print_text(strings[Strings::Dost_thou_pay_9052]);   /* "Dost thou pay?" */
    for (;;) {
        unsigned key = to_upper((uint8_t)get_command_key());
        if (key == 'Y') {
            print_text(strings[Strings::Yes_9066]);
            return 0;
        }
        if (key == 'N' || platform->quit_requested()) break;
    }
    print_text(strings[Strings::No_906C]);
    return 1;
}

/* Merchant 0x81-0x88 = shop type 0-7. shop_town is this location's index in the type's list
 * (ds:23CA, 16 per type); it selects the keeper and shop names (ds:21CA / 22CA) and the prices. */
void Conversation::open_shop(int merchant)
{
    if ((party.transport & 0xFE) == 0x12 && merchant != 0x83) {
        print_text(strings[Strings::A_merchant_says_GET_THAT_9072]);   /* "A merchant says: GET THAT HORSE OUT..." */
        return;
    }
    int member;
    if (party.active_player == 0xFF) {
        member = (int)party.first_able();
    } else {
        member = party.active_player;
    }
    town.shop_type = (uint16_t)(merchant - 0x81);
    town.shop_town = 0;
    if (Town::table_byte(Town::SHOP_LOCATION + town.shop_type * 0x10) != map.id()) {
        town.shop_town = 0;
        do {
            if ((int16_t)town.shop_town > 0x0F) break;
            town.shop_town++;
        } while (Town::table_byte(Town::SHOP_LOCATION + town.shop_type * 0x10 + town.shop_town) != map.id());
    }
    uint16_t i = (uint16_t)(town.shop_type * 0x20 + town.shop_town * 2);
    town.shop_name = Town::table_word(Town::SHOP_NAME + i);
    town.shopkeeper_name = Town::table_word(Town::SHOPKEEPER_NAME + i);
    switch (town.shop_type) {
    case 0: town.shop_armoury(member); break;
    case 1: town.shop_tavern(member); break;
    case 2: town.shop_stables(member); break;
    case 3: town.shop_shipwright(member); break;
    case 4: town.shop_reagents(member); break;
    case 5: town.shop_provisioner(member); break;
    case 6: town.shop_healer(); break;
    case 7: town.shop_inn(member); break;
    }
}

int Conversation::guard_toll(void)
{
    if (map == Map::PALACE_OF_BLACKTHORN) {
        if (party.active_spell == 0x1D) {   /* password check (the active_spell byte doubles as a flag here) */
            char answer[16];
            put_char('"');
            print_text(strings[Strings::Give_now_the_password_90FC]);   /* "Give now the password, bearer of..." */
            put_char('"');
            print_text(strings[Strings::Your_response_9128]);   /* "Your response:" */
            read_line(0x0E, answer);      /* into a 4-byte buffer in the original; cut to 4 */
            put_char('\n');
            answer[4] = 0;
            if (str_equal_nocase(answer, strings[Strings::IMPE_4A9A])) {   /* "IMPE" */
                put_char('\n');
                put_char('"');
                print_text(strings[Strings::Pass_friend_913A]);   /* "Pass, friend." */
                put_char('"');
                put_char('\n');
                return 0;
            }
        }
        return 1;
    }
    if (map == Map::MINOC) {
        put_char('"');
        print_text(strings[Strings::Thou_wilt_give_half_thy_90A2]);   /* "Thou wilt give half thy gold to ..." */
        put_char('"');
        if (ask_pay_yes_no()) return 1;
        party.gold = (uint16_t)((int16_t)party.gold / 2);
    } else {
        unsigned toll = 0;
        for (int i = 0; i < party.size(); i++)
            if (!party[i].dead()) toll += 10;
        print_text(strings[Strings::A_guard_demands_a_90CC]);   /* "A guard demands a" */
        print_number(' ', 2, (int)toll);
        print_text(strings[Strings::gp_tribute_to_Blackthorn_90E0]);   /* "gp tribute to Blackthorn!" */
        if (ask_pay_yes_no() || party.gold < toll) return 1;
        party.gold = (uint16_t)(party.gold - toll);
    }
    draw_status_bar();
    return 0;
}

int Conversation::talk_to_npc(int npc)
{
    put_char('\n');
    conversation.npc = (uint16_t)npc;
    NpcState *s = &npcs.state[npc];
    uint8_t *activity = &npcs.schedules[npc].activity[s->sched_slot];
    if (*activity == 4) {
        *activity = 1;   /* came to talk: now just wanders */
    } else if (map.objects[s->object].tile == 0x70 && ((s->sched_slot & 1) == 0 || s->dialogue == 0)) {
        print_text(strings[Strings::The_guard_offers_no_9148]);   /* "The guard offers no response!" */
        return 0;
    }
    int d = (int16_t)s->dialogue;
    if (d == 0) {
        print_text(strings[Strings::No_response_9168]);   /* "No response!" */
    } else if (d < 0x80) {
        talk_run_conversation(d);
    } else if (d == 0xFD) {
        put_char('"');
        print_text(strings[Strings::Don_t_hurt_me_Please_9176]);   /* "Don't hurt me! Please go away!" */
        put_char('"');
        put_char('\n');
    } else if (d == 0xFE) {
        town.npc_begone(npc);
    } else if (d == 0xFF) {
        return guard_toll();
    } else if ((s->sched_slot & 1) && (npcs.current_schedule_slot(game_time.hour, npc) & 1)) {
        open_shop(d);   /* merchants are at the shop in schedule entry 1 */
    } else {
        print_text(strings[Strings::A_merchant_says_Come_see_9196]);   /* "A merchant says: Come see me at my shoppe, " */
        print_text(strings[Strings::when_it_s_open_91C4]);   /* "when it's open!" */
    }
    return 0;
}

int Conversation::cmd_talk(void)
{
    Direction dir = {0, 0};
    if (!ask_direction(&dir)) return 0;
    int16_t dx = (int16_t)dir.dx, dy = (int16_t)dir.dy;
    unsigned x = (unsigned)(dx + map.x), y = (unsigned)(dy + map.y);
    if (find_object_at(map.z, (uint8_t)y, (uint8_t)x) == 0 && is_talk_through_tile(y, x)) {
        x += (unsigned)dx;
        y += (unsigned)dy;
    }
    uint16_t slot;
    if (find_object_at(map.z, (uint8_t)y, (uint8_t)x, &slot) == 0) {
        print_text(strings[Strings::Nobody_s_here_91D6]);   /* "Nobody's here!" */
        return 0;
    }
    int npc = town.npc_for_object((int16_t)slot);
    uint8_t t = *map.get_map_tile_ptr(y, x);
    if (t == 0x9D) print_text(strings[Strings::No_response_91F6]);        /* in bed: "No response!" */
    else if (t == 0xAB) print_text(strings[Strings::Zzzzzz_91E8]);   /* "Zzzzzz..." */
    else if (npc < 0) print_text(strings[Strings::No_response_91F6]);     /* not an NPC (horse ...); the original reads slot -1 */
    else return talk_to_npc(npc);
    return 0;
}

void Conversation::talk_emit_newline(void) { talk_exec_op(0x8D); }
void Conversation::talk_emit_quote(void) { talk_exec_op(0xA2); }

/* Prints the buffered word. A space at column 0 is dropped, and a newline right after the window
 * wrapped by itself (last character at column 15) is dropped. */
void Conversation::talk_flush_word(void)
{
    if (conversation.word_len == 0) return;
    for (unsigned i = 0; i < conversation.word_len;) {
        select_font((conversation.word_buf[i] & 0x80) == 0);
        uint8_t c = conversation.word_buf[i] & 0x7F;
        i++;
        if (get_cursor_col() == 0 && c == ' ') continue;
        if (conversation.last_col >= 0x0F && c == '\n') {
            conversation.last_col = 0;
            continue;
        }
        if (c != '\n') conversation.last_col = get_cursor_col();
        put_char(c);
    }
    conversation.word_len = 0;
    select_font(0);
}

/* Up to 16 characters; a space (0xA0) or newline (0x8A) ends the word: a new line first if it
 * wouldn't fit (column + length >= 18), then it is printed. A 17th character is dropped. */
void Conversation::talk_buffer_char(uint8_t c)
{
    if (conversation.word_len != 0x10) {
        conversation.word_buf[conversation.word_len++] = c;
        if (c != 0x8A && c != 0xA0) return;
        if (get_cursor_col() + conversation.word_len >= 0x12) put_char('\n');
    }
    talk_flush_word();
}

/* Three digit operands (ASCII with bit 7); beggars (0x6C-0x6F) give karma for alms. */
int Conversation::talk_op_pay_gold(void)
{
    int amount = (conversation.operands[0] & 0x7F) * 100 + (conversation.operands[1] & 0x7F) * 10 +
                 (conversation.operands[2] & 0x7F) - 0x14D0;
    if ((int16_t)party.gold < amount) {
        conversation.word_len = 0;
        put_char('"');
        print_text(strings[Strings::Thou_hast_not_enough_9328]);   /* "Thou hast not enough gold!" */
        put_char('"');
        print_text(strings[Strings::NL_NL_9344]);
        conversation.pending_op = 0;
        conversation.operand_count = 0;
        return talk_main_loop();
    }
    party.gold = (uint16_t)(party.gold - amount);
    draw_status_bar();
    if ((map.objects[npcs.state[conversation.npc].object].tile & 0xFC) == 0x6C && conversation.alms_counter > 99) {
        conversation.alms_counter = 0;
        add_byte_capped(99, 1, &party.karma);
        if (party.gold == 0) add_byte_capped(99, 2, &party.karma);
    }
    return 0;
}

/* < 0x40 inventory, 0x41 food, 0x42 gold, 0x43 keys, 0x44 gems, 0x45 torches, 0x46 grapples,
 * 0x47 magic carpets, 0x48-0x4A quest items, 0x4B odd keys. */
void Conversation::talk_op_give_item(uint8_t item)
{
    uint8_t *p = NULL;
    if (item < 0x40) {
        p = item < 0x30 ? &party.inventory[item] : &party.mixed_spells[item - 0x30];   /* the tables run on in the original */
    } else {
        switch (item) {
        case 0x41: add_word_capped(9999, 1, &party.food); draw_status_bar(); return;
        case 0x42: add_word_capped(9999, 1, &party.gold); draw_status_bar(); return;
        case 0x43: p = &party.keys; break;
        case 0x44: p = &party.gems; break;
        case 0x45: p = &party.torches; break;
        case 0x46: p = &party.grapples; break;
        case 0x47: p = &party.magic_carpets; break;
        case 0x48: party.sextant = 0xFF; return;
        case 0x49: party.spyglass = 0xFF; return;
        case 0x4A: party.black_badge = 0xFF; return;
        case 0x4B: p = &party.odd_keys; break;
        default: return;
        }
    }
    add_byte_capped(99, 1, p);
}

/* Advances past the next match byte (1) or stop byte (0). Stops at the end of the buffer too
 * (the original would run on). */
int Conversation::talk_skip_to(uint8_t stop, uint8_t match)
{
    for (;;) {
        if (conversation.ptr >= 0x400) return 0;
        uint8_t b = TP;
        conversation.ptr++;
        if (b == match) return 1;
        if (b == stop) return 0;
    }
}

void Conversation::talk_seek_string(int index)
{
    conversation.ptr = 0;
    for (; index != 0; index--) talk_skip_to(0x90, 0);
}

int Conversation::talk_run_string(void)
{
    while (TP != 0) {
        uint8_t op = TP;
        conversation.ptr++;
        if (talk_exec_op(op)) return 1;
        if (conversation.ptr >= 0x400) return 0;
    }
    return 0;
}

int Conversation::talk_say_string(int index)
{
    talk_seek_string(index & 0xFF);
    return talk_run_string();
}

/* "Same answer as the next keyword": skip to the next string (the keyword), past it, run its answer. */
int Conversation::talk_op_or(void)
{
    uint16_t p = (uint16_t)(conversation.ptr + 1);
    while (*text_buffer.at(p) == 0 && p < 0x400) p++;
    do {
        p++;
    } while (*text_buffer.at(p - 1) != 0 && p < 0x400);
    conversation.ptr = p;
    return talk_run_string();
}

void Conversation::talk_op_avatar_name(void)
{
    for (const char *n = party[0].name; *n; n++) talk_exec_op((uint8_t)(*n | 0x80));
}

/* The NPC joins if a party member (1-15) has the same first three letters as its name: that
 * member is swapped into the next party slot. */
int Conversation::talk_op_join_party(void)
{
    uint16_t saved = conversation.ptr;
    if (party.full()) {
        get_command_key();
        print_text(strings[Strings::Thou_hast_no_room_for_9348]);   /* "Thou hast no room for me in thy party! " */
        print_text(strings[Strings::Seek_me_again_if_one_9372]);   /* "Seek me again if one of thy members doth leave thee." */
        return 0;
    }
    char npc_name[4], member[4];
    talk_seek_string(0);
    memcpy(npc_name, script_str(), 3);
    npc_name[3] = 0;
    conversation.ptr += 3;
    for (int i = 15; i > 0; i--) {
        memcpy(member, party[i].name, 3);
        member[3] = 0;
        if (str_equal_nocase(member, npc_name)) {
            party.swap_in((unsigned)i);
            town.mark_npc_dead(conversation.npc);
            town.remove_npc(conversation.npc);
            draw_status_bar();
            talk_flush_word();
            conversation.ptr = saved;
            return 1;
        }
    }
    talk_buffer_char(0x22);
    talk_buffer_char('\n');
    print_text(strings[Strings::System_Error_No_Match_93A8]);   /* "System Error - No Match!" */
    conversation.ptr = saved;
    return 1;
}

/* Label sections follow the keywords, each after a 0x90 byte; 0x9F ends them. */
void Conversation::seek_label(void)
{
    conversation.ptr = 0;
    while (TP != conversation.label && conversation.ptr < 0x400) talk_skip_to(0x9F, 0x90);
}

int Conversation::talk_goto_label(void)
{
    seek_label();
    conversation.ptr++;
    return talk_run_string();
}

int Conversation::talk_run_next_string(void)
{
    talk_skip_to(0x90, 0);
    return talk_run_string();
}

int Conversation::talk_goto_label_default(void)
{
    seek_label();
    talk_skip_to(0x9F, 0);
    return talk_run_string();
}

int Conversation::talk_seek_keyword(int index)
{
    conversation.ptr = 0;
    for (int n = index * 2 + 5; n != 0; n--)
        if (!talk_skip_to(0x90, 0)) return 0;
    return 1;
}

int Conversation::talk_match_keyword(void)
{
    for (conversation.keyword_index = 0;; conversation.keyword_index++) {
        if (!talk_seek_keyword(conversation.keyword_index)) return 0;
        if (find_word(script_str())) return 1;
        conversation.ptr++;
    }
}

void Conversation::talk_read_input(void)
{
    talk_flush_word();
    read_line(0x0F, conversation.input);
}

int Conversation::talk_say_bye(void)
{
    talk_emit_quote();
    if (talk_say_string(4) == 0) {
        talk_emit_quote();
        talk_emit_newline();
    }
    return 1;
}

void Conversation::rebuke_pause(void)
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
int Conversation::talk_builtin_keyword(uint8_t index)
{
    int r;
    if (index < 5 && conversation.question_pending) return 2;
    if (index == 0) {
        print_text(strings[Strings::My_name_is_93C2]);   /* "\"My name is " */
        r = talk_say_string(0);
    } else if (index < 3) {
        talk_emit_quote();
        r = talk_say_string(3);
    } else if (index < 5) {
        return talk_say_bye();
    } else {
        print_text(strings[Strings::With_language_like_that_93D0]);   /* "With language like that, how did you become an Avatar?" */
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
int Conversation::check_builtins(void)
{
    int r = -1;
    for (uint8_t i = 0; i < 0x22; i++) {
        if (!find_word(builtin_keyword_text(i))) continue;
        r = talk_builtin_keyword(i);
        if (r == 0 || r == 1) return r;
    }
    return r;
}

int Conversation::talk_main_loop(void)
{
    for (;;) {
        conversation.question_pending = 0;
        print_text(strings[Strings::Your_interest_9408]);   /* "Your interest?\n:" */
        talk_read_input();
        if (conversation.input[0] == 0 || platform->quit_requested()) {
            print_text(strings[Strings::BYE_941A]);   /* "BYE" */
            return talk_say_bye();
        }
        talk_emit_newline();
        talk_emit_newline();
        int r = check_builtins();
        if (r == 1) return 1;
        if (r == 0) continue;
        if (!talk_match_keyword()) {
            print_text(strings[Strings::I_cannot_help_thee_with_9420]);   /* "\"I cannot help thee with that." */
        } else {
            talk_emit_quote();
            if (talk_say_string(conversation.keyword_index * 2 + 6)) return 1;
        }
        talk_emit_quote();
        talk_emit_newline();
        talk_emit_newline();
    }
}

/* After the label: the question, the default reply, then (answer, reply) pairs up to 0x90. */
int Conversation::talk_match_label_answer(void)
{
    seek_label();
    talk_skip_to(0x9F, 0);
    talk_skip_to(0x9F, 0);
    do {
        if (find_word(script_str())) return 1;
    } while (talk_skip_to(0x90, 0) && talk_skip_to(0x90, 0));
    return 0;
}

int Conversation::talk_ask_question(void)
{
    int r;
    do {
        talk_emit_quote();
        if (talk_goto_label()) return 1;
        talk_emit_quote();
        do {
            talk_emit_newline();
            talk_emit_newline();
            conversation.question_pending = 0xFF;
            print_text(strings[Strings::You_respond_9440]);   /* "You respond-\n:" */
            talk_read_input();
            if (platform->quit_requested()) return 1;
            if (conversation.input[0] == 0) print_text(strings[Strings::What_didst_thou_say_9450]);   /* "What didst thou say?" */
        } while (conversation.input[0] == 0);
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

void Conversation::talk_mark_met(int npc) { conversation.met_bits[map.id()] |= (uint32_t)1 << (npc & 31); }
int Conversation::talk_has_met(int npc) { return (conversation.met_bits[map.id()] >> (npc & 31)) & 1; }

void Conversation::clear_pending(void)
{
    conversation.operand_count = 0;
    conversation.pending_op = 0;
}

/* Operands of 0x85 (3 digits), 0x86 (item), 0x8C (label if met; 0xFF = main loop) and 0xFE
 * (karma, label: ask the question if karma >= the first operand). */
int Conversation::talk_op_operands(uint8_t b)
{
    switch (conversation.pending_op) {
    case 0x85:
        conversation.operands[conversation.operand_count++] = b;
        if (conversation.operand_count != 3) return 0;
        if (talk_op_pay_gold()) return 1;
        break;
    case 0x86:
        talk_op_give_item(b & 0x7F);
        break;
    case 0x8C:
        clear_pending();
        if (!talk_has_met(conversation.npc)) return 0;
        if (b == 0xFF) return talk_main_loop();
        conversation.label = b;
        return talk_ask_question();
    case 0xFE:
        conversation.operands[conversation.operand_count++] = b;
        if (conversation.operand_count != 2) return 0;
        clear_pending();
        if (party.karma < conversation.operands[0]) return 0;
        conversation.label = conversation.operands[1];
        return talk_ask_question();
    }
    clear_pending();
    return 0;
}

/* Matches the first four letters of each party member's name: the NPC now knows the party. */
void Conversation::talk_op_ask_name(void)
{
    talk_emit_quote();
    talk_flush_word();
    print_text(strings[Strings::What_is_thy_name_9468]);   /* "What is thy name?\"" */
    print_text(strings[Strings::You_respond_947C]);   /* "You respond-" */
    talk_read_input();
    clear_pending();
    if (conversation.input[0] != 0) {
        for (unsigned i = 0; i < party.size(); i++) {
            char name[5];
            memcpy(name, party[i].name, 4);
            name[4] = 0;
            if (find_word(name)) {
                talk_mark_met(conversation.npc);
                print_text(strings[Strings::A_pleasure_94A0]);   /* "A pleasure!" */
                return;
            }
        }
        print_text(strings[Strings::If_you_say_so_94B0]);   /* "If you say so..." */
        return;
    }
    print_text(strings[Strings::If_you_say_so_948C]);
}

/* One script byte. Returns 1 if the conversation has ended. */
int Conversation::talk_exec_op(uint8_t op)
{
    uint16_t resume = conversation.ptr;   /* most ops leave the pointer where it was */
    if (conversation.pending_op != 0) return talk_op_operands(op);
    conversation.operand_count = 0;
    if (op == 0xA2 && conversation.last_op == 0xA2) return 0;   /* no doubled quotes */
    switch (op) {
    case 0x81: talk_op_avatar_name(); resume = conversation.ptr; break;
    case 0x82: return 1;                                    /* end */
    case 0x83: rebuke_pause(); resume = conversation.ptr; break; /* pause */
    case 0x84: return talk_op_join_party();
    case 0x85: case 0x86: case 0x8C: case 0xFE: conversation.pending_op = op; break;
    case 0x87:
        if (talk_op_or()) return 1;
        break;
    case 0x88: talk_op_ask_name(); resume = conversation.ptr; break;
    case 0x89: add_byte_capped(99, 1, &party.karma); return 0;
    case 0x8A: sub_byte_floor0(1, &party.karma); return 0;
    case 0x8B: town.alert_town(); return 0;
    case 0x8E: conversation.font_mask ^= 0x80; return 0;
    case 0x8F: get_command_key(); return 0;
    case 0xFF:
        talk_flush_word();
        return talk_main_loop();
    default:
        if (op > 0x90 && op < 0xA0) {   /* a label inside text: ask its question */
            conversation.label = op;
            return talk_ask_question();
        }
        if (op < 0x81) {   /* a word from the table, with spaces around it */
            const char *w = word(op);
            talk_buffer_char(0xA0);
            for (const char *p = w; *p; p++) talk_buffer_char((uint8_t)(*p | 0x80));
            if (*w == 0) talk_buffer_char(op);
            else conversation.need_space = 1;
        } else {   /* a character; 0x8D = new line */
            op |= 0x80;
            if (op == 0x8D) op = 0x8A;
            uint8_t c = op & conversation.font_mask;
            conversation.last_op = c;
            if (conversation.need_space) talk_buffer_char(0xA0);
            talk_buffer_char(c);
            conversation.need_space = 0;
        }
        resume = conversation.ptr;
        break;
    }
    conversation.ptr = resume;
    return 0;
}

/* "You see <description>", then the greeting if the NPC knows the party, else half the time
 * "I am called <name>". */
int Conversation::talk_greeting(void)
{
    print_text(strings[Strings::You_see_94C4]);   /* "You see " */
    if (talk_say_string(1)) return 1;
    talk_emit_newline();
    talk_emit_newline();
    int r;
    if (!talk_has_met(conversation.npc)) {
        set_random_seed((uint16_t)time_seed());
        if (random_between(1, 0) == 0) return 0;
        print_text(strings[Strings::I_am_called_94CE]);   /* "\"I am called " */
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
void Conversation::talk_maybe_steal(void)
{
    if (town.shadowlord_here != 0) return;
    print_text(strings[Strings::Something_was_stolen_94DC]);   /* "Something was stolen!" */
    sound_slide(0x32, 1, 2000, 800);
    set_random_seed((uint16_t)time_seed());
    if (party.keys == 0 && party.gems == 0 && party.torches == 0) {
        for (int i = 0x2F; i >= 0; i--)
            if (party.inventory[i]) { sub_byte_floor0(1, &party.inventory[i]); return; }
        for (int i = 7; i >= 0; i--)
            if (party.potions[i]) { sub_byte_floor0(1, &party.potions[i]); return; }
        for (int i = 7; i >= 0; i--)
            if (party.scrolls[i]) { sub_byte_floor0(1, &party.scrolls[i]); return; }
        sub_word_floor0((uint16_t)random_between(0x0F, 1), &party.gold);
        draw_status_bar();
        return;
    }
    for (;;) {
        int r = random_between(2, 0);
        uint8_t *p = r == 0 ? &party.keys : r == 1 ? &party.gems : &party.torches;
        if (*p) { sub_byte_floor0(1, p); return; }
    }
}

/* The .TLK index (count, then (script, offset) pairs) is read into text_buffer, then the script. */
void Conversation::talk_run_conversation(int script)
{
    const char *file = map.tlk_file();
    load_file_part(0, 0x200, text_buffer.data, file);
    unsigned count = text_buffer.word(0), entry = 0;
    for (unsigned i = 1; i <= count && i < 0x80; i++, entry++)
        if (text_buffer.word(2 + entry * 4) == script) break;
    uint16_t offset = text_buffer.word(2 + entry * 4 + 2);
    memset(text_buffer.data, 0, 0x400);
    load_file_part(offset, 0x400, text_buffer.data, file);   /* may be short at the end of the file */
    conversation.word_len = 0;
    conversation.need_space = 0;
    if (talk_greeting() == 0 && talk_main_loop() == 0) talk_say_bye();
    talk_maybe_steal();
}

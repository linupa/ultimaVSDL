/* status.h - the party panel on the right (rows 1-9 of text window 1) and the panel title bars.
 * Main program 0000:2726-0000:2e8e, 0000:39fc, 0000:400c, 0000:4c2a-0000:4f3c. */
#ifndef U5_STATUS_H
#define U5_STATUS_H

#include <stdint.h>

/* The party panel. `status` is the one Status object; the functions below the class are what the
 * game calls, and forward to it. */
class Status {
public:
    static Status &instance();

    /* Set when something shown on the panel changed (hit points, food, gold, a spell): the main
     * loops redraw the panel before the next key (refresh). (The original's byte, ds:A9FA.) */
    bool dirty = false;

    /* Redraws the panel if it is dirty. */
    void refresh(void)
    {
        if (dirty) {
            draw_status_bar();
            dirty = false;
        }
    }

    int  check_party_state(void);              /* 39fc: -1 all dead, 1 all asleep, 0 ok. (The original also leaves the first able
                                                * member in dir_x; that is Party::first_able now.) */
    void draw_member_status_row(unsigned member);   /* 2726 */
    void draw_gold(void);                      /* 2884 */
    void draw_status_bar(void);                /* 2900: party rows, food, gold or hull, date, active spell */
    void highlight_member_row(int member);     /* 2a28: XOR bar over the row */
    int  select_member(int allow_all);         /* 2d7a: index, -1 none, -2 all (allow_all) */
    int  ask_member_number(void);              /* 2e8e */
    void regen_ring_tick(void);                /* 400c */
    void draw_label_left(void);                /* 4c2a */
    void draw_label_right(void);               /* 4cce */
    void draw_panel_top_bar(void);             /* 4e20 */
    void draw_panel_title(const char *title);  /* 4e50 */
    void clear_side_panel(void);               /* 4efc */
    void draw_panel_bottom_bar(void);          /* 4f3c */
    void draw_list_header_bar(void);            /* 4daa */
    void draw_list_marker(uint8_t c);          /* 4dea */

    Status(const Status &) = delete;
    Status &operator=(const Status &) = delete;

private:
    Status() = default;

    int is_combat_turn_of(unsigned member);
    void label_cap(uint8_t glyph, int left);
};

/* The one Status object (Status::instance()). */
extern Status &status;

inline int check_party_state(void) { return status.check_party_state(); }
inline void draw_member_status_row(unsigned member) { status.draw_member_status_row(member); }
inline void draw_gold(void) { status.draw_gold(); }
inline void draw_status_bar(void) { status.draw_status_bar(); }
inline void highlight_member_row(int member) { status.highlight_member_row(member); }
inline int select_member(int allow_all) { return status.select_member(allow_all); }
inline int ask_member_number(void) { return status.ask_member_number(); }
inline void regen_ring_tick(void) { status.regen_ring_tick(); }
inline void draw_label_left(void) { status.draw_label_left(); }
inline void draw_label_right(void) { status.draw_label_right(); }
inline void draw_panel_top_bar(void) { status.draw_panel_top_bar(); }
inline void draw_panel_title(const char *title) { status.draw_panel_title(title); }
inline void clear_side_panel(void) { status.clear_side_panel(); }
inline void draw_panel_bottom_bar(void) { status.draw_panel_bottom_bar(); }
inline void draw_list_header_bar(void) { status.draw_list_header_bar(); }
inline void draw_list_marker(uint8_t c) { status.draw_list_marker(c); }

#endif

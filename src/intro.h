/* intro.h - INTRO.OVL: title sequence, main menu, the "Ultima V Introduction" story.
 * including Transfer from Ultima IV. */
#ifndef U5_INTRO_H
#define U5_INTRO_H
#include "strings.h"

/* Everything before the game proper: the title sequence and menu, the introduction's story, the
 * demo, character creation and the transfer from Ultima IV (INTRO.OVL in intro.cpp, FONT.OVL in
 * story.cpp). `intro` is the one Intro object. None of its state is part of a saved game. */
class Intro {
public:
    static Intro &instance();

    uint16_t title_picture = 0;      /* the logo picture (show_title_screen) */
    /* Lord British's signature, drawn stroke by stroke on the title screen: BRITISH.PTH, and
     * how far intro_trace_path has got in it. The buffer is as long as a saved game, which is
     * where the original reads the file to; the file itself is shorter. */
    enum { PATH_SIZE = 0x1060 };
    uint8_t signature_path[PATH_SIZE] = {};
    uint16_t path_pos = 0;

    /* Story text (draw_story_text): the cursor in pixels, and two pairs of margins. Lines whose y
     * lies between story_line_min and story_line_max use pair 1, the text beside a picture. The
     * endings draw their pages with these too. */
    uint16_t story_x = 0, story_y = 0x3C;
    uint16_t story_left[2] = {0, 0}, story_right[2] = {0x140, 0x140};
    uint16_t story_line_min = 0xC8, story_line_max = 0xC8;
    uint16_t story_space_width = 5;  /* pixels for a space */

    /* The demo ("Return to the View"). */
    uint8_t demo_frame = 0;
    uint8_t demo_caption_first = 0, demo_caption_last = 0, demo_caption_blink = 0, demo_caption_mode = 0;

    /* Character creation: the virtues asked in this round and those already beaten, and the
     * attributes the answers add up to. */
    uint8_t virtue_asked[8] = {}, virtue_beaten[8] = {};
    uint8_t new_int = 0, new_dex = 0, new_str = 0;

    /* The transfer from Ultima IV: the first record of PARTY.SAV. */
    uint8_t u4_character[40] = {};
    uint16_t u4_is_avatar = 0;

    /* ---- title, menu, story, transfer (intro.cpp) ---- */

    void draw_menu_banner(void);                 /* 81d0 */
    int  intro_trace_path(int y, int x);         /* 8210: draws part of Lord British's signature */
    void intro_story(void);                      /* 830e */
    void print_centered(const char *text);       /* 85fe: label on the bottom frame line */
    void draw_menu_border(void);                 /* 86a0 */
    void show_title_screen(unsigned quick);      /* 8770 */
    void draw_menu_item(const char *text, int col, int row, int selected);   /* 8836 */
    void draw_main_menu(int selected);           /* 887c */
    void menu_scroll_effect(void);               /* 88ee: Acknowledgements scroll */
    int  wait_key_or_ticks(int ticks);           /* 8b0e */

    /* 8b46. Returns 1 when "Journey Onward" has loaded the saved game (the original then returns to
     * main), 0 if the window was closed. skip_title: start at the menu, as when a key is held. */
    int  intro_main(int skip_title);
    int  read_ultima4_party(void);               /* 91d6: 1 = bad data */
    void print_ultima4_class(void);              /* 9438 */
    int  convert_ultima4_stat(int stat);         /* 94aa */
    void transfer_from_ultima4(void);            /* 94ea */
    void print_transfer_label(int row);          /* 9fe2 */
    void draw_transfer_frame(void);              /* a022 */
    void draw_transfer_panel(int x);             /* a0e6 */
    void draw_transfer_screen(void);             /* a1e4 */

    /* ---- story text, demo, character creation (story.cpp; see story.h) ---- */

    /* Draws text with the proportional font (PROPORT.PCS) into the back page at story_x / story_y,
     * word-wrapped and justified between two pairs of margins: lines whose y lies between
     * story_line_min and story_line_max use the second pair (text beside a picture). In the text,
     * '_' marks where a word may be hyphenated and '{' leaves a 15-pixel gap; a line is 9 pixels. */
    void draw_story_text(const uint8_t *text, uint16_t font);   /* e1e0 */

    void demo_draw_cell(int row, int col);     /* e482 */
    int  demo_step(int frames);                /* e4dc: 1 if a key was pressed */
    void demo_caption(int n);                  /* e5f8 */
    void attract_demo(void);                   /* e684 */

    int  random_virtue(void);                  /* eb78 */
    void ask_virtue_question(uint16_t picture, uint16_t font);   /* eba8 */
    void create_character(void);               /* ecea: writes SAVED.GAM and SAVED.OOL */

    /* Port helper: wait for a key (the original spins on poll_key). Returns it, or 0 on quit. */
    int  wait_any_key(void);

    /* A page of the introduction's story: the text's two pairs of margins, where it is in
     * STORY.DAT, the lines that use the second pair, where the text starts, and the picture:
     * its frame, which file (set), its place and what else is drawn (kind). */
    struct StoryPage {
        uint8_t left[2];
        uint16_t right[2];
        uint16_t text;
        uint8_t line_min, line_max, x, y, frame, set, px, py, kind;
    };

    /* Puts pages together from the original's field-by-field tables at b: `words` entries of
     * the left pairs, the right pairs and the text offsets, then `bytes` entries of each byte
     * field. (Endings::Page has the same layout.) */
    template <class P> static void read_pages(P *pages, unsigned count, const uint8_t *b, unsigned words, unsigned bytes)
    {
        const uint8_t *right = b + 2 * words, *text = right + 4 * words, *f = text + 2 * words;
        for (unsigned i = 0; i < count; i++) {
            P &p = pages[i];
            p.left[0] = b[2 * i];
            p.left[1] = b[2 * i + 1];
            p.right[0] = (uint16_t)(right[4 * i] | right[4 * i + 1] << 8);
            p.right[1] = (uint16_t)(right[4 * i + 2] | right[4 * i + 3] << 8);
            p.text = (uint16_t)(text[2 * i] | text[2 * i + 1] << 8);
            uint8_t *field[9] = {&p.line_min, &p.line_max, &p.x, &p.y, &p.frame, &p.set, &p.px, &p.py, last_field(p)};
            for (int k = 0; k < 9; k++) *field[k] = f[k * bytes + i];
        }
    }
    static uint8_t *last_field(StoryPage &p) { return &p.kind; }
    template <class P> static uint8_t *last_field(P &p) { return &p.chapter; }

    Intro(const Intro &) = delete;
    Intro &operator=(const Intro &) = delete;

private:
    Intro();

    static StoryPage story_pages_[22];
    static int16_t transfer_label_rows_[8];
    static Strings::Id transfer_labels_[8], demo_captions_[4];
    static uint8_t glyph_widths_[128], virtue_card_x_[8], virtue_card_y_[8];
    static int8_t virtue_int_[8], virtue_dex_[8], virtue_str_[8];
    static uint16_t question_offsets_[8][8];
    static int glyph_w(uint8_t c);

    int title_sequence(void);
    int transfer_yes_no(void);
    void transfer_mark(int row, int col, Strings::Id text, int on);
    int journey_onward(void);
    void back_to_title(void);
};

extern Intro &intro;

#endif

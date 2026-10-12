/* gametime.h - the game's clock and calendar, as the Time class. `game_time` is the one Time
 * object. (The file is not called time.h, and the object not `time`, because the C library has
 * both.)
 *
 * A day has 24 hours, a month 28 days, a year 13 months; pass_time (map.h) moves the clock on.
 * With it are the things that go by the date: the moons, and when something was last done.
 * All of it is part of the saved game: Time registers each piece with SaveGame (savegame.h),
 * which puts it at its old place in SAVED.GAM; the ds offsets are given below. */
#ifndef U5_GAMETIME_H
#define U5_GAMETIME_H

#include <stdint.h>

class Time {
public:
    static Time &instance();

    /* The clock and the date shown on the status panel. */
    uint8_t minute = 0;              /* ds:5881 */
    uint8_t hour = 0;                /* ds:587F */
    uint8_t prev_hour = 0;           /* ds:5880  the hour before the last pass_time: has the sky moved? */
    uint8_t day = 0;                 /* ds:587E  1-28 */
    uint8_t month = 0;               /* ds:587D  1-13 */
    uint16_t year = 0;               /* ds:5874 */

    /* The moons: the glyph of each one's phase today, which is also the moongate's destination
     * ('0'-'7'), and how far the moongates have risen (0-16). */
    uint8_t moon1_phase = 0;         /* ds:5885  Trammel */
    uint8_t moon2_phase = 0;         /* ds:5886  Felucca */
    uint8_t moongate_phase = 0;      /* ds:5887 */

    /* When things were last done. */
    uint8_t rest_cooldown = 0;       /* ds:588C  hours until camping restores again (set to 14) */
    uint8_t reagent_spot_day[3] = {};/* ds:5858  the day each reagent spot was last picked */
    uint8_t hidden_item_day = 0;     /* ds:57B2  the day hidden item 14 was last taken */
    uint8_t apparition_month = 0;    /* ds:588D  the month Lord British last appeared to a resting party (only written) */

    /* A new game's grace period: while set, a monster group's size is rolled twice, the second
     * roll under the first, so groups come out smaller (Combat::place_monster_group). INIT.GAM starts it at 1
     * and the first change of month clears it for good; nothing sets it again. */
    uint8_t no_ambush = 0;           /* ds:5959 */

    /* Trammel's (moon 0) and Felucca's (moon 1) phase on day 1-28, as a glyph '0'-'7' (ds:1ED8 +
     * day * 2, from DATA.OVL). */
    uint8_t moon_phase(unsigned day, int moon) const { return moon_phases_[day < 29 ? day : 28][moon & 1]; }

    Time(const Time &) = delete;
    Time &operator=(const Time &) = delete;

private:
    Time();

    uint8_t moon_phases_[29][2] = {};   /* [0] is not a day: the bytes before the table */
};

/* The one Time object (Time::instance()). */
extern Time &game_time;

#endif

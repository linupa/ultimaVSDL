/* gametime.cpp - the game's clock and calendar. See gametime.h. */
#include "gamedata.h"
#include "gametime.h"
#include "savegame.h"

Time::Time()
{
    SaveGame &save = SaveGame::instance();   /* not `savegame`: this runs before main, in no set order */
    save.add(0x57B2, hidden_item_day, "time.hidden_item_day");
    save.add(0x5858, reagent_spot_day, "time.reagent_spot_day");
    save.add(0x5874, year, "time.year");
    save.add(0x587D, month, "time.month");
    save.add(0x587E, day, "time.day");
    save.add(0x587F, hour, "time.hour");
    save.add(0x5880, prev_hour, "time.prev_hour");
    save.add(0x5881, minute, "time.minute");
    save.add(0x5885, moon1_phase, "time.moon1_phase");
    save.add(0x5886, moon2_phase, "time.moon2_phase");
    save.add(0x5887, moongate_phase, "time.moongate_phase");
    save.add(0x588C, rest_cooldown, "time.rest_cooldown");
    save.add(0x588D, apparition_month, "time.apparition_month");
    Data::instance().add(0x1ED8, moon_phases_, "time.moon_phases", Data::SHARED);   /* from DATA.OVL; day 0 is two bytes of Map's exit_y */
    save.add(0x5959, no_ambush, "time.no_ambush");
}

Time &Time::instance()
{
    static Time the_time;
    return the_time;
}

Time &game_time = Time::instance();

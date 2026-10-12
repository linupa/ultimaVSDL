/* spells.cpp - the spell tables. See spells.h. */
#include "gamedata.h"
#include "spells.h"

Spells &Spells::instance()
{
    static Spells the_spells;
    return the_spells;
}

Spells &spells = Spells::instance();

Spells::Spells()
{
    Data &data = Data::instance();   /* not a global: this runs before main, in no set order */
    data.add(0x19D2, reagent_names_, "spells.reagent_names");
    data.add(0x19E2, names_, "spells.names");
    data.add(0x1BFC, syllables_, "spells.syllables");
    data.add(0x1C30, letters_, "spells.letters");
    data.add(0x1C90, where_, "spells.where");
    data.add(0x1CC0, recipes_, "spells.recipes");
    data.add(0x1CF0, cone_steps_, "spells.cone_steps");
}

Strings::Id Spells::name_of(int spell) { return spells.name_id((unsigned)spell); }
Strings::Id Spells::reagent_name_of(int reagent) { return spells.reagent_name_id((unsigned)reagent); }

uint8_t Spells::where_[Spells::COUNT];   /* ds:1C90 */

uint8_t Spells::recipes_[Spells::COUNT];   /* ds:1CC0 */

Strings::Id Spells::names_[Spells::COUNT];   /* ds:19E2 */

Strings::Id Spells::letters_[Spells::COUNT];   /* ds:1C30 */

Strings::Id Spells::syllables_[26];   /* ds:1BFC */

Strings::Id Spells::reagent_names_[Spells::REAGENTS];   /* ds:19D2 */

int16_t Spells::cone_steps_[Spells::CONE_RAYS];   /* ds:1CF0 */

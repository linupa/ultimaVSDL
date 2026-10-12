/* gamedata.cpp - reading DATA.OVL and handing its parts to their owners. See gamedata.h. */
#include <stdio.h>
#include <stdlib.h>
#include "files.h"
#include "gamedata.h"

Data &Data::instance()
{
    static Data the_data;
    return the_data;
}

/* A mistake here would quietly give a class the wrong table, so it stops the program instead. */
void Data::add(unsigned ds_offset, unsigned size, Fill fill, const char *name, Sharing sharing)
{
    const char *problem = NULL, *other = "";
    if (loaded_) problem = "comes after DATA.OVL was loaded";
    else if (size == 0 || ds_offset + size > SIZE) problem = "is outside DATA.OVL";
    else if (count_ == MAX_PIECES) problem = "is one piece too many";
    for (int i = 0; i < count_ && !problem; i++)
        if (ds_offset < pieces_[i].offset + pieces_[i].size && pieces_[i].offset < ds_offset + size &&
            sharing == OWN && pieces_[i].sharing == OWN) {
            problem = "overlaps ";
            other = pieces_[i].name;
        }
    if (problem) {
        fprintf(stderr, "Data: %s (ds:%04X, %u bytes) %s%s\n", name, ds_offset, size, problem, other);
        abort();
    }
    pieces_[count_++] = Piece{ds_offset, size, fill, name, sharing};
}

int Data::load(const char *file)
{
    size_t size = 0;
    unsigned char *buf = game_load_file(file, &size);
    if (!buf) {
        fprintf(stderr, "Data: cannot read %s from the game directory\n", file);
        return 0;
    }
    if (size != HEADER + SIZE) {
        fprintf(stderr, "Data: %s is %zu bytes, not %u: not Ultima V's\n", file, size, (unsigned)(HEADER + SIZE));
        free(buf);
        return 0;
    }
    for (int i = 0; i < count_; i++) pieces_[i].fill(buf + HEADER + pieces_[i].offset, pieces_[i].size);
    free(buf);
    loaded_ = 1;
    return 1;
}

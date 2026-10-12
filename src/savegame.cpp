/* savegame.cpp - reading and writing saved games in the original's format. See savegame.h. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "savegame.h"

SaveGame::SaveGame()
{
    add(0x55A6, header, "savegame.header");
}

SaveGame &SaveGame::instance()
{
    static SaveGame the_savegame;
    return the_savegame;
}

SaveGame &savegame = SaveGame::instance();

/* A mistake here would quietly corrupt saved games, so it stops the program instead. */
void SaveGame::add(unsigned ds_offset, void *data, unsigned size, const char *name)
{
    unsigned offset = ds_offset - DS_OFFSET;
    const char *problem = NULL, *other = "";
    if (ds_offset < DS_OFFSET || size == 0 || offset + size > SIZE) problem = "is outside the saved range";
    else if (count_ == MAX_PIECES) problem = "is one piece too many";
    for (int i = 0; i < count_ && !problem; i++)
        if (offset < pieces_[i].offset + pieces_[i].size && pieces_[i].offset < offset + size) {
            problem = "overlaps ";
            other = pieces_[i].name;
        }
    if (problem) {
        fprintf(stderr, "SaveGame: %s (ds:%04X, %u bytes) %s%s\n", name, ds_offset, size, problem, other);
        abort();
    }
    pieces_[count_++] = Piece{offset, size, data, name};
}

void SaveGame::pack(uint8_t *image) const
{
    memcpy(image, rest_, SIZE);
    for (int i = 0; i < count_; i++) memcpy(image + pieces_[i].offset, pieces_[i].data, pieces_[i].size);
}

void SaveGame::unpack(const uint8_t *image)
{
    memcpy(rest_, image, SIZE);
    for (int i = 0; i < count_; i++) memcpy(pieces_[i].data, image + pieces_[i].offset, pieces_[i].size);
}

int SaveGame::load_path(const char *path)
{
    uint8_t image[SIZE];
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(image, 1, SIZE, f);
    fclose(f);
    if (n != SIZE) return -1;
    unpack(image);
    return 0;
}

int SaveGame::save_path(const char *path) const
{
    uint8_t image[SIZE];
    pack(image);
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t n = fwrite(image, 1, SIZE, f);
    fclose(f);
    return n == SIZE ? 0 : -1;
}

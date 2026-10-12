/* savegame.h - reading and writing saved games in the original's format.
 *
 * SAVED.GAM (and INIT.GAM, the new game) is 0x1060 bytes: a copy of the original's data segment
 * from ds:55A6 on, which it loads and saves as one block. Here SaveGame builds that block from
 * the classes that hold the state, and takes it apart again:
 *   - a class that keeps part of the saved state registers it with add(), saying which bytes
 *     of the block it stands for. Those bytes are then read from and written to the class;
 *   - the few bytes no class holds (the original's padding and unused slots, all 0 in the
 *     game's files) are kept by SaveGame as they were loaded, so a file comes back unchanged.
 * The registered pieces are stored as they lie in memory, which assumes a little-endian host
 * for the 16 and 32 bit ones. `savegame` is the one SaveGame object.
 *
 * A class registers its pieces in its constructor. Those run before main, in no set order, so
 * they must call SaveGame::instance() rather than use `savegame`, which may not be set yet. */
#ifndef U5_SAVEGAME_H
#define U5_SAVEGAME_H

#include <stdint.h>

class SaveGame {
public:
    enum { DS_OFFSET = 0x55A6, SIZE = 0x1060 };

    static SaveGame &instance();

    /* `size` bytes at `data` are the saved game's bytes from ds offset `ds_offset` on. `name` is
     * for error messages. Pieces must lie inside the block and not overlap. */
    void add(unsigned ds_offset, void *data, unsigned size, const char *name);
    template <class T> void add(unsigned ds_offset, T &field, const char *name) { add(ds_offset, &field, sizeof field, name); }

    /* The saved game as the SIZE bytes of the file, from the state / into the state. */
    void pack(uint8_t *image) const;
    void unpack(const uint8_t *image);

    /* The registered pieces, for checks: how many, and each one's place in the data segment. */
    int  count() const { return count_; }
    void piece(int i, unsigned *ds_offset, unsigned *size, const char **name) const
    {
        *ds_offset = pieces_[i].offset + DS_OFFSET;
        *size = pieces_[i].size;
        *name = pieces_[i].name;
    }

    /* For the tests: forgets the bytes no class holds (they become 0). */
    void clear_rest() { for (uint8_t &b : rest_) b = 0; }

    /* A saved game in a plain file, by path. 0 on success. (The game itself goes through its own
     * file layer: load_game_file / save_game_file in map.h.) */
    int load_path(const char *path);
    int save_path(const char *path) const;

    /* The file's first word (ds:55A6), which the game neither reads nor sets: kept as it is. */
    uint16_t header = 0;

    SaveGame(const SaveGame &) = delete;
    SaveGame &operator=(const SaveGame &) = delete;

private:
    SaveGame();

    struct Piece {
        unsigned offset, size;   /* offset within the block */
        void *data;
        const char *name;
    };
    enum { MAX_PIECES = 256 };
    Piece pieces_[MAX_PIECES];
    int count_ = 0;
    uint8_t rest_[SIZE] = {};   /* the block as last loaded, for the bytes no class holds */
};

extern SaveGame &savegame;

#endif

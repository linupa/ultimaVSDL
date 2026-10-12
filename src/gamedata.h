/* gamedata.h - the original game's data file, DATA.OVL, as the Data class. `Data::instance()` is
 * the one Data object.
 *
 * The port keeps none of the game's tables or text in its own source: each class that owns some
 * of them says, in its constructor, which part of DATA.OVL it needs and where to put it
 * (add()), and Data::load() reads the file once, early in main, and hands every owner its part.
 * Owners keep their own copies; Data keeps nothing after the load.
 *
 * Offsets are the original's data-segment offsets (ds:xxxx), i.e. the file offset less DATA.OVL's
 * 16-byte header - the same numbers the decompiled source and SaveGame use.
 *
 * Owners must call Data::instance(), not keep a global reference to it: their constructors run
 * before main in no set order, and instance() builds the object on its first call. */
#ifndef U5_GAMEDATA_H
#define U5_GAMEDATA_H

#include <stdint.h>
#include <functional>
#include <type_traits>

class Data {
public:
    static Data &instance();

    enum { SIZE = 0xBD40, HEADER = 16 };

    /* Two owners may not claim the same bytes, unless one of them says SHARED: a table the original
     * indexes past its end into its neighbours (the port copies those bytes too), or a run of
     * tables copied whole that has text in between. Copies are read-only, so sharing is safe;
     * the check is there to keep each table's owner clear. */
    enum Sharing { OWN, SHARED };

    /* The general form: `fill` gets the `size` bytes at ds:offset when the file is loaded. */
    using Fill = std::function<void(const uint8_t *bytes, unsigned size)>;
    void add(unsigned ds_offset, unsigned size, Fill fill, const char *name, Sharing sharing = OWN);

    /* An array or a single value, read as the original stores it: bytes as they are, 16-bit
     * values (and enums of that size, such as Strings::Id) little-endian. */
    template <class T, unsigned N> void add(unsigned ds_offset, T (&field)[N], const char *name, Sharing sharing = OWN)
    {
        add(ds_offset, N * (unsigned)sizeof(T), [&field](const uint8_t *b, unsigned) {
            for (unsigned i = 0; i < N; i++) field[i] = read<T>(b + i * sizeof(T));
        }, name, sharing);
    }
    template <class T, unsigned N, unsigned M> void add(unsigned ds_offset, T (&field)[N][M], const char *name, Sharing sharing = OWN)
    {
        add(ds_offset, N * M * (unsigned)sizeof(T), [&field](const uint8_t *b, unsigned) {
            for (unsigned i = 0; i < N; i++)
                for (unsigned j = 0; j < M; j++) field[i][j] = read<T>(b + (i * M + j) * sizeof(T));
        }, name, sharing);
    }
    template <class T, class = typename std::enable_if<std::is_integral<T>::value || std::is_enum<T>::value>::type>
    void add(unsigned ds_offset, T &field, const char *name, Sharing sharing = OWN)
    {
        add(ds_offset, (unsigned)sizeof(T), [&field](const uint8_t *b, unsigned) { field = read<T>(b); }, name, sharing);
    }

    /* A 16-bit value of the original's tables at b, little-endian; for owners' own fills. */
    static uint16_t word(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }

    /* Reads DATA.OVL from the game directory (files.h) and fills every owner. On failure says why
     * on stderr and returns 0. */
    int load(const char *file = "DATA.OVL");
    int loaded() const { return loaded_; }

    /* What has been claimed, for the tests. */
    int count() const { return count_; }
    void piece(int i, unsigned *ds_offset, unsigned *size, const char **name, Sharing *sharing = nullptr) const
    {
        if (sharing) *sharing = pieces_[i].sharing;
        *ds_offset = pieces_[i].offset;
        *size = pieces_[i].size;
        *name = pieces_[i].name;
    }

    Data(const Data &) = delete;
    Data &operator=(const Data &) = delete;

private:
    Data() = default;

    template <class T> static T read(const uint8_t *b)
    {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2, "DATA.OVL holds bytes and 16-bit words");
        if (sizeof(T) == 1) return (T)b[0];
        return (T)(uint16_t)(b[0] | b[1] << 8);
    }

    struct Piece {
        unsigned offset, size;
        Fill fill;
        const char *name;
        Sharing sharing;
    };
    enum { MAX_PIECES = 1024 };
    Piece pieces_[MAX_PIECES];
    int count_ = 0;
    int loaded_ = 0;
};

#endif

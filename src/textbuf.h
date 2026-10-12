/* textbuf.h - the text buffer: 2000 bytes of scratch that the game loads text into and reads it
 * from. `text_buffer` is the one TextBuffer object.
 *
 * It is one buffer in the original too, used in turn for a conversation's script (the first
 * 0x400 bytes), the shops' and the story's text, the messages of the set pieces, a line being put
 * together, the object files on saving - and, while the people of a town find their way, as the
 * 32 x 32 path grid. Offsets are from its start. */
#ifndef U5_TEXTBUF_H
#define U5_TEXTBUF_H

#include <stdint.h>

class TextBuffer {
public:
    enum { SIZE = 2000 };

    static TextBuffer &instance();

    uint8_t data[SIZE] = {};

    /* The byte at an offset. One outside the buffer gives a spare byte: the original reads and
     * writes whatever lies there. */
    uint8_t *at(long offset) { return offset >= 0 && offset < SIZE ? &data[offset] : &outside_; }
    /* The little-endian word at an offset. */
    uint16_t word(long offset) { return (uint16_t)(*at(offset) | *at(offset + 1) << 8); }

    TextBuffer(const TextBuffer &) = delete;
    TextBuffer &operator=(const TextBuffer &) = delete;

private:
    TextBuffer() = default;

    uint8_t outside_ = 0;
};

/* The one TextBuffer object (TextBuffer::instance()). */
extern TextBuffer &text_buffer;

#endif

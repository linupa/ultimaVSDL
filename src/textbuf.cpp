/* textbuf.cpp - the text buffer. See textbuf.h. */
#include "textbuf.h"

TextBuffer &TextBuffer::instance()
{
    static TextBuffer the_text_buffer;
    return the_text_buffer;
}

TextBuffer &text_buffer = TextBuffer::instance();

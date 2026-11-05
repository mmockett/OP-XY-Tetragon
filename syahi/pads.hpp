#pragma once
// The pad controls' captions, for the stock header (pad_control_text) and the
// kit's own: HUMAN "off", "1".."15"; GRIT "-16".."-1", "0", "+1".."+15".
#include "dsp.hpp"

namespace syahi {
static inline bool pad_control_text(u32 field, i32 value, char* text) {
    u32 n = 0;
    if (field == 0) {
        value = clamp(value, 0, 15);
        if (!value) { text[n++] = 'o'; text[n++] = 'f'; text[n++] = 'f'; }
        else {
            if (value >= 10) text[n++] = '1';
            text[n++] = static_cast<char>('0' + value % 10);
        }
    } else if (field == 1) {
        value = clamp(value, -16, 15);
        if (value < 0) { text[n++] = '-'; value = -value; }
        else if (value > 0) text[n++] = '+';
        if (value >= 10) text[n++] = '1';
        text[n++] = static_cast<char>('0' + value % 10);
    } else {
        return false;
    }
    text[n] = 0;
    return true;
}
static inline void pad_caption(u32 field, i32 value, char* text) {
    if (!pad_control_text(field, value, text)) text[0] = 0;
}
}

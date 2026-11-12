#pragma once
// CURIO's screen: the selected key's specimen alone, centred below the
// two-page header (art.h, from tools/make_panel.py).
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "kit.h"
#include "art.h"
#include "dsp.h"

namespace curio {
// The art's nodes, the header's eight bar nodes last; the header's 16 text slots.
using Frame = opxy::draw::Frame<4, 64, art::node_count, 16, 9>;
struct DisplayState {
    opxy::draw::PageHeader header;   // 20 bytes
    opxy::draw::NoteTracker notes;   // 16 bytes
    opxy::u32 initialized, shown, serial, track;
    opxy::i32 age;                   // ms since the selected key was struck; idle when negative
    opxy::i32 held;                  // ms the note has been held
    opxy::i32 life;                  // ms the hit's sound lasts, as the screen reckons it
    opxy::u32 keys[4];               // the reshaping outlines' keys
};
}

#pragma once
// CURIO's screen: the cabinet's drawer pulled open, below the two-page
// header (art.h, panel.svg.fragment, from tools/make_panel.py).
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "kit.h"
#include "art.h"
#include "dsp.h"

namespace curio {
// One reshaping outline of four points (the plumb line); the art's nodes, the
// header's eight bar nodes last; the header's 16 text slots; one palette entry.
using Frame = opxy::draw::Frame<1, 4, art::node_count, 16, 1>;
struct DisplayState {
    opxy::draw::PageHeader header;   // 20 bytes
    opxy::draw::NoteTracker notes;   // 16 bytes
    opxy::u32 initialized, shown, serial, track;
    opxy::i32 age;                   // ms since the selected key was struck; idle when negative
    opxy::i32 held;                  // ms the note has been held
    opxy::i32 life;                  // ms the hit's sound lasts, as the screen reckons it
    opxy::u32 keys[4];               // spare
};
}

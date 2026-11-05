#pragma once
// Syahi: one struck head, loaded and shaped into any drum. A synthesis drum
// kit: each key holds one of eight models and its eight controls, and on Shift
// its own mixer channel with two pad controls, HUMAN and GRIT.
#include <opxy/audio.hpp>
#include "voice.hpp"
#include "kit.h"

namespace syahi {
// Each key's notes so far, by the key's settings: HUMAN's variation is drawn
// from a key's own count, as the screen draws it from the key's note serial.
const u32 counted_keys = 24;
struct Counter {
    u32 identity, count, used;
};

struct Voice {
    Core core;
    u32 next;           // the played key's model, from drum_select
    u32 pending;        // a note started
    u32 quiet;          // blocks in a row under the floor
    u32 done;           // the sound has ended: render writes silence
    i32 human, grit;    // the key's pad controls, from drum_pad
    u32 raw[8];         // the controls the voice was last set up for
    i32 spot_offset;    // HUMAN's nudge to this hit's SPOT (0..127 units)
    i32 pitch_offset;   // and to its pitch, Q16 octaves
    i32 low, held, hp;  // GRIT's low-pass, its held sample, the 24 Hz high-pass
    i32 level;          // the hit's output gain, Q28 (engine.cpp level_of)
    u32 hold_left;
};
struct Shared {
    Scratch scratch;
    Counter counters[counted_keys];
    u32 clock;
};

// The drum kind's callbacks (the SDK's docs/api.md, synthesis drum kits).
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls);
const char* model_name(u32 model);
const char* parameter_name(u32 model, u32 slot);
u32 parameter_count(u32 model);
}

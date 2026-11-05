#pragma once
// CURIO: a cabinet of curiosities as a synthesis drum kit. Each key holds one
// of sixteen specimens (kit.h), the drawer's physics (thump.h, crack.h,
// tick.h, ring.h) and its eight controls; Shift adds the key's own mixer
// channel and two pad controls of its own, TUNE and VARY.
#include <opxy/audio.hpp>
#include "kit.h"
#include "ring.h"

namespace curio {

struct Voice {
    u32 model;      // the specimen sounding
    u32 next;       // the played key's specimen, from drum_select
    u32 owner;
    u32 pending;    // a note started
    u32 quiet;      // blocks in a row under the floor
    u32 done;       // the sound has ended: render writes silence
    i32 tune, vary; // the key's pad controls, from drum_pad
    i32 level;      // this hit's level (VARY), Q31
    i32 gain;       // this hit's output gain: the specimen's, its controls' trims and VARY, Q28
    i32 lim;        // the peak limiter's gain, Q31 (full: not limiting)
    union {
        Ball ball;
        Cask cask;
        Stone stone;
        Drip drip;
        Ice ice;
        Sheet sheet;
        Spark spark;
        Cork cork;
        Cicada cicada;
        Ratchet ratchet;
        Pod pod;
        Match match;
        Glass glass;
        BellJar belljar;
        Chalk chalk;
        Roar roar;
    } u;
};
struct Shared {
    u32 hits;       // every note so far: VARY's seed
};

// The drum kind's callbacks (the SDK's docs/api.md, synthesis drum kits).
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls);
const char* model_name(u32 model);
const char* parameter_name(u32 model, u32 slot);
u32 parameter_count(u32 model);
}

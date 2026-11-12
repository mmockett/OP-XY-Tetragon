#pragma once
// GALILEO: a synthesis drum kit with motion inside the hit. Each of the 24
// keys is one of Galileo's inclined planes: a note releases a ball (or a
// handful) that rings the frets, lands on the key's body and bounces to rest.
#include <opxy/audio.hpp>
#include <opxy/transport.hpp>
#include "dsp.h"
#include "kit.h"
#include "motion.h"
#include "tempo.h"
#include "bodies.h"

namespace galileo {
// A block's impacts gathered on the strike grid: every ball landing within one
// cell of it strikes the body once, together (dsp::together), from the
// strongest one's 8-sample step and place in the run. A cell is 32 samples
// (0.73 ms), 16 for a fill, so a block holds 2 or 4.
struct Cell {
    i32 v, top;           // the cell's force, and its strongest impact's (Q15)
    u32 fine, step;       // the strongest's 8-sample step in the block, and its place in the run
};
const u32 cells_max = 4;

struct Voice {
    u32 model, next;      // the model sounding, and the played key's, from drum_select
    u32 owner;
    u32 pending;          // a note started: set the run up at the next render
    u32 fresh;            // initialized since its last note start
    u32 stamp;            // the kit's block count when it last rendered
    u32 quiet;            // blocks in a row under the floor with nothing to come
    u32 done;             // its sound has ended
    u32 kept;             // blocks kept counting the kit's clock since
    i32 space, feel;      // the key's pad fields (drum_pad)
    u32 seed;             // FEEL's looseness
    u32 t;                // samples since the note
    u32 balls;            // balls still moving
    i32 gain;             // the model's output gain, trimmed by its body controls (Q26)
    motion::Run run;
    u32 planned[4];       // the run's inputs (motion controls, pad fields, tempo), so a repeat isn't planned again
    motion::Cursor cursor[motion::balls_max];
    body::Body body;
};
// The kit's own clock and tempo, across its voices and tracks.
struct Shared {
    u32 block;            // blocks rendered while anything sounds
    u32 now;              // the same in samples
    u32 active;           // voices alive (initialized, not destroyed)
    u32 latest;           // the newest note's voice
    u32 bpm_x10;          // the SDK's tempo when it passes one (drum_transport), else 0
    u32 p16_transport;    // its sixteenth, samples
    u32 notes;
    tempo::Learner learner;
};

// The drum kind's callbacks (the SDK's docs/api.md, synthesis drum kits).
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls);
const char* model_name(u32 model);
const char* parameter_name(u32 model, u32 slot);
u32 parameter_count(u32 model);
}

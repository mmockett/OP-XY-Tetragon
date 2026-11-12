#pragma once
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "dsp.h"
#include "kit.h"
#include "motion.h"
#include "tempo.h"
#include "captions.h"
#include "art.h"

namespace galileo {
// The art's nodes, then the two-page header's eight bars; its 16 text slots.
const u32 header_node = art::node_count;
using Frame = opxy::draw::Frame<2, 64, art::node_count + opxy::draw::page_header_nodes, opxy::draw::page_header_texts, art::line_palette + 2 * art::line_bands>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    u32 initialized, track, serial;
    i32 age;            // Q10 seconds since the selected key's last note (large when none)
    u32 clock;          // samples the screen has run, for its own tempo learner
    u32 planned;        // the controls the run was laid out from (a key), 0 for none
    u32 shown;          // the model last drawn, plus one
    u32 lit;            // the dots of the ball's path lit, from the lip
    i32 swing;          // its amplitude now, Q10 degrees
    tempo::Learner learner;
    motion::Run run;
};
}

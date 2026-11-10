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
using Frame = opxy::draw::Frame<1, 64, art::node_count + opxy::draw::page_header_nodes, opxy::draw::page_header_texts, art::palette>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::draw::NoteTracker notes;
    u32 initialized, track, serial;
    i32 age;            // Q10 seconds since the selected key's last note (large when none)
    u32 clock;          // samples the screen has run, for its own tempo learner
    u32 planned;        // the controls the run was laid out from (a key), 0 for none
    u32 bell_risers;    // the risers the bells were last hung for (a frame behind the board), 0 for none
    tempo::Learner learner;
    motion::Run run;
};
}

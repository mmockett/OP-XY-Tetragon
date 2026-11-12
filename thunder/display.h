#pragma once
// THUNDER's screen: the theatre thunder sheet in its gallows frame (art.h,
// panel.svg.fragment), drawn for the selected key's model and controls.
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "art.h"
#include "units.h"

namespace thunder {
using Frame = opxy::draw::Frame<2, 64, art::node_count, 16, 16>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::u32 initialized, track, serial;
    opxy::i32 age;        // Q10 seconds since the selected key last struck
};
}

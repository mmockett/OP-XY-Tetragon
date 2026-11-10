#pragma once
// Syahi's screen: a tabla in the stock isometric look, its parts moved by the
// selected key's controls and its nodal lines lit by each hit.
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "art.h"

namespace syahi {
// No reshaping outlines: the two-page header's eight bar nodes, the drum's
// parts and the header's 16 text slots.
using Frame = opxy::draw::Frame<0, 0, art::nodes, 16, 16>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::u32 initialized, track, serial;
    opxy::i32 age;           // Q10 s since the selected key's last hit
    // The nodes as the last frame left them: a control that moves many parts at
    // once (FORM taking a band off moves the whole head) moves them a few frames
    // at a time, so no frame changes more than the device redraws smoothly.
    opxy::draw::Node before[art::nodes];
};
}

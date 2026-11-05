#pragma once
// Syahi's screen: an engraved plate of the selected key's struck object, in
// section on the left and its head seen from above on the right.
#include <opxy/draw.hpp>
#include <opxy/page_header.hpp>
#include "art.h"

namespace syahi {
// Four reshaping outlines (head, shell, coupling, striker), the fixed parts,
// the two-page header's eight bar nodes and its 16 text slots.
using Frame = opxy::draw::Frame<4, 64, art::nodes, 16, 16>;
struct DisplayState {
    opxy::draw::PageHeader header;
    opxy::u32 initialized, track, serial;
    opxy::i32 age;           // Q10 s since the selected key's last hit
    opxy::u32 shown;         // the model drawn last, plus one; 0 after a reset
    // The nodes as the last frame left them: a turned control that moves many
    // parts at once (TUNE scales the whole object) moves them a few frames at a
    // time, so no frame changes more than the device redraws smoothly.
    opxy::draw::Node before[art::nodes];
};
}

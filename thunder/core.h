#pragma once
// THUNDER: one thunder sheet, every drum. A synthesis drum kit (the SDK's drum
// kind): each of the 24 keys holds one of twelve ways of playing the sheet and
// its eight controls, and on Shift its own mixer channel and two pad controls,
// TUNE and DRIVE.
#include <opxy/audio.hpp>
#include "kit.h"
#include "models.h"

namespace thunder {
struct Shared { u32 notes; };

// The drum kind's callbacks (the SDK's docs/api.md, synthesis drum kits).
void drum_defaults(u32 key, opxy::u8* model, opxy::u8* controls);
const char* model_name(u32 model);
const char* parameter_name(u32 model, u32 slot);
u32 parameter_count(u32 model);
}

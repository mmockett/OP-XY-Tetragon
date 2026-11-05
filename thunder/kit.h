#pragma once
// THUNDER's kit: its twelve models, their eight controls, and the 24 keys'
// default sounds (keys 53..76, laid out as stock's drum kits are).
#include <opxy/types.hpp>

namespace thunder {
namespace kit {
const opxy::u32 keys = 24;
const opxy::u32 models = 12;
struct Model {
    const char* name;
    const char* labels[8];
    unsigned char defaults[8];   // 0..127
};
const Model model[models] = {
    {"BOOM", {"SIZE", "DAMP", "BEATER", "SPOT", "TENSION", "CAMBER", "GLIDE", "CHAIN"}, {52, 76, 18, 10, 110, 70, 58, 0}},
    {"SLAP", {"SIZE", "DAMP", "BEATER", "SPOT", "TENSION", "CHAIN", "LINKS", "METAL"}, {64, 84, 74, 40, 96, 80, 44, 40}},
    {"POP", {"SIZE", "DAMP", "POPS", "SPREAD", "SNAP", "METAL", "CHAIN", "SCATTER"}, {64, 80, 64, 60, 64, 12, 0, 15}},
    {"FLICK", {"SIZE", "DAMP", "NAIL", "SPOT", "TENSION", "CHAIN", "SWING", "SCRAPE"}, {64, 84, 80, 20, 40, 0, 0, 0}},
    {"TOM", {"SIZE", "DAMP", "BEATER", "SPOT", "TENSION", "CAMBER", "ROLL", "CHAIN"}, {69, 84, 60, 60, 100, 44, 0, 0}},
    {"CRASH", {"SIZE", "DAMP", "BEATER", "SPOT", "TENSION", "SWELL", "SWING", "CHAIN"}, {50, 16, 96, 90, 10, 50, 30, 0}},
    {"CLANK", {"SIZE", "DAMP", "BEATER", "SPOT", "SHAPE", "SHEET", "CHAIN", "BUZZ"}, {56, 92, 90, 40, 96, 30, 0, 0}},
    {"GRIT", {"SIZE", "DAMP", "BEAD", "GRAINS", "SWELL", "METAL", "CHAIN", "POUR"}, {74, 70, 70, 84, 40, 20, 0, 40}},
    {"HOP", {"SIZE", "DAMP", "BALL", "SPOT", "DROP", "LOSS", "TENSION", "CHAIN"}, {60, 70, 96, 50, 50, 50, 20, 0}},
    {"ZAP", {"SIZE", "DAMP", "SWEEP", "ECHOS", "DEPTH", "METAL", "CHAIN", "CURVE"}, {64, 66, 44, 40, 60, 30, 0, 40}},
    {"BOW", {"SIZE", "DAMP", "PRESS", "SPOT", "SPEED", "TENSION", "CHAIN", "BEND"}, {56, 50, 40, 30, 50, 20, 0, 30}},
    {"SHAKE", {"SIZE", "DAMP", "RUMBLE", "RATE", "TENSION", "CHAIN", "CRACK", "SWELL"}, {50, 50, 56, 60, 30, 20, 60, 64}},
};
// Each key's model and its eight controls.
const unsigned char key_model[keys] = {0, 1, 4, 4, 4, 2, 6, 6, 3, 3, 5, 5, 7, 8, 9, 11, 0, 1, 3, 4, 2, 10, 9, 11};
const unsigned char key_values[keys][8] = {
    {52, 76, 18, 10, 110, 70, 58, 0},     // 53 BD: BOOM
    {64, 84, 74, 40, 96, 80, 44, 40},     // 54 SD: SLAP, wired
    {87, 84, 60, 60, 100, 44, 0, 0},      // 55 HT: TOM high
    {69, 84, 60, 60, 100, 44, 0, 0},      // 56 MT: TOM mid
    {51, 80, 60, 60, 100, 44, 0, 0},      // 57 LT: TOM low
    {64, 80, 64, 60, 64, 12, 0, 15},      // 58 CP: POP
    {90, 110, 90, 40, 20, 30, 0, 0},      // 59 RS: CLANK, a high rim
    {56, 92, 90, 40, 96, 30, 0, 0},       // 60 CB: CLANK, a stirrup cowbell
    {64, 84, 80, 20, 40, 0, 0, 0},        // 61 CH: FLICK closed
    {64, 40, 80, 20, 40, 0, 40, 0},       // 62 OH: FLICK open
    {80, 20, 100, 40, 10, 0, 30, 0},      // 63 RC: CRASH as a ride
    {50, 16, 96, 90, 10, 50, 30, 0},      // 64 CC: CRASH
    {74, 70, 70, 84, 40, 20, 0, 40},      // 65: GRIT
    {60, 70, 96, 50, 50, 50, 20, 0},      // 66: HOP
    {64, 66, 44, 40, 60, 30, 0, 40},      // 67: ZAP
    {50, 50, 56, 60, 30, 20, 60, 64},     // 68: SHAKE, a thunderclap
    {40, 51, 18, 10, 110, 96, 70, 0},     // 69: BOOM, long
    {80, 96, 74, 40, 96, 40, 60, 10},     // 70: SLAP, dry
    {64, 100, 50, 20, 40, 0, 0, 0},       // 71: FLICK, pedal
    {36, 74, 60, 60, 100, 44, 0, 0},      // 72: TOM, floor
    {70, 110, 0, 60, 64, 12, 0, 15},      // 73: POP, one snap
    {56, 50, 40, 30, 50, 20, 0, 30},      // 74: BOW
    {96, 66, 20, 0, 60, 30, 0, 40},       // 75: ZAP, short and high
    {20, 50, 90, 60, 30, 20, 60, 64},     // 76: SHAKE, long and low
};
// The keys that start in choke group 1 (the hats: closed, open, pedal).
const unsigned char key_choke[keys] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0};
}
}

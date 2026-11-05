#pragma once
// CURIO's cabinet: sixteen specimens in four drawers, their eight controls,
// how each control is captioned, and the default kit on the 24 keys. The
// engine and the screen both read these tables.
#include <opxy/types.hpp>

namespace curio {
namespace kit {
using opxy::i32;
using opxy::u32;
using opxy::u8;

const u32 keys = 24;
const u32 models = 16;
const u32 drawers = 4;
const char* const drawer_name[drawers] = {"THUMP", "CRACK", "TICK", "RING"};

enum Kind : u8 { amount, hertz, linear_hz, time, count };
// A control's caption: amount 0..100; hertz a 2^(b x) (b in Q12 octaves);
// a size that lowers a pitch is captioned as an amount, so the number rises clockwise;
// linear_hz a + b x; time a + b x^e microseconds (e Q12); count a + floor(b x^e).
struct Caption { Kind kind; i32 a, b, e; };
struct Model {
    const char* name;      // the header's MACHINE, lower case as the header shows it, at most 53 px at 20 px
    u8 drawer;
    const char* labels[8];
    u8 defaults[8];        // the design defaults, 0..127
    Caption caption[8];
};
// An amount, 0..100.
constexpr Caption P = {amount, 0, 0, 0};
const Model model[models] = {
    {"ball", 0, {"DROP", "BOUNCE", "SKIN", "BALL", "DECAY", "FLOOR", "SPOT", "GRIT"}, {70, 38, 53, 44, 19, 25, 0, 0},
     {P, P, {hertz, 30, 8192, 0}, P, {time, 60000, 1100000, 6554}, P, P, P}},
    {"cask", 0, {"SIZE", "FILL", "KNOCK", "STAVE", "BUNG", "HOOP", "SLOSH", "CELLAR"}, {44, 38, 57, 38, 25, 0, 0, 0},
     {{hertz, 40, 9421, 0}, P, P, P, P, P, P, P}},
    {"stone", 0, {"PITCH", "MALLET", "ROCK", "RING", "CHIP", "STACK", "DULL", "DUST"}, {38, 57, 51, 57, 38, 0, 38, 0},
     {{hertz, 110, 12288, 0}, P, P, {time, 120000, 1600000, 6144}, P, P, P, P}},
    {"drip", 0, {"SIZE", "RISE", "BASIN", "TAP", "DEPTH", "SPLASH", "WOBBLE", "ECHO"}, {64, 57, 38, 0, 44, 32, 0, 25},
     {P, P, P, {count, 1, 5, 4096}, P, P, P, P}},
    {"ice", 1, {"LOAD", "SHEET", "SHARDS", "TONE", "RING", "COLD", "LAKE", "SPREAD"}, {70, 51, 25, 44, 44, 64, 64, 0},
     {P, P, {count, 1, 7, 4096}, P, {time, 50000, 250000, 4096}, P, P, P}},
    {"sheet", 1, {"SIZE", "SHAKE", "RATTLE", "HANG", "THUNDER", "METAL", "BEATER", "ROLL"}, {64, 0, 57, 19, 0, 64, 51, 0},
     {P, P, P, {time, 150000, 3200000, 6554}, P, P, P, {count, 0, 9, 4096}}},
    {"spark", 1, {"CHARGE", "GAP", "JAR", "OZONE", "ARC", "SCATTER", "BODY", "DIST"}, {51, 51, 44, 13, 0, 25, 0, 0},
     {P, {time, 4000, 16000, 4096}, {linear_hz, 700, 2600, 0}, P, P, P, P, P}},
    {"cork", 1, {"NECK", "POP", "FIZZ", "BOTTLE", "THOCK", "SQUEAK", "SPILL", "GLUG"}, {51, 64, 19, 38, 51, 0, 25, 0},
     {{linear_hz, 350, 900, 0}, P, P, P, P, P, P, P}},
    {"cicda", 2, {"RIBS", "SONG", "SHELL", "BODY", "TAIL", "CHIRP", "WING", "SWARM"}, {32, 70, 76, 38, 25, 0, 0, 0},
     {{count, 1, 31, 6144}, {hertz, 120, 12288, 0}, {hertz, 3500, 6554, 0}, P, {time, 12000, 400000, 6963}, P, P, {count, 1, 5, 4096}}},
    {"rtcht", 2, {"TEETH", "CRANK", "SLAT", "WOOD", "SLOW", "SPRING", "BOX", "WEAR"}, {19, 64, 64, 51, 0, 0, 25, 0},
     {{count, 1, 15, 4096}, {linear_hz, 10, 70, 0}, {linear_hz, 900, 3100, 0}, P, P, P, P, P}},
    {"pod", 2, {"SEEDS", "SHAKE", "SHELL", "SIZE", "DRY", "TILT", "SWISH", "SCATTER"}, {57, 38, 64, 51, 0, 0, 25, 0},
     {P, P, {linear_hz, 1800, 5000, 0}, P, P, P, P, P}},
    {"mtch", 2, {"SCRAPE", "FLARE", "HEAD", "DRAW", "PHOS", "WOOD", "HISS", "PUFF"}, {64, 51, 51, 38, 25, 0, 51, 0},
     {P, P, {linear_hz, 2000, 4000, 0}, {time, 20000, 100000, 4096}, P, P, P, P}},
    {"glass", 3, {"WATER", "STRIKE", "RING", "RIM", "RUB", "SLOSH", "CRACK", "CHINK"}, {44, 57, 57, 64, 0, 25, 0, 0},
     {P, P, {time, 350000, 4000000, 6144}, P, P, P, P, P}},
    {"bell", 3, {"STROKES", "BELL", "AIR", "HAMMER", "RING", "GONG", "JAR", "BASE"}, {0, 57, 13, 32, 70, 0, 0, 0},
     {{count, 1, 11, 4096}, {hertz, 450, 7373, 0}, P, P, {time, 80000, 1400000, 5734}, P, P, P}},
    {"chalk", 3, {"PITCH", "PRESS", "SLATE", "GLIDE", "LENGTH", "GRIT", "CHATTER", "DUST"}, {51, 51, 64, 38, 32, 0, 0, 0},
     {{hertz, 500, 10240, 0}, P, {linear_hz, 1500, 3500, 0}, P, {time, 30000, 300000, 4096}, P, P, P}},
    {"roar", 3, {"WHIRL", "SIZE", "CORD", "SWELL", "LENGTH", "THROB", "BUZZ", "WIND"}, {44, 64, 51, 38, 38, 76, 38, 25},
     {{linear_hz, 2, 14, 0}, P, P, {time, 20000, 600000, 4096}, {time, 300000, 2200000, 4096}, P, P, P}},
};
// Each drawer's first specimen and how many it holds (modelGroups).
const u8 drawer_first[drawers] = {0, 4, 8, 12};

// The default kit: kicks low, then snares and claps, hats on the black keys
// above them, toms between, and percussion and effects at the top.
const u8 key_model[keys] = {0, 1, 4, 7, 6, 5, 8, 1, 8, 1, 8, 1, 10, 9, 12, 13, 2, 11, 2, 5, 3, 14, 15, 4};
const u8 key_values[keys][8] = {
    {70, 38, 53, 44, 19, 25, 0, 0},      //  1 BALL: kick
    {32, 13, 44, 32, 38, 0, 13, 25},     //  2 CASK: boom
    {70, 51, 25, 44, 44, 64, 64, 0},     //  3 ICE: snare
    {70, 64, 0, 25, 76, 0, 0, 0},        //  4 CORK: rim
    {51, 51, 44, 13, 0, 25, 0, 0},       //  5 SPARK: clap
    {70, 0, 76, 13, 0, 70, 76, 0},       //  6 SHEET: iron snare
    {25, 76, 76, 38, 22, 0, 0, 0},       //  7 CICADA: closed hat
    {57, 89, 51, 38, 25, 0, 0, 0},       //  8 CASK: low tom
    {13, 57, 57, 32, 13, 25, 0, 0},      //  9 CICADA: pedal hat
    {76, 89, 51, 38, 25, 0, 0, 0},       // 10 CASK: mid tom
    {70, 95, 83, 44, 50, 19, 0, 0},      // 11 CICADA: open hat
    {96, 89, 51, 38, 25, 0, 0, 0},       // 12 CASK: high tom
    {57, 38, 64, 51, 0, 0, 25, 0},       // 13 POD: shaker
    {19, 64, 64, 51, 0, 0, 25, 0},       // 14 RATCHET
    {44, 57, 57, 64, 0, 25, 0, 0},       // 15 GLASS
    {0, 51, 19, 51, 32, 38, 0, 0},       // 16 BELL JAR: cowbell
    {25, 57, 51, 57, 38, 0, 38, 0},      // 17 STONE: low
    {64, 51, 51, 38, 25, 0, 51, 0},      // 18 MATCH: brush
    {70, 64, 51, 51, 38, 0, 38, 0},      // 19 STONE: high
    {83, 64, 38, 83, 51, 70, 57, 0},     // 20 SHEET: crash, long hang and thunder
    {64, 57, 38, 0, 44, 32, 0, 25},      // 21 DRIP
    {51, 51, 64, 38, 32, 0, 0, 0},       // 22 CHALK
    {44, 64, 51, 38, 38, 76, 38, 25},    // 23 ROAR
    {51, 102, 0, 127, 89, 51, 38, 76},   // 24 ICE: the "pew", thick sheet, all tone, spread
};

// TUNE's and VARY's captions (padControls).
inline void tune_text(i32 st, char* out) {
    u32 n = 0;
    if (st > 0) out[n++] = '+';
    if (st < 0) { out[n++] = '-'; st = -st; }
    if (st >= 10) out[n++] = static_cast<char>('0' + st / 10);
    out[n++] = static_cast<char>('0' + st % 10);
    out[n++] = 's';
    out[n++] = 't';
    out[n] = 0;
}
inline void vary_text(i32 v, char* out) {
    u32 n = 0;
    if (v >= 10) out[n++] = static_cast<char>('0' + v / 10);
    out[n++] = static_cast<char>('0' + v % 10);
    out[n] = 0;
}
}
}

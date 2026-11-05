// CURIO's header, as the SDK's padMix and padControls give it: without
// Shift the key's eight controls in real units; under Shift on the first row
// the specimen (under its drawer as the title), CHOKE, PAN and LEVEL, and on
// the second TUNE, VARY, FX I and FX II, on whichever row Input's line says.
#include "../engine.cpp"
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
curio::DisplayState state;
curio::Frame frame;
void show(opxy::u32 model, opxy::u32 line, bool shift, opxy::i32 pan = 0, opxy::i32 level = 0, opxy::u32 choke = 0,
          opxy::i32 tune = 0, opxy::i32 vary = 0, opxy::i32 fx1 = 0, opxy::i32 fx2 = 0) {
    opxy::draw::Input in = opxy::draw::Input();
    in.model = model;
    for (int c = 0; c < 8; ++c) in.slots[c] = curio::kit::model[model].defaults[c] << 24;
    for (opxy::u32 i = 0; i < 4; ++i) in.shown[i] = static_cast<opxy::u8>(line * 4 + i);
    in.line = static_cast<opxy::u8>(line);
    in.shift = shift ? 1 : 0;
    in.choices[0] = static_cast<opxy::u8>(model / 4);
    in.choices[1] = static_cast<opxy::u8>(model % 4);
    in.choices[2] = static_cast<opxy::u8>(choke);
    in.pan = static_cast<opxy::i8>(pan);
    in.level = static_cast<opxy::i8>(level);
    in.pad[0] = static_cast<opxy::i8>(tune);
    in.pad[1] = static_cast<opxy::i8>(vary);
    in.pad[2] = static_cast<opxy::i8>(fx1);
    in.pad[3] = static_cast<opxy::i8>(fx2);
    curio::display_reset(state);
    curio::display(state, in, frame);
}
void expect(const char* what, const char* shown, const char* wanted) {
    if (std::strcmp(shown, wanted)) { std::printf("FAIL %s reads \"%s\", not \"%s\"\n", what, shown, wanted); ++failures; }
}
}

int main() {
    // Without Shift: the ball's controls in their units.
    show(0, 0, false);
    expect("SKIN", frame.header[2], "53Hz");
    expect("the first title", frame.texts[0].chars, "drop");
    show(0, 1, false);
    expect("DECAY", frame.header[0], "0.11s");
    // Shift's first row: the specimen under its drawer, choke, pan, level.
    const struct { opxy::u32 m; const char *drawer, *name; } names[] = {{0, "thump", "ball"}, {5, "crack", "sheet"}, {8, "tick", "cicda"}, {9, "tick", "rtcht"}, {11, "tick", "mtch"}, {13, "ring", "bell"}};
    for (const auto& n : names) {
        show(n.m, 0, true, -40, -6, 2);
        expect("MACHINE", frame.header[0], n.name);
        expect("MACHINE's title", frame.texts[0].chars, n.drawer);
        expect("CHOKE", frame.header[1], "2");
        expect("PAN", frame.header[2], "-40");
        expect("LEVEL", frame.header[3], "-6dB");
    }
    show(0, 0, true, 100, 0, 0);
    expect("CHOKE", frame.header[1], "none");
    expect("PAN", frame.header[2], "100");
    expect("LEVEL", frame.header[3], "0dB");
    // Shift's second row: TUNE, VARY and the key's own sends.
    show(3, 1, true, 0, 0, 0, -5, 8, 0, 4);
    expect("TUNE", frame.header[0], "-5st");
    expect("VARY", frame.header[1], "8");
    expect("FX I", frame.header[2], "TRK");
    expect("FX II", frame.header[3], "50");
    expect("the second row's title", frame.texts[0].chars, "tune");
    show(3, 1, true, 0, 0, 0, 15, 0, 7, 1);
    expect("TUNE", frame.header[0], "+15st");
    expect("VARY", frame.header[1], "0");
    expect("FX I", frame.header[2], "100");
    expect("FX II", frame.header[3], "0");
    // Both Shift rows at once, stacked as the pages: the edited row bright
    // (titles 0..3, values 4..7), the other grey (titles 8..11, values 12..15).
    show(3, 1, true, -40, -6, 2, -5, 8, 0, 4);
    expect("the grey first-row title", frame.texts[9].chars, "choke");
    expect("the grey CHOKE", frame.texts[13].chars, "2");
    expect("the grey PAN", frame.texts[14].chars, "-40");
    expect("the grey LEVEL", frame.texts[15].chars, "-6dB");
    show(3, 0, true, -40, -6, 2, -5, 8, 0, 4);
    expect("the grey second-row title", frame.texts[8].chars, "tune");
    expect("the grey TUNE", frame.texts[12].chars, "-5st");
    expect("the grey VARY", frame.texts[13].chars, "8");
    expect("the grey FX I", frame.texts[14].chars, "TRK");
    expect("the grey FX II", frame.texts[15].chars, "50");
    expect("the bright PAN", frame.texts[6].chars, "-40");
    // Every name shows at the header's own size, bright or grey: none is
    // shrunk to fit.
    show(9, 0, true);
    if (frame.texts[4].scale != 1024) { std::printf("FAIL the specimen's name is drawn at scale %d\n", frame.texts[4].scale); ++failures; }
    show(9, 1, true);
    if (frame.texts[12].scale != 1024) { std::printf("FAIL the specimen's grey name is drawn at scale %d\n", frame.texts[12].scale); ++failures; }
    return failures ? 1 : 0;
}

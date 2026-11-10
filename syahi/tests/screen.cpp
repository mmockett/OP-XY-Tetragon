// Syahi's screen checks, on the real display():
// - every frame is valid, for every model with every control through all 128
//   values, struck and at rest, and on both Shift rows;
// - no outline reshapes (tools/make_panel.py checks every authored outline:
//   one subpath of at most 64 points, 1/8 px apart, a closing edge of 1/4 px,
//   some area, no turn sharper than SHARPEST);
// - churn: nothing changes at rest, and at most an average of 20 nodes a frame
//   while hits sound, at the defaults and with every control up; no frame
//   changes more than 30 while a control is turned or jumps back;
// - every header value fits 53 px at the full 20 px (the OP-XY draws header
//   values at full size, whatever their scale) and every title 56 px at 10 px,
//   in the OP-XY's font (tests/widths.h);
// - the drum's six controls each move the picture between 0 and 127;
// - the struck lines go back as the model's own sound does: note-off and the
//   track's amp envelope change nothing (the OP-XY applies neither to a kit's voice);
// - HUMAN moves the stroke's spot from hit to hit, by the engine's own variation.
#include "../display.cpp"
#include "../engine.cpp"
#include "widths.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

static_assert(syahi::Frame::path_count == 0, "Syahi's screen reshapes no outline");

namespace {
int failures = 0, reported = 0;
#define CHECK(cond, ...) do { if (!(cond)) { if (reported++ < 40) { std::printf("FAIL "); std::printf(__VA_ARGS__); std::printf("\n"); } ++failures; } } while (0)

syahi::DisplayState state;
syahi::Frame frame, before;

opxy::draw::Input input_for(unsigned model, const unsigned* raw) {
    opxy::draw::Input in = opxy::draw::Input();
    in.model = model;
    for (int i = 0; i < 8; ++i) in.slots[i] = raw[i] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(raw[i] << 24);
    for (int i = 0; i < 4; ++i) { in.shown[i] = static_cast<opxy::u8>(i); in.parameters[i] = in.slots[i]; }
    in.elapsed = 17;   // a 60th of a second, Q10
    in.amp.attack = in.amp.decay = in.amp.sustain = in.amp.release = -1;
    return in;
}
void draw(const char* what, const opxy::draw::Input& in) {
    syahi::display(state, in, frame);
    CHECK(opxy::draw::valid(frame), "%s: invalid frame", what);
}
unsigned changed() {
    unsigned n = 0;
    for (unsigned i = 0; i < syahi::Frame::node_count; ++i) {
        const auto& a = frame.nodes[i];
        const auto& b = before.nodes[i];
        n += a.x != b.x || a.y != b.y || a.scale != b.scale || a.cos != b.cos || a.sin != b.sin || a.shade != b.shade || a.paint != b.paint;
    }
    return n;
}
// Average and peak changed nodes a frame over 3 s of hits every 300 ms, and the
// changes once everything has settled.
void churn(unsigned model, const unsigned* raw, double& average, unsigned& peak, unsigned& rest) {
    syahi::display_reset(state);
    frame = syahi::Frame();
    opxy::draw::Input in = input_for(model, raw);
    syahi::display(state, in, frame);
    unsigned total = 0, frames = 0;
    peak = 0;
    for (int f = 0; f < 180; ++f) {
        if (f % 18 == 0) { in.note_serial += 1; in.held_notes = 1; }
        if (f % 18 == 6) { in.held_notes = 0; in.note_off_serial += 1; }
        before = frame;
        syahi::display(state, in, frame);
        const unsigned c = changed();
        total += c;
        peak = c > peak ? c : peak;
        ++frames;
    }
    for (int f = 0; f < 600; ++f) syahi::display(state, in, frame);   // ten seconds to settle
    before = frame;
    syahi::display(state, in, frame);
    rest = changed();
    average = static_cast<double>(total) / frames;
}
double width(const char* text, int px) {
    double w = 0;
    for (; *text; ++text) w += (*text >= 32 && *text < 127 ? advances[*text - 32] : 600) * px / 1000.0;
    return w;
}
}

int main() {
    using syahi::kit::models;
    // ---- every control through every value, at rest and struck
    for (unsigned m = 0; m < models; ++m)
        for (unsigned slot = 0; slot < 8; ++slot)
            for (unsigned v = 0; v < 128; ++v) {
                unsigned raw[8];
                for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[m][c];
                raw[slot] = v;
                char what[64];
                std::snprintf(what, sizeof(what), "%s %s %u", syahi::kit::model_names[m], syahi::kit::labels[m][slot], v);
                syahi::display_reset(state);
                frame = syahi::Frame();
                opxy::draw::Input in = input_for(m, raw);
                draw(what, in);
                if (v % 8 == 0) {
                    in.note_serial += 1;
                    in.held_notes = 1;
                    in.pad[0] = 15;
                    for (int f = 0; f < 12; ++f) draw(what, in);   // through the struck lines
                }
            }
    // ---- every pair of extremes on each model, struck
    for (unsigned m = 0; m < models; ++m)
        for (unsigned a = 0; a < 8; ++a)
            for (unsigned b = a + 1; b < 8; ++b)
                for (unsigned va : {0u, 127u})
                    for (unsigned vb : {0u, 127u}) {
                        unsigned raw[8];
                        for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[m][c];
                        raw[a] = va;
                        raw[b] = vb;
                        syahi::display_reset(state);
                        frame = syahi::Frame();
                        opxy::draw::Input in = input_for(m, raw);
                        draw("pair", in);
                        in.note_serial += 1;
                        for (int f = 0; f < 6; ++f) draw("pair struck", in);
                    }
    // ---- churn
    double worst = 0;
    for (unsigned m = 0; m < models; ++m) {
        unsigned raw[8], busy[8];
        for (int c = 0; c < 8; ++c) { raw[c] = syahi::kit::model_defaults[m][c]; busy[c] = 127; }
        double average;
        unsigned peak, rest;
        churn(m, raw, average, peak, rest);
        CHECK(average <= 20, "%s churns %.1f nodes a frame at its defaults", syahi::kit::model_names[m], average);
        CHECK(rest == 0, "%s changes %u nodes at rest", syahi::kit::model_names[m], rest);
        worst = average > worst ? average : worst;
        std::printf("churn %-6s defaults %.1f (peak %u)", syahi::kit::model_names[m], average, peak);
        churn(m, busy, average, peak, rest);
        CHECK(average <= 20, "%s churns %.1f nodes a frame with its controls up", syahi::kit::model_names[m], average);
        CHECK(rest == 0, "%s changes %u nodes at rest with its controls up", syahi::kit::model_names[m], rest);
        worst = average > worst ? average : worst;
        std::printf(", controls up %.1f (peak %u)\n", average, peak);
    }
    // ---- turning: each control swept a value a frame while hits sound, then
    // back in one jump, never changes more than 30 nodes in a frame, and settles
    unsigned turning_peak = 0;
    for (unsigned m = 0; m < models; ++m)
        for (unsigned slot = 0; slot < 8; ++slot) {
            unsigned raw[8];
            for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[m][c];
            syahi::display_reset(state);
            frame = syahi::Frame();
            opxy::draw::Input in = input_for(m, raw);
            syahi::display(state, in, frame);
            for (int f = 0; f <= 140; ++f) {
                const unsigned v = f < 128 ? static_cast<unsigned>(f) : 0;   // up, then straight back to 0
                in.slots[slot] = v >= 127 ? 0x7fffffff : static_cast<opxy::i32>(v << 24);
                if (slot >= 4) { for (int i = 0; i < 4; ++i) in.shown[i] = static_cast<opxy::u8>(4 + i); }   // the header slides to page 2
                if (f % 20 == 0) { in.note_serial += 1; in.held_notes = 1; }
                before = frame;
                syahi::display(state, in, frame);
                const unsigned c = changed();
                turning_peak = c > turning_peak ? c : turning_peak;
                CHECK(c <= 30, "turning %s %s changes %u nodes in a frame", syahi::kit::model_names[m], syahi::kit::labels[m][slot], c);
            }
            for (int f = 0; f < 600; ++f) syahi::display(state, in, frame);
            before = frame;
            syahi::display(state, in, frame);
            CHECK(changed() == 0, "turning %s %s: the screen never settles", syahi::kit::model_names[m], syahi::kit::labels[m][slot]);
        }
    std::printf("turning any control: at most %u nodes a frame\n", turning_peak);
    // ---- the drum's six controls each move the still picture
    for (unsigned m = 0; m < models; ++m)
        for (unsigned slot = 0; slot < 6; ++slot) {
            unsigned raw[8];
            for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[m][c];
            syahi::Frame low, high;
            raw[slot] = 0;
            syahi::display_reset(state);
            opxy::draw::Input in = input_for(m, raw);
            low = syahi::Frame();
            syahi::display(state, in, low);
            raw[slot] = 127;
            syahi::display_reset(state);
            in = input_for(m, raw);
            high = syahi::Frame();
            syahi::display(state, in, high);
            bool differ = false;
            for (unsigned n = syahi::art::first; n < syahi::Frame::node_count; ++n) {
                const auto& a = low.nodes[n];
                const auto& b = high.nodes[n];
                differ = differ || a.x != b.x || a.y != b.y || a.scale != b.scale || a.shade != b.shade;
            }
            CHECK(differ, "%s %s moves nothing between 0 and 127", syahi::kit::model_names[m], syahi::kit::labels[m][slot]);
        }
    // FORM: four bands at 0, one at 127
    {
        unsigned raw[8];
        for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[3][c];
        for (unsigned v : {0u, 127u}) {
            raw[4] = v;
            syahi::display_reset(state);
            frame = syahi::Frame();
            syahi::display(state, input_for(3, raw), frame);
            int shown = 0;
            for (unsigned b = 0; b < 4; ++b) shown += frame.nodes[syahi::art::band_0 + b].x < opxy::draw::hidden_x * opxy::draw::unit;
            CHECK(shown == (v ? 1 : 4), "FORM %u shows %d bands", v, shown);
        }
    }
    // ---- captions: every value at 20 px within 53 px, every title at 10 px within 56 px
    for (unsigned m = 0; m < models; ++m)
        for (unsigned slot = 0; slot < 8; ++slot)
            for (unsigned v = 0; v < 128; ++v)
                for (unsigned shift = 0; shift < 3; ++shift) {
                    unsigned raw[8];
                    for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[m][c];
                    raw[slot] = v;
                    syahi::display_reset(state);
                    opxy::draw::Input in = input_for(m, raw);
                    if (shift) {
                        in.shift = 1;
                        in.line = static_cast<opxy::u8>(shift - 1);
                        for (int i = 0; i < 4; ++i) in.shown[i] = static_cast<opxy::u8>((shift - 1) * 4 + i);
                        in.pan = static_cast<opxy::i8>(static_cast<int>(v) * 200 / 127 - 100);
                        in.level = static_cast<opxy::i8>(-static_cast<int>(v) * 30 / 127);
                        in.pad[0] = static_cast<opxy::i8>(v % 16);
                        in.pad[1] = static_cast<opxy::i8>(static_cast<int>(v % 32) - 16);
                        in.pad[2] = static_cast<opxy::i8>(v % 8);
                        in.pad[3] = static_cast<opxy::i8>((v + 3) % 8);
                        in.choices[1] = static_cast<opxy::u8>(v % 4);
                    }
                    syahi::display(state, in, frame);
                    for (int t = 0; t < 16; ++t) {
                        const bool title = (t % 8) < 4;
                        // The OP-XY draws header values at the full 20 px whatever their scale; titles at 10 px.
                        const double w = width(frame.texts[t].chars, title ? 10 : 20);
                        const double room = title ? 56 : 53;
                        CHECK(w <= room, "%s %s %u: \"%s\" is %.1f px", syahi::kit::model_names[m], syahi::kit::labels[m][slot], v, frame.texts[t].chars, w);
                    }
                }
    // ---- a kit's voice ignores the amp envelope and note-off: so do the struck lines
    {
        unsigned raw[8];
        for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[2][c];
        raw[1] = 100;   // a long tom
        auto ring = [&](opxy::i32 release, int held) {
            syahi::display_reset(state);
            frame = syahi::Frame();
            opxy::draw::Input in = input_for(2, raw);
            in.amp.attack = in.amp.decay = in.amp.sustain = 0;
            in.amp.release = release;
            syahi::display(state, in, frame);
            in.note_serial += 1;
            in.held_notes = 1;
            int last = 0;
            for (int f = 0; f < 400; ++f) {
                if (f == held) { in.held_notes = 0; in.note_off_serial += 1; }
                before = frame;
                syahi::display(state, in, frame);
                if (changed()) last = f;
            }
            return last;   // the last frame a line went back
        };
        const int held = ring(-1, 300), tapped = ring(-1, 2), short_release = ring(0x7f000000, 2);
        std::printf("ring: %d frames held, %d tapped, %d with a short amp release\n", held, tapped, short_release);
        CHECK(held > 30, "the long tom's lines go back after only %d frames", held);
        CHECK(tapped == held && short_release == held, "note-off or the amp envelope changes the ring (%d, %d, %d frames)", held, tapped, short_release);
    }
    // ---- HUMAN moves the stroke's spot hit to hit, and only with HUMAN
    {
        unsigned raw[8];
        for (int c = 0; c < 8; ++c) raw[c] = syahi::kit::model_defaults[3][c];
        for (int human : {0, 12}) {
            opxy::i32 xs[6];
            for (int hit = 0; hit < 6; ++hit) {
                syahi::display_reset(state);
                opxy::draw::Input in = input_for(3, raw);
                in.pad[0] = static_cast<opxy::i8>(human);
                in.note_serial = static_cast<opxy::u32>(hit);
                syahi::display(state, in, frame);
                xs[hit] = frame.nodes[syahi::art::spot].x;
                const syahi::Variation v = syahi::vary(static_cast<opxy::u32>(hit), human);
                CHECK(human || v.spot == 0, "HUMAN 0 varies the spot");
            }
            bool moved = false;
            for (int hit = 1; hit < 6; ++hit) moved = moved || xs[hit] != xs[0];
            CHECK(moved == (human != 0), "HUMAN %d: the spot %s", human, moved ? "wanders" : "stays");
        }
    }
    std::printf("nodes %u of 512, no reshaping outlines; worst average churn %.1f a frame\n", syahi::Frame::node_count, worst);
    if (failures) std::printf("%d screen checks failed\n", failures);
    else std::printf("screen: all checks passed\n");
    return failures ? 1 : 0;
}

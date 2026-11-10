// GALILEO's screen checks, through its real display():
// - every frame is valid, and the reshaping outline (the drum's ring) follows
//   the device's drawing rules (64 distinct points at least 1/8 px apart, a
//   closing edge of at least 1/4 px, area enclosed, no turn sharper than
//   SHARPEST degrees: the device strokes with unlimited mitres), across every
//   model and every control's ends, at rest and mid-run;
// - churn while a run plays stays within budget, and is 0 at rest; turning
//   any control a step at a time changes at most 30 nodes in a frame;
// - the run plays out whatever the track's amp envelope and gate do: a kit's
//   voice ignores both (it plays until its own sound ends), so the screen does;
// - a hit whose sound lands at once still shows the ball roll down the board,
//   and with reduced motion it is on the head at once;
// - the controls show: risers, bells, balls, wires and the drum's size;
// - Shift shows MACHINE, CHOKE, PAN, LEVEL and SPACE, FEEL, FX I, FX II;
// - every header caption fits its column in the device font (advance widths
//   measured with the suite's tools/caption_width.py; tools/check_captions.py
//   measures them in the font itself).
#include "../engine.cpp"
#include "../display.cpp"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <initializer_list>

namespace {
int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL " __VA_ARGS__); std::printf("\n"); ++failures; } } while (0)
using galileo::Frame;
galileo::DisplayState state;
Frame frame, before;
opxy::draw::Input input;

void begin(unsigned model, const unsigned char* c, int space = 0, int feel = 0) {
    std::memset(&state, 0, sizeof(state));
    std::memset(&frame, 0, sizeof(frame));
    input = opxy::draw::Input();
    input.track = 1;
    input.model = model;
    for (int i = 0; i < 8; ++i) input.slots[i] = c[i] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(c[i]) << 24;
    for (int i = 0; i < 4; ++i) { input.shown[i] = static_cast<opxy::u8>(4 + i); input.parameters[i] = input.slots[4 + i]; }
    input.line = 1;
    input.pad[0] = static_cast<opxy::i8>(space);
    input.pad[1] = static_cast<opxy::i8>(feel);
    input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = opxy::draw::envelope_unavailable;
    galileo::display_reset(state);
}
unsigned changed() {
    unsigned n = 0;
    for (unsigned i = 0; i < Frame::node_count; ++i) n += std::memcmp(&frame.nodes[i], &before.nodes[i], sizeof(frame.nodes[i])) != 0;
    for (unsigned p = 0; p < Frame::path_count; ++p)
        n += frame.paths[p].key != before.paths[p].key && !std::memcmp(&frame.nodes[p], &before.nodes[p], sizeof(frame.nodes[p]));
    return n;
}
// One frame at fps; returns the nodes it changed.
unsigned step(double fps = 60) {
    before = frame;
    input.elapsed = static_cast<opxy::i32>(1024 / fps + 0.5);
    galileo::display(state, input, frame);
    return changed();
}
void note() { ++input.note_serial; input.held_notes = 1; }

// The device's rules for a reshaping outline (the SDK's docs/display.md), and
// no turn sharper than SHARPEST degrees: a turn near straight back draws a
// long mitre spike on the device.
const double SHARPEST = 150.0;
void check_outlines(const char* what) {
    CHECK(opxy::draw::valid(frame), "%s: invalid frame", what);
    const double limit = std::cos(SHARPEST * 3.14159265358979 / 180.0);
    for (unsigned p = 0; p < Frame::path_count; ++p) {
        const auto& path = frame.paths[p];
        long long area = 0;
        double closest = 1e9, sharpest = 1;
        for (unsigned i = 0; i < 64; ++i) {
            const auto& a = path.points[i];
            const auto& b = path.points[(i + 1) % 64];
            const auto& c = path.points[(i + 2) % 64];
            area += static_cast<long long>(a.x) * b.y - static_cast<long long>(b.x) * a.y;
            for (unsigned j = i + 1; j < 64; ++j) {
                const double dx = (path.points[j].x - a.x) / 1024.0, dy = (path.points[j].y - a.y) / 1024.0;
                const double d = dx * dx + dy * dy;
                if (d < closest) closest = d;
            }
            const double ux = b.x - a.x, uy = b.y - a.y, vx = c.x - b.x, vy = c.y - b.y;
            const double lu = std::sqrt(ux * ux + uy * uy), lv = std::sqrt(vx * vx + vy * vy);
            if (lu > 0 && lv > 0) { const double k = (ux * vx + uy * vy) / (lu * lv); if (k < sharpest) sharpest = k; }
        }
        const auto& first = path.points[0];
        const auto& last = path.points[63];
        const double cx = (first.x - last.x) / 1024.0, cy = (first.y - last.y) / 1024.0;
        CHECK(closest >= 1.0 / 64, "%s: path %u has points %.3f px apart", what, p, closest);
        CHECK(cx * cx + cy * cy >= 1.0 / 16, "%s: path %u closes over %.3f px", what, p, cx * cx + cy * cy);
        CHECK(area != 0, "%s: path %u encloses no area", what, p);
        CHECK(sharpest >= limit, "%s: path %u turns sharper than %.0f degrees", what, p, SHARPEST);
    }
}
// Where the first ball to roll is drawn (one ball: slot 0), Q10.
int ball_x() { return frame.nodes[galileo::art::ball0].x; }
int ball_y() { return frame.nodes[galileo::art::ball0].y; }
bool shown(unsigned n) { return frame.nodes[n].x != opxy::draw::hidden_x * 1024; }
unsigned count_shown(unsigned first, unsigned count, unsigned stride) {
    unsigned k = 0;
    for (unsigned i = 0; i < count; ++i) k += shown(first + i * stride);
    return k;
}

// Advance widths at 20 px, in half pixels, for ASCII 32..126 (Univers Next Pro
// Light, measured with the suite's tools/caption_width.py and rounded up).
const unsigned char advance[95] = {12, 13, 15, 23, 23, 37, 28, 9, 12, 12, 18, 28, 12, 14, 12, 12, 23, 23, 23, 23, 23, 23, 23,
    23, 23, 23, 12, 12, 27, 28, 27, 20, 40, 28, 27, 27, 30, 25, 23, 29, 30, 11, 20, 25, 23, 38, 31, 30, 25, 30, 27, 25, 22, 30,
    26, 41, 26, 25, 23, 12, 12, 12, 27, 21, 12, 22, 24, 21, 24, 22, 13, 24, 23, 10, 10, 21, 10, 36, 23, 23, 24, 24, 14, 20, 13,
    23, 21, 33, 21, 21, 18, 16, 11, 16, 27};
double width20(const char* s) {
    unsigned w = 0;
    for (; *s; ++s) w += (*s >= 32 && *s < 127) ? advance[*s - 32] : 40;
    return w / 2.0;
}
double width10(const char* s) {
    unsigned w = 0;
    for (; *s; ++s) { char c = *s; if (c >= 'A' && c <= 'Z') c += 32; w += (c >= 32 && c < 127) ? advance[c - 32] : 40; }
    return w / 4.0;
}
}

int main() {
    using namespace galileo;
    // Every model, every control at its ends, at rest and through a run.
    for (unsigned m = 0; m < kit::models; ++m) {
        unsigned char base[8];
        for (int i = 0; i < 8; ++i) base[i] = 64;
        for (unsigned k = 0; k < 24; ++k) if (kit::key_model[k] == m) { std::memcpy(base, kit::key_values[k], 8); break; }
        for (unsigned s = 0; s < 9; ++s)
            for (unsigned v = 0; v < 2; ++v) {
                unsigned char c[8];
                std::memcpy(c, base, 8);
                if (s < 8) c[s] = v ? 127 : 0;
                else std::memset(c, v ? 127 : 0, 8);
                char what[64];
                std::snprintf(what, sizeof(what), "model %u slot %u at %u", m, s, v ? 127 : 0);
                begin(m, c, v ? 15 : -16, v ? 4 : 0);
                step();
                check_outlines(what);
                note();
                for (int f = 0; f < 90; ++f) { step(30); if (f % 15 == 0) check_outlines(what); }
            }
    }
    // Churn: the default keys and the heaviest motion, at 30 and 60 frames a second.
    {
        double worst_mean = 0;
        unsigned worst_peak = 0;
        for (unsigned fps = 30; fps <= 60; fps += 30) {
            for (unsigned k = 0; k < 24 + 2; ++k) {
                unsigned m = 1;
                unsigned char c[8] = {57, 76, 44, 64, 120, 127, 127, 127};   // 16 frets at 1/64, 24 balls, full bounce
                if (k < 24) { m = kit::key_model[k]; std::memcpy(c, kit::key_values[k], 8); }
                if (k == 25) { m = 13; c[0] = 64; c[4] = 24; c[5] = 8; }     // a shaker of 24 balls
                begin(m, c);
                for (int f = 0; f < 10; ++f) step(fps);
                note();
                unsigned total = 0, moving = 0, peak = 0;
                for (unsigned f = 0; f < 4 * fps; ++f) {
                    const unsigned n = step(fps);
                    if (n) { total += n; ++moving; }
                    if (n > peak) peak = n;
                }
                input.held_notes = 0;
                unsigned rest = 0;
                for (unsigned f = 0; f < 8 * fps; ++f) rest = step(fps);   // the longest runs have ended
                const double mean = moving ? static_cast<double>(total) / moving : 0;
                if (mean > worst_mean) worst_mean = mean;
                if (peak > worst_peak) worst_peak = peak;
                CHECK(mean <= 20, "key %u at %u fps changes %.1f nodes a frame while it runs", k, fps, mean);
                CHECK(peak <= 30, "key %u at %u fps changes %u nodes in one frame", k, fps, peak);
                CHECK(rest == 0, "key %u at %u fps still changes %u nodes at rest", k, fps, rest);
            }
        }
        std::printf("churn: worst mean %.1f, worst peak %u nodes a frame\n", worst_mean, worst_peak);
    }
    // Turning any control a step a frame, from 0 to 127 and back, from three
    // bases: at most 30 nodes change in a frame, and none once it stops.
    {
        unsigned worst = 0;
        const unsigned char bases[3][8] = {{0, 0, 0, 0, 0, 0, 0, 0}, {64, 64, 64, 64, 64, 64, 64, 64}, {127, 127, 127, 127, 127, 127, 127, 127}};
        for (unsigned b = 0; b < 3; ++b)
            for (unsigned s = 0; s < 8; ++s) {
                unsigned char c[8];
                std::memcpy(c, bases[b], 8);
                begin(1, c);
                step();
                for (int v = 0; v < 256; ++v) {
                    const int raw = v < 128 ? v : 255 - v;
                    input.slots[s] = raw >= 127 ? 0x7fffffff : raw << 24;
                    if (s >= 4) input.parameters[s - 4] = input.slots[s];
                    const unsigned n = step();
                    if (n > worst) worst = n;
                    CHECK(n <= 30, "slot %u stepped to %d changes %u nodes in a frame", s, raw, n);
                }
                step();
                const unsigned rest = step();
                CHECK(rest == 0, "slot %u still changes %u nodes once it stops", s, rest);
            }
        std::printf("controls: at most %u nodes a frame turning one control a step\n", worst);
    }
    // The run plays out: a short amp envelope and a note let go at 0.1 s
    // change nothing, as a kit's voice ignores both.
    {
        unsigned char roll[8] = {57, 76, 44, 64, 88, 64, 70, 0};   // eight 1/32s and a bounce: about 0.7 s
        int free_x[3], cut_x[3];
        for (int pass = 0; pass < 2; ++pass) {
            begin(1, roll);
            if (pass) { input.amp.attack = 0; input.amp.decay = 0x08000000; input.amp.sustain = 0; input.amp.release = 0x7f000000; }
            step();
            note();
            int k = 0;
            for (int f = 1; f <= 45; ++f) {
                if (f == 6 && pass) { input.held_notes = 0; ++input.note_off_serial; }
                step();
                if (f == 15 || f == 30 || f == 45) (pass ? cut_x : free_x)[k++] = ball_x();
            }
        }
        for (int k = 0; k < 3; ++k) CHECK(free_x[k] == cut_x[k], "the amp envelope or the gate moves the ball (frame %d: %d, %d)", 15 * (k + 1), free_x[k], cut_x[k]);
    }
    // A hit whose sound lands at once (no frets) still shows the ball roll:
    // from the head down the board over 0.1 s, faster each frame, then onto
    // the drum, and back to the head once it rests. With reduced motion it is
    // on the head of the drum at once.
    {
        unsigned char hit[8] = {57, 76, 44, 64, 88, 0, 0, 0};
        begin(1, hit);
        step();
        const int rest_x = ball_x(), rest_y = ball_y();
        note();
        int xs[12];
        for (int f = 0; f < 12; ++f) { step(); xs[f] = ball_x(); }
        CHECK(xs[0] == rest_x, "the ball leaves the head before the note's first frame (%d, %d)", xs[0], rest_x);
        for (int f = 1; f < 5; ++f) CHECK(xs[f] > xs[f - 1], "the ball doesn't roll at frame %d (%d, %d)", f, xs[f - 1], xs[f]);
        for (int f = 2; f < 5; ++f) CHECK(xs[f] - xs[f - 1] > xs[f - 1] - xs[f - 2], "the roll doesn't speed up at frame %d", f);
        CHECK(xs[8] == xs[11] && xs[11] > rest_x + 100 * 1024, "the ball hasn't come to the drum by 0.15 s (%d, %d)", xs[8], xs[11]);
        for (int f = 0; f < 120; ++f) step();
        CHECK(ball_x() == rest_x && ball_y() == rest_y, "the ball isn't back at the head after its run");
        begin(1, hit);
        input.reduced_motion = 1;
        step();
        note();
        step();
        CHECK(ball_x() == xs[11], "with reduced motion the ball isn't on the drum at once (%d, %d)", ball_x(), xs[11]);
    }
    // The controls show: TILT one to four risers, FRETS none to nine bells,
    // BALLS one to six, the second control one to six wires, the first the
    // drum's size (one copy shown).
    {
        using namespace galileo::art;
        unsigned char lo[8] = {0, 0, 0, 0, 0, 0, 0, 0}, hi[8] = {127, 127, 127, 127, 127, 127, 127, 127};
        begin(1, lo);
        step();
        CHECK(count_shown(riser1_left, 3, riser_nodes) == 0 && count_shown(bell0_hanger, bells, bell_nodes) == 0 &&
              count_shown(ball5, balls, ball_nodes) == 1 && count_shown(drum0_wire0, wires, 1) == 1 && shown(drum0_head) && !shown(drum6_head),
              "every control at 0 doesn't show one riser, no bells, one ball, one wire and the largest drum");
        begin(1, hi);
        step();
        CHECK(count_shown(riser1_left, 3, riser_nodes) == 3 && count_shown(bell0_hanger, bells, bell_nodes) == bells &&
              count_shown(ball5, balls, ball_nodes) == balls && count_shown(drum6_wire0, wires, 1) == wires && shown(drum6_head) && !shown(drum0_head),
              "every control at 127 doesn't show four risers, nine bells, six balls, six wires and the smallest drum");
    }
    // Shift's rows in the kit's own header.
    {
        unsigned char c[8] = {57, 76, 44, 64, 88, 0, 0, 0};
        begin(1, c);
        input.shift = 1;
        input.line = 0;
        input.choices[0] = 0;
        input.choices[2] = 2;
        input.pan = -40;
        input.level = -6;
        for (int i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(i);
        step();
        CHECK(!std::strcmp(frame.texts[4].chars, "snare"), "MACHINE reads '%s'", frame.texts[4].chars);
        CHECK(!std::strcmp(frame.texts[0].chars, "skin"), "MACHINE's title reads '%s', not the group", frame.texts[0].chars);
        CHECK(!std::strcmp(frame.texts[5].chars, "2"), "CHOKE reads '%s'", frame.texts[5].chars);
        CHECK(!std::strcmp(frame.texts[6].chars, "-40"), "PAN reads '%s'", frame.texts[6].chars);
        CHECK(!std::strcmp(frame.texts[7].chars, "-6dB"), "LEVEL reads '%s'", frame.texts[7].chars);
        // Both rows at once: the second row's Shift grey under the first.
        CHECK(!std::strcmp(frame.texts[8].chars, "space"), "the grey SPACE title reads '%s'", frame.texts[8].chars);
        CHECK(!std::strcmp(frame.texts[14].chars, "TRK"), "the grey FX I reads '%s'", frame.texts[14].chars);
        input.line = 1;
        input.pad[0] = -6; input.pad[1] = 9; input.pad[2] = 0; input.pad[3] = 4;
        for (int i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(4 + i);
        for (int f = 0; f < 20; ++f) step();
        CHECK(!std::strcmp(frame.texts[4].chars, "-6"), "SPACE reads '%s'", frame.texts[4].chars);
        CHECK(!std::strcmp(frame.texts[5].chars, "lax2"), "FEEL reads '%s'", frame.texts[5].chars);
        CHECK(!std::strcmp(frame.texts[6].chars, "TRK"), "FX I reads '%s'", frame.texts[6].chars);
        CHECK(!std::strcmp(frame.texts[7].chars, "50"), "FX II reads '%s'", frame.texts[7].chars);
        CHECK(!std::strcmp(frame.texts[0].chars, "space"), "SPACE's title reads '%s'", frame.texts[0].chars);
        // and the first row's grey over it
        CHECK(!std::strcmp(frame.texts[12].chars, "snare"), "the grey MACHINE reads '%s'", frame.texts[12].chars);
        CHECK(!std::strcmp(frame.texts[8].chars, "skin"), "the grey MACHINE's title reads '%s'", frame.texts[8].chars);
        CHECK(!std::strcmp(frame.texts[14].chars, "-40"), "the grey PAN reads '%s'", frame.texts[14].chars);
        CHECK(!std::strcmp(frame.texts[15].chars, "-6dB"), "the grey LEVEL reads '%s'", frame.texts[15].chars);
    }
    // Every caption fits: values in 53 px at 20 px (LEVEL, the last column, 55), titles in 56 px at 10 px.
    {
        char text[16];
        double widest = 0;
        for (unsigned m = 0; m < kit::models; ++m) {
            CHECK(width20(kit::model[m].name) <= 53, "MACHINE '%s' is %.1f px", kit::model[m].name, width20(kit::model[m].name));
            for (unsigned s = 0; s < 8; ++s) {
                CHECK(width10(kit::model[m].labels[s]) <= 56, "title '%s' is %.1f px", kit::model[m].labels[s], width10(kit::model[m].labels[s]));
                for (unsigned raw = 0; raw < 128; ++raw)
                    for (int feel = 0; feel < 16; feel += s == 4 ? 1 : 16) {
                        caption::value(text, opxy::draw::header_capacity, m, s, raw, feel);
                        const double w = width20(text);
                        if (w > widest) widest = w;
                        CHECK(w <= 53, "model %u slot %u at %u: '%s' is %.1f px", m, s, raw, text, w);
                    }
            }
        }
        for (int v = -16; v < 16; ++v) { caption::space(text, 16, v); CHECK(width20(text) <= 53, "SPACE '%s'", text); }
        for (int v = 0; v < 16; ++v) { caption::feel(text, 16, v); CHECK(width20(text) <= 53, "FEEL '%s'", text); }
        CHECK(width20("-30dB") <= 56, "LEVEL -30dB");   // 53.7 px measured; the rounded-up table adds up to 2.5
        for (unsigned r = 0; r < 2; ++r)
            for (unsigned c = 0; c < 4; ++c) CHECK(width10(view::shift_titles[r][c]) <= 56, "title '%s'", view::shift_titles[r][c]);
        std::printf("captions: widest value %.1f px\n", widest);
    }
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}

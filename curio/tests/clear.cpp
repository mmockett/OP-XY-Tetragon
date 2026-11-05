// Nothing enters the header or leaves the screen: every part of every
// specimen, moving or still, stays below the header's bottom edge (y 38,
// where the SDK's two-page header ends) and inside the screen's 480 x 220 at
// every control setting and through every hit. It also prints each
// specimen's extent at its defaults, at rest, and over every sweep, from which
// tools/make_panel.py centres it. Each node's outline
// (tests/bounds.h, from tools/make_panel.py; the reshaped outlines from the
// frame itself) is placed, scaled and turned as the device would, its stroke
// added, and its top checked against the line. Sweeps: each control through
// all 128 values with the rest at their defaults, hits along the way; every
// control at 0 or 100 in all 256 combinations, each through a whole hit; all
// at 50.
#include "../engine.cpp"
#include "../display.cpp"
#include "bounds.h"
#include <cstdio>
#include <cstring>

namespace {
const double header_bottom = 38.0, screen_w = 480.0, screen_h = 220.0;
const double stroke = 0.85;   // half the widest stroke drawn (1.66 px: the match head at its largest) and a little more
int failures = 0;
curio::DisplayState state;
curio::Frame frame;
double worst[16];
int worst_node[16];
char worst_what[16][96];
double reach[16][4];      // left, top, right, bottom over everything
double rest[16][4];       // at the defaults, at rest
bool at_rest = false;

// Node n's box as drawn, px (left, top, right, bottom); false when hidden.
bool box_of(const curio::Frame& f, unsigned n, double* box) {
    const opxy::draw::Node& d = f.nodes[n];
    if (d.x >= opxy::draw::hidden_x * 1024) return false;
    const double k = d.scale / 1024.0, c = d.cos / 1024.0, s = d.sin / 1024.0;
    double l, t, r, b;
    if (n < curio::Frame::path_count) {
        l = t = 1e9;
        r = b = -1e9;
        for (int i = 0; i < 64; ++i) {
            const double x = f.paths[n].points[i].x / 1024.0, y = f.paths[n].points[i].y / 1024.0;
            l = x < l ? x : l; r = x > r ? x : r; t = y < t ? y : t; b = y > b ? y : b;
        }
    } else {
        l = curio::bounds::node[n][0]; t = curio::bounds::node[n][1];
        r = curio::bounds::node[n][2]; b = curio::bounds::node[n][3];
    }
    // The four corners turned and scaled.
    box[0] = box[1] = 1e9;
    box[2] = box[3] = -1e9;
    const double xs[2] = {l, r}, ys[2] = {t, b};
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            const double x = d.x / 1024.0 + k * (c * xs[i] - s * ys[j]);
            const double y = d.y / 1024.0 + k * (s * xs[i] + c * ys[j]);
            box[0] = x < box[0] ? x : box[0]; box[2] = x > box[2] ? x : box[2];
            box[1] = y < box[1] ? y : box[1]; box[3] = y > box[3] ? y : box[3];
        }
    box[0] -= stroke; box[1] -= stroke; box[2] += stroke; box[3] += stroke;
    return true;
}
void grow(double* into, const double* box) {
    into[0] = box[0] < into[0] ? box[0] : into[0]; into[1] = box[1] < into[1] ? box[1] : into[1];
    into[2] = box[2] > into[2] ? box[2] : into[2]; into[3] = box[3] > into[3] ? box[3] : into[3];
}
void check(opxy::u32 m, const char* what) {
    for (unsigned n = 0; n < curio::art::header_node; ++n) {
        double box[4];
        if (!box_of(frame, n, box)) continue;
        grow(reach[m], box);
        if (at_rest) grow(rest[m], box);
        const double t = box[1];
        if (box[0] < 0 || box[2] > screen_w || box[3] > screen_h) {
            std::printf("FAIL %s's node %u leaves the screen (%.1f, %.1f to %.1f, %.1f): %s\n", curio::kit::model[m].name, n, box[0], box[1], box[2], box[3], what);
            ++failures;
        }
        if (t < worst[m]) {
            worst[m] = t;
            worst_node[m] = static_cast<int>(n);
            std::snprintf(worst_what[m], sizeof(worst_what[m]), "%s", what);
        }
    }
}
opxy::draw::Input input_for(opxy::u32 model, const opxy::u8* raw) {
    opxy::draw::Input in = opxy::draw::Input();
    in.model = model;
    for (int c = 0; c < 8; ++c) in.slots[c] = raw[c] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(raw[c]) << 24;
    in.track = 1;
    in.elapsed = 17;
    in.amp.attack = in.amp.decay = in.amp.sustain = in.amp.release = opxy::draw::envelope_unavailable;
    return in;
}
// At rest, then (if asked) through a whole hit.
void run(opxy::u32 m, const opxy::u8* raw, bool hit, const char* label) {
    opxy::draw::Input in = input_for(m, raw);
    display_reset(state);
    display(state, in, frame);
    check(m, label);
    if (!hit) return;
    in.note_serial = 1;
    char what[96];
    for (int f = 0; f < 600; ++f) {
        in.held_notes = f < 6 ? 1 : 0;
        display(state, in, frame);
        if (state.age < 0) break;
        std::snprintf(what, sizeof(what), "%s, %d ms into a hit", label, f * 16);
        check(m, what);
    }
}
}

int main() {
    using namespace curio;
    for (opxy::u32 m = 0; m < kit::models; ++m) {
        worst[m] = 1e9;
        reach[m][0] = reach[m][1] = rest[m][0] = rest[m][1] = 1e9;
        reach[m][2] = reach[m][3] = rest[m][2] = rest[m][3] = -1e9;
        char label[96];
        opxy::u8 raw[8];
        at_rest = true;
        run(m, kit::model[m].defaults, false, "the defaults, at rest");
        at_rest = false;
        for (int c = 0; c < 8; ++c)
            for (int v = 0; v < 128; ++v) {
                std::memcpy(raw, kit::model[m].defaults, 8);
                raw[c] = static_cast<opxy::u8>(v);
                std::snprintf(label, sizeof(label), "control %d at %d", c + 1, v);
                run(m, raw, v % 16 == 0 || v == 127, label);
            }
        for (int bits = 0; bits < 256; ++bits) {
            for (int c = 0; c < 8; ++c) raw[c] = (bits >> c) & 1 ? 127 : 0;
            std::snprintf(label, sizeof(label), "controls %d%d%d%d%d%d%d%d (0 or 100)", bits & 1, bits >> 1 & 1, bits >> 2 & 1,
                          bits >> 3 & 1, bits >> 4 & 1, bits >> 5 & 1, bits >> 6 & 1, bits >> 7 & 1);
            run(m, raw, true, label);
        }
        for (int c = 0; c < 8; ++c) raw[c] = 64;
        run(m, raw, true, "every control at 50");
        if (worst[m] < header_bottom) {
            std::printf("FAIL %s's node %d reaches y %.1f, above the header's bottom (%.0f): %s\n", kit::model[m].name, worst_node[m],
                        worst[m], header_bottom, worst_what[m]);
            ++failures;
        }
    }
    std::printf("clear: each specimen's extent, px (left top right bottom): at its defaults at rest | over every sweep\n");
    for (int m = 0; m < 16; ++m)
        std::printf("  %-6s %6.1f %6.1f %6.1f %6.1f | %6.1f %6.1f %6.1f %6.1f\n", kit::model[m].name, rest[m][0], rest[m][1], rest[m][2], rest[m][3],
                    reach[m][0], reach[m][1], reach[m][2], reach[m][3]);
    double least = 1e9;
    int at = 0;
    for (int m = 0; m < 16; ++m)
        if (worst[m] < least) { least = worst[m]; at = m; }
    std::printf("clear: every specimen stays below the header and on the screen; the highest reach is %s's, at y %.1f\n", kit::model[at].name, least);
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}

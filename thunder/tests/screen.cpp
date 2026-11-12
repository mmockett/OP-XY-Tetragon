// THUNDER's screen checks, run by sdk.py test. The screen runs on the host
// for every model, each control at five settings, through a hit, and fails on
// any frame the OP-XY would draw wrongly beyond what opxy::draw::valid
// checks: a shown outline draws streaks or slivers on the device when it has
// no area (traced out and back, or turning straight back), when neighbouring
// points lie within an eighth of a pixel, or when its closing edge is under a
// quarter of a pixel. It also checks how many nodes change a frame while a
// note sounds (the device slows past about 20) and that a still screen
// changes none; that every caption fits its column in the OP-XY's font; the
// Shift rows; and that the track's amp envelope moves nothing (stock never
// applies it to a kit's voices, which play to their own end).
#include "../display.cpp"
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {
using namespace opxy::draw;
typedef thunder::Frame F;
const opxy::i32 apart = unit / 8;   // path units (Q10): the outlines are drawn at twice this on screen
int failures = 0;
const unsigned char defaults[12][8] = {
    {52, 92, 18, 10, 110, 70, 58, 0}, {64, 84, 74, 40, 96, 80, 44, 40}, {64, 80, 64, 60, 64, 12, 0, 15},
    {64, 84, 80, 20, 40, 0, 0, 0}, {52, 92, 42, 30, 100, 44, 0, 0}, {50, 16, 96, 90, 10, 50, 30, 0},
    {56, 92, 90, 40, 96, 30, 0, 0}, {74, 70, 70, 84, 40, 20, 0, 40}, {60, 70, 96, 50, 50, 50, 20, 0},
    {64, 66, 44, 40, 60, 30, 0, 40}, {56, 50, 40, 30, 50, 20, 0, 30}, {50, 50, 56, 60, 30, 20, 60, 64}};

void fail_path(const char* when, const char* what, unsigned p, long a, long b) {
    std::printf("%s: path %u %s (%ld, %ld)\n", when, p, what, a, b);
    ++failures;
}
void check_paths(const F& f, const char* when) {
    for (unsigned p = 0; p < F::path_count; ++p) {
        if (f.nodes[p].x >= hidden_x * unit) continue;
        const Point* q = f.paths[p].points;
        const unsigned n = F::point_count;
        for (unsigned i = 0; i < n; ++i) {
            const Point& a = q[i];
            const Point& b = q[(i + 1) % n];
            const long dx = b.x - a.x, dy = b.y - a.y;
            const long size = dx * dx + dy * dy;
            if (i + 1 < n && dx < apart && dx > -apart && dy < apart && dy > -apart) fail_path(when, "points merge at", p, i, i + 1);
            if (i + 1 == n && size < 4L * apart * apart) fail_path(when, "closes over", p, dx, dy);
            const Point& c = q[(i + 2) % n];
            const long ex = c.x - b.x, ey = c.y - b.y;
            const long cross = dx * ey - dy * ex, dot = dx * ex + dy * ey, next = ex * ex + ey * ey;
            if (dot < 0 && size && next) {
                const double c2 = static_cast<double>(cross) * cross, s = static_cast<double>(size) * next;
                if (c2 < 0.003 * s) fail_path(when, "turns straight back at point", p, (i + 1) % n, 0);
            }
        }
        double area = 0;
        for (unsigned i = 0; i < n; ++i) {
            const Point& a = q[i];
            const Point& b = q[(i + 1) % n];
            area += (static_cast<double>(a.x) * b.y - static_cast<double>(b.x) * a.y) / 2;
        }
        area /= static_cast<double>(unit) * unit;
        if (area < 4 && area > -4) fail_path(when, "has almost no area (square path units)", p, static_cast<long>(area * 1000), 0);
    }
}

// Univers Next Pro Light's advance widths for printable ASCII (32 to 126), in
// thousandths of an em: the OP-XY lays out its header text by them.
const int advance[95] = {
    280, 320, 358, 560, 560, 915, 693, 207, 292, 292, 439, 676, 280, 329, 280, 293, 560, 560, 560, 560, 560, 560, 560, 560,
    560, 560, 280, 280, 675, 676, 675, 485, 983, 693, 656, 664, 728, 602, 555, 715, 742, 275, 494, 621, 557, 944, 751, 745,
    602, 745, 659, 617, 546, 744, 650, 1020, 638, 613, 565, 291, 293, 291, 675, 514, 293, 536, 581, 522, 581, 532, 318, 589,
    574, 235, 235, 513, 235, 894, 574, 571, 581, 581, 345, 480, 325, 574, 512, 820, 506, 507, 445, 381, 251, 381, 675};
double width(const char* text, int px) {
    int total = 0;
    for (const char* c = text; *c; ++c) {
        if (*c < 32 || *c > 126) return -1;
        total += advance[*c - 32];
    }
    return total * px / 1000.0;
}

thunder::DisplayState state;
F frame, before;
Input input;
void set(unsigned m, const int* c, unsigned row) {
    std::memset(&input, 0, sizeof input);
    input.track = 1;
    input.model = m;
    for (int i = 0; i < 8; ++i) input.slots[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
    for (unsigned i = 0; i < 4; ++i) {
        input.shown[i] = static_cast<opxy::u8>(row * 4 + i);
        input.parameters[i] = input.slots[row * 4 + i];
    }
    input.line = static_cast<opxy::u8>(row);
    input.elapsed = 34;   // 30 frames a second
    input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = envelope_unavailable;
}
// Nodes and outlines that changed since the last frame.
unsigned changed() {
    unsigned n = 0;
    for (unsigned i = 0; i < F::node_count; ++i)
        if (std::memcmp(&frame.nodes[i], &before.nodes[i], sizeof(Node))) ++n;
    for (unsigned p = 0; p < F::path_count; ++p)
        if (frame.paths[p].key != before.paths[p].key) ++n;
    before = frame;
    return n;
}
// A hit at 30 frames a second: the frames it moves for, and the changed nodes
// a frame on average over them (the header's own text excluded).
unsigned peak = 0;   // the most nodes any one frame changes
void hit(unsigned frames, unsigned& moving, double& churn, const char* when, bool check) {
    thunder::display_reset(state);
    std::memset(&frame, 0, sizeof frame);
    thunder::display(state, input, frame);
    before = frame;
    // at rest nothing changes
    for (int k = 0; k < 3; ++k) {
        thunder::display(state, input, frame);
        if (changed()) { std::printf("%s: the screen moves at rest\n", when); ++failures; break; }
    }
    input.note_serial += 1;
    input.held_notes = 1;
    unsigned total = 0;
    moving = 0;
    for (unsigned t = 0; t < frames; ++t) {
        if (t == 4) { input.held_notes = 0; input.note_off_serial += 1; }
        thunder::display(state, input, frame);
        if (!valid(frame)) { std::printf("%s frame %u: invalid\n", when, t); ++failures; }
        if (check) check_paths(frame, when);
        const unsigned n = changed();
        if (n) { total += n; moving = t + 1; }
        if (check && n > peak) peak = n;
    }
    churn = moving ? static_cast<double>(total) / moving : 0;
}
}

int main() {
    // captions: every model's every control at every value, with TUNE; the
    // Shift rows' captions; every title
    std::set<std::string> values, titles;
    for (unsigned m = 0; m < 12; ++m) {
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = defaults[m][i];
        for (unsigned s = 0; s < 8; ++s)
            for (int v = 0; v <= 127; ++v)
                for (int tune = -16; tune <= 15; tune += (s == 0 ? 1 : 31)) {
                    c[s] = v;
                    set(m, c, s / 4);
                    input.pad[0] = static_cast<opxy::i8>(tune);
                    thunder::display_reset(state);
                    thunder::display(state, input, frame);
                    for (int i = 0; i < 4; ++i) {
                        values.insert(frame.header[i]);
                        titles.insert(frame.texts[i].chars);
                        titles.insert(frame.texts[8 + i].chars);
                    }
                    c[s] = defaults[m][s];
                }
        for (unsigned row = 0; row < 2; ++row)
            for (int k = 0; k < 64; ++k) {
                set(m, c, row);
                input.shift = 1;
                input.pan = static_cast<opxy::i8>(k * 200 / 63 - 100);
                input.level = static_cast<opxy::i8>(-(k * 30 / 63));
                input.choices[1] = static_cast<opxy::u8>(k & 3);
                input.pad[0] = static_cast<opxy::i8>(k / 2 - 16);
                input.pad[1] = static_cast<opxy::i8>(k & 15);
                input.pad[2] = static_cast<opxy::i8>(k & 7);
                input.pad[3] = static_cast<opxy::i8>((k >> 3) & 7);
                thunder::display_reset(state);
                thunder::display(state, input, frame);
                for (int i = 0; i < 4; ++i) {
                    values.insert(frame.header[i]);
                    titles.insert(frame.texts[i].chars);
                }
            }
    }
    for (const std::string& v : values) {
        const double w = width(v.c_str(), 20);
        if (w < 0 || w > 53.8) { std::printf("caption '%s' is %.1f px wide at 20 px (at most 53)\n", v.c_str(), w); ++failures; }
    }
    for (const std::string& t : titles) {
        const double w = width(t.c_str(), 10);
        if (w < 0 || w > 56) { std::printf("title '%s' is %.1f px wide at 10 px (at most 56)\n", t.c_str(), w); ++failures; }
    }
    std::printf("%u captions and %u titles measured\n", static_cast<unsigned>(values.size()), static_cast<unsigned>(titles.size()));

    // the Shift rows, as MD KIT shows them
    {
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = defaults[0][i];
        set(0, c, 0);
        input.shift = 1;
        input.pan = -40;
        input.level = -6;
        input.choices[1] = 1;
        thunder::display_reset(state);
        thunder::display(state, input, frame);
        const char* want[4] = {"boom", "1", "-40", "-6dB"};
        const char* title[4] = {"machine", "choke", "pan", "level"};
        for (int i = 0; i < 4; ++i) {
            if (std::strcmp(frame.header[i], want[i])) { std::printf("FAIL shift row 1 shows '%s' for '%s'\n", frame.header[i], want[i]); ++failures; }
            if (std::strcmp(frame.texts[i].chars, title[i])) { std::printf("FAIL shift row 1 titled '%s' for '%s'\n", frame.texts[i].chars, title[i]); ++failures; }
        }
        // both rows at once: the second row's names grey under the first
        for (int i = 0; i < 4; ++i) {
            const char* grey[4] = {"tune", "drive", "fx i", "fx ii"};
            if (std::strcmp(frame.texts[8 + i].chars, grey[i])) { std::printf("FAIL shift row 2 grey under row 1 titled '%s' for '%s'\n", frame.texts[8 + i].chars, grey[i]); ++failures; }
        }
        set(0, c, 1);
        input.shift = 1;
        input.pan = -40;
        input.level = -6;
        input.choices[1] = 1;
        input.pad[0] = 7;
        input.pad[1] = 0;
        input.pad[2] = 0;
        input.pad[3] = 7;
        thunder::display_reset(state);
        thunder::display(state, input, frame);
        const char* want2[4] = {"+7", "OFF", "TRK", "100"};
        const char* title2[4] = {"tune", "drive", "fx i", "fx ii"};
        for (int i = 0; i < 4; ++i) {
            if (std::strcmp(frame.header[i], want2[i])) { std::printf("FAIL shift row 2 shows '%s' for '%s'\n", frame.header[i], want2[i]); ++failures; }
            if (std::strcmp(frame.texts[i].chars, title2[i])) { std::printf("FAIL shift row 2 titled '%s' for '%s'\n", frame.texts[i].chars, title2[i]); ++failures; }
            // and the first row grey over it
            if (std::strcmp(frame.texts[12 + i].chars, want[i])) { std::printf("FAIL shift row 1 grey shows '%s' for '%s'\n", frame.texts[12 + i].chars, want[i]); ++failures; }
            if (std::strcmp(frame.texts[8 + i].chars, title[i])) { std::printf("FAIL shift row 1 grey titled '%s' for '%s'\n", frame.texts[8 + i].chars, title[i]); ++failures; }
        }
    }

    // outlines, validity and churn: every model, each control at five
    // settings, a hit followed for four seconds
    double worst_churn = 0, sum_churn = 0;
    unsigned runs = 0;
    for (unsigned m = 0; m < 12; ++m)
        for (int s = -1; s < 8; ++s)
            for (int k = 0; k < (s < 0 ? 1 : 5); ++k) {
                int c[8];
                for (int i = 0; i < 8; ++i) c[i] = defaults[m][i];
                if (s >= 0) c[s] = k * 127 / 4;
                set(m, c, s >= 4 ? 1 : 0);
                char when[96];
                std::snprintf(when, sizeof when, "%s %s %d", thunder::view::labels[m][0] ? thunder::view::names[m] : "?", s >= 0 ? thunder::view::labels[m][s] : "defaults", c[s >= 0 ? s : 0]);
                unsigned moving;
                double churn;
                hit(120, moving, churn, when, true);
                if (churn > worst_churn) worst_churn = churn;
                sum_churn += churn;
                ++runs;
                if (churn > 25) { std::printf("%s: %.1f nodes change a frame while it moves\n", when, churn); ++failures; }
            }
    std::printf("churn: %.1f nodes a frame on average while a hit moves, %.1f at the worst setting, %u in the busiest frame\n", sum_churn / runs, worst_churn, peak);
    if (peak > 32) { std::printf("the busiest frame changes %u nodes (at most 32)\n", peak); ++failures; }

    // the amp envelope moves nothing: a kit voice plays to its own end
    // whatever M2 says, so a hit moves as long with any envelope as with none;
    // reduced motion keeps the sheet still
    {
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = defaults[0][i];
        unsigned plain, shorter, longer;
        double churn;
        set(0, c, 0);
        hit(150, plain, churn, "unknown envelope", false);
        set(0, c, 0);
        input.amp.attack = 0;
        input.amp.decay = 20 << 24;
        input.amp.sustain = 0;
        input.amp.release = 0x7f000000;
        hit(150, shorter, churn, "short envelope", false);
        set(0, c, 0);
        input.amp.attack = 0;
        input.amp.decay = 0x7fffffff;
        input.amp.sustain = 0x7fffffff;
        input.amp.release = 0;
        hit(150, longer, churn, "long envelope", false);
        std::printf("a hit moves for %u frames with no envelope, %u with a short one, %u with a long one\n", plain, shorter, longer);
        if (shorter != plain || longer != plain) { std::printf("FAIL the amp envelope changes a kit's motion\n"); ++failures; }
        set(0, c, 0);
        input.reduced_motion = 1;
        unsigned still;
        hit(60, still, churn, "reduced motion", false);
        if (still > 1) { std::printf("FAIL reduced motion still moves the sheet for %u frames\n", still); ++failures; }
    }
    // a change of the held notes without a new note (another key let go) moves nothing
    {
        int c[8];
        for (int i = 0; i < 8; ++i) c[i] = defaults[0][i];
        set(0, c, 0);
        thunder::display_reset(state);
        thunder::display(state, input, frame);
        before = frame;
        input.held_notes = 2;
        input.note_off_serial += 1;
        thunder::display(state, input, frame);
        if (changed()) { std::printf("FAIL another key's notes move the screen\n"); ++failures; }
    }
    if (failures) { std::printf("%d failures\n", failures); return 1; }
    return 0;
}

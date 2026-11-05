// Writes THUNDER's screen as JSON lines, one frame a line, for
// tools/make_sheet.py: the real display code. Not part of the machine.
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include -I. tools/frames.cpp -o frames
//   ./frames < scenes > frames.jsonl
// Each scene line: model c0..c7 line shift tune drive pan level fx1 fx2 hit_ms
// renders the screen at rest (hit_ms < 0) or hit_ms after a note.
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
using namespace opxy::draw;
thunder::DisplayState state;
thunder::Frame frame;

void dump(FILE* out) {
    std::fprintf(out, "{\"nodes\":[");
    for (unsigned i = 0; i < thunder::Frame::node_count; ++i) {
        const Node& n = frame.nodes[i];
        std::fprintf(out, "%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, unsigned(n.paint));
    }
    std::fprintf(out, "],\"paths\":[");
    for (unsigned p = 0; p < thunder::Frame::path_count; ++p) {
        std::fprintf(out, "%s[", p ? "," : "");
        for (unsigned k = 0; k < thunder::Frame::point_count; ++k)
            std::fprintf(out, "%s[%d,%d]", k ? "," : "", frame.paths[p].points[k].x, frame.paths[p].points[k].y);
        std::fprintf(out, "]");
    }
    std::fprintf(out, "],\"texts\":[");
    for (unsigned t = 0; t < thunder::Frame::text_count; ++t) {
        const Text& x = frame.texts[t];
        std::fprintf(out, "%s[%d,%d,%d,\"%s\"]", t ? "," : "", x.x, x.y, x.scale, x.chars);
    }
    std::fprintf(out, "],\"valid\":%d}\n", valid(frame) ? 1 : 0);
}
}

int main() {
    char line[512];
    while (std::fgets(line, sizeof line, stdin)) {
        int m, c[8], row, shift, tune, drive, pan, level, fx1, fx2, hit;
        const int got = std::sscanf(line, "%d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d", &m, &c[0], &c[1], &c[2], &c[3],
                                    &c[4], &c[5], &c[6], &c[7], &row, &shift, &tune, &drive, &pan, &level, &fx1, &fx2, &hit);
        if (got < 18) continue;
        std::memset(&state, 0, sizeof state);
        std::memset(&frame, 0, sizeof frame);
        Input input;
        std::memset(&input, 0, sizeof input);
        input.track = 1;
        input.model = static_cast<opxy::u32>(m);
        for (int i = 0; i < 8; ++i) input.slots[i] = c[i] >= 127 ? 0x7fffffff : c[i] << 24;
        for (int i = 0; i < 4; ++i) {
            input.shown[i] = static_cast<opxy::u8>(row * 4 + i);
            input.parameters[i] = input.slots[row * 4 + i];
        }
        input.line = static_cast<opxy::u8>(row);
        input.shift = static_cast<opxy::u32>(shift);
        input.choices[0] = static_cast<opxy::u8>(m);
        input.pan = static_cast<opxy::i8>(pan);
        input.level = static_cast<opxy::i8>(level);
        input.pad[0] = static_cast<opxy::i8>(tune);
        input.pad[1] = static_cast<opxy::i8>(drive);
        input.pad[2] = static_cast<opxy::i8>(fx1);
        input.pad[3] = static_cast<opxy::i8>(fx2);
        input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = envelope_unavailable;
        input.elapsed = 17;   // 60 frames a second
        thunder::display_reset(state);
        thunder::display(state, input, frame);
        if (hit >= 0) {
            input.note_serial = 1;
            input.held_notes = 1;
            const int frames = hit * 60 / 1000;
            for (int f = 0; f < frames; ++f) {
                thunder::display(state, input, frame);
                input.note_serial = 1;
            }
            thunder::display(state, input, frame);
        }
        dump(stdout);
    }
    return 0;
}

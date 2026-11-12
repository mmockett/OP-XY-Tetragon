// Prints the screen's frames for scenes read from stdin, one per line:
//   model c0..c7 shift line tune vary fx1 fx2 pan level choke hit_ms attack decay sustain release
// (controls 0..127; hit_ms 0 for the screen at rest, else the frame that long
// after a hit; amp positions 0..127, or -1 for an unknown envelope). Each
// frame is a line of JSON: nodes, paths and texts. tools/sheet.py draws them.
#include "../engine.cpp"
#include "../display.cpp"
#include <cstdio>

int main() {
    using namespace curio;
    static DisplayState state;
    static curio::Frame frame;
    int v[23];
    while (true) {
        for (int i = 0; i < 23; ++i)
            if (std::scanf("%d", &v[i]) != 1) return 0;
        opxy::draw::Input in = opxy::draw::Input();
        in.model = static_cast<opxy::u32>(v[0]);
        for (int i = 0; i < 8; ++i) in.slots[i] = v[1 + i] >= 127 ? 0x7fffffff : v[1 + i] << 24;
        in.shift = static_cast<opxy::u32>(v[9]);
        in.line = static_cast<opxy::u8>(v[10]);
        for (int i = 0; i < 4; ++i) in.shown[i] = static_cast<opxy::u8>(v[10] * 4 + i);
        in.pad[0] = static_cast<opxy::i8>(v[11]);
        in.pad[1] = static_cast<opxy::i8>(v[12]);
        // Scene lines give each send as its old step (0 TRK, 1..7); the display
        // takes the percent + 1 (SDK 1.15).
        in.pad[2] = static_cast<opxy::i8>(v[13] > 0 ? (((v[13] > 7 ? 7 : v[13]) - 1) * 100 + 3) / 6 + 1 : 0);
        in.pad[3] = static_cast<opxy::i8>(v[14] > 0 ? (((v[14] > 7 ? 7 : v[14]) - 1) * 100 + 3) / 6 + 1 : 0);
        in.pan = static_cast<opxy::i8>(v[15]);
        in.level = static_cast<opxy::i8>(v[16]);
        in.choices[0] = static_cast<opxy::u8>(v[0] / 4);
        in.choices[1] = static_cast<opxy::u8>(v[0] % 4);
        in.choices[2] = static_cast<opxy::u8>(v[17]);
        const int hit = v[18];
        if (v[19] < 0) in.amp.attack = in.amp.decay = in.amp.sustain = in.amp.release = opxy::draw::envelope_unavailable;
        else {
            in.amp.attack = v[19] << 24; in.amp.decay = v[20] << 24; in.amp.sustain = v[21] << 24; in.amp.release = v[22] << 24;
        }
        in.track = 1;
        in.held_notes = 0;
        in.elapsed = 16;
        display_reset(state);
        display(state, in, frame);
        if (hit > 0) {
            in.note_serial += 1;
            in.held_notes = 1;
            for (int t = 0; t < hit; t += 16) {
                if (t >= 100) in.held_notes = 0;
                display(state, in, frame);
            }
        } else display(state, in, frame);
        std::printf("{\"nodes\":[");
        for (unsigned i = 0; i < curio::Frame::node_count; ++i) {
            const opxy::draw::Node& n = frame.nodes[i];
            std::printf("%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, static_cast<unsigned>(n.paint));
        }
        std::printf("],\"paths\":[");
        for (unsigned p = 0; p < curio::Frame::path_count; ++p) {
            std::printf("%s[", p ? "," : "");
            for (unsigned i = 0; i < 64; ++i) std::printf("%s%d,%d", i ? "," : "", frame.paths[p].points[i].x, frame.paths[p].points[i].y);
            std::printf("]");
        }
        std::printf("],\"texts\":[");
        for (unsigned t = 0; t < curio::Frame::text_count; ++t) {
            const opxy::draw::Text& x = frame.texts[t];
            std::printf("%s[%d,%d,%d,\"", t ? "," : "", x.x, x.y, x.scale);
            for (const char* c = x.chars; *c; ++c) std::printf(*c == '"' || *c == '\\' ? "\\%c" : "%c", *c);
            std::printf("\"]");
        }
        std::printf("],\"valid\":%d}\n", opxy::draw::valid(frame) ? 1 : 0);
    }
}

// Host frames: runs Syahi's real display() for the states given on stdin and
// prints each final frame as one line of JSON (tools/sheet.py draws them).
// A state line: model c0..c7 shift line pan level pad0 pad1 pad2 pad3 choke
//               hit_ms human_serial
// hit_ms < 0: no hit; else the frame is drawn that long after a hit.
#include "../display.cpp"
#include <cstdio>

int main() {
    static syahi::DisplayState state;
    static syahi::Frame frame;
    long v[20];
    while (true) {
        int got = 0;
        for (int i = 0; i < 20; ++i) got += std::scanf("%ld", &v[i]) == 1;
        if (got < 20) break;
        frame = syahi::Frame();
        syahi::display_reset(state);
        opxy::draw::Input in = opxy::draw::Input();
        in.model = static_cast<opxy::u32>(v[0]);
        for (int i = 0; i < 8; ++i) in.slots[i] = v[1 + i] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(v[1 + i] << 24);
        for (int i = 0; i < 4; ++i) { in.shown[i] = static_cast<opxy::u8>(v[10] * 4 + i); in.parameters[i] = in.slots[v[10] * 4 + i]; }
        in.shift = static_cast<opxy::u32>(v[9]);
        in.line = static_cast<opxy::u8>(v[10]);
        in.pan = static_cast<opxy::i8>(v[11]);
        in.level = static_cast<opxy::i8>(v[12]);
        for (int i = 0; i < 4; ++i) in.pad[i] = static_cast<opxy::i8>(v[13 + i]);
        in.choices[0] = static_cast<opxy::u8>(v[0]);
        in.choices[1] = static_cast<opxy::u8>(v[17]);
        in.amp.attack = in.amp.decay = in.amp.sustain = in.amp.release = -1;
        in.note_serial = static_cast<opxy::u32>(v[19]);
        in.elapsed = 17;
        in.held_notes = 0;
        syahi::display(state, in, frame);   // the baseline
        if (v[18] >= 0) {
            in.note_serial += 1;
            in.held_notes = 1;
            long t = 0;
            syahi::display(state, in, frame);
            while (t + 17 <= v[18]) {
                t += 17;
                syahi::display(state, in, frame);
            }
        }
        std::printf("{\"valid\":%d,\"paths\":[", opxy::draw::valid(frame) ? 1 : 0);
        for (unsigned p = 0; p < syahi::Frame::path_count; ++p) {
            std::printf("%s{\"key\":%u,\"points\":[", p ? "," : "", frame.paths[p].key);
            for (unsigned i = 0; i < syahi::Frame::point_count; ++i)
                std::printf("%s[%d,%d]", i ? "," : "", frame.paths[p].points[i].x, frame.paths[p].points[i].y);
            std::printf("]}");
        }
        std::printf("],\"nodes\":[");
        for (unsigned n = 0; n < syahi::Frame::node_count; ++n) {
            const opxy::draw::Node& d = frame.nodes[n];
            std::printf("%s[%d,%d,%d,%d,%d,%u,%u]", n ? "," : "", d.x, d.y, d.scale, d.cos, d.sin, d.shade, d.paint);
        }
        std::printf("],\"texts\":[");
        for (unsigned t = 0; t < syahi::Frame::text_count; ++t) {
            std::printf("%s[%d,%d,%d,\"", t ? "," : "", frame.texts[t].x, frame.texts[t].y, frame.texts[t].scale);
            for (const char* c = frame.texts[t].chars; *c; ++c) std::printf(*c == '"' || *c == '\\' ? "\\%c" : "%c", *c);
            std::printf("\"]");
        }
        std::printf("]}\n");
    }
    return 0;
}

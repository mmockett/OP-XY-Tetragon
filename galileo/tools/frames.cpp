// Runs GALILEO's real display() on the host for the screen tools
// (tools/make_sheet.py, tools/check_screen.py). Not part of the machine.
//
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include -I. tools/frames.cpp tables.cpp -o frames
//   ./frames < script > frames.jsonl
//
// Script lines (one scene per "scene" line, the rest set its input):
//   scene NAME                     start a scene from a reset display
//   model M | slots c0..c7 | pad SPACE FEEL FX1 FX2 | shift S LINE | pan P | level L
//   amp A D S R (Q31 control positions, or "amp unknown") | held N
//   note                           a note on the selected key (its serial moves)
//   release                        the track's notes let go
//   run SECONDS FPS                frames at FPS for SECONDS; "emit" prints each
//   emit                           print the current frame (one JSON line)
//   churn SECONDS FPS              frames, printing only each frame's changed-node count
#include "../display.cpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
galileo::DisplayState state;
galileo::Frame frame, previous;
opxy::draw::Input input;
char name[64] = "scene";

void print_frame(const galileo::Frame& f, double t) {
    std::printf("{\"scene\":\"%s\",\"t\":%.4f,\"paths\":[", name, t);
    for (unsigned p = 0; p < galileo::Frame::path_count; ++p) {
        std::printf("%s[", p ? "," : "");
        for (unsigned i = 0; i < galileo::Frame::point_count; ++i)
            std::printf("%s[%d,%d]", i ? "," : "", f.paths[p].points[i].x, f.paths[p].points[i].y);
        std::printf("]");
    }
    std::printf("],\"keys\":[");
    for (unsigned p = 0; p < galileo::Frame::path_count; ++p) std::printf("%s%u", p ? "," : "", f.paths[p].key);
    std::printf("],\"nodes\":[");
    for (unsigned i = 0; i < galileo::Frame::node_count; ++i) {
        const opxy::draw::Node& n = f.nodes[i];
        std::printf("%s[%d,%d,%d,%d,%d,%u,%u]", i ? "," : "", n.x, n.y, n.scale, n.cos, n.sin, n.shade, static_cast<unsigned>(n.paint));
    }
    std::printf("],\"texts\":[");
    for (unsigned i = 0; i < galileo::Frame::text_count; ++i) {
        const opxy::draw::Text& t2 = f.texts[i];
        std::printf("%s[%d,%d,%d,\"", i ? "," : "", t2.x, t2.y, t2.scale);
        for (const char* c = t2.chars; *c; ++c) std::printf(*c == '"' || *c == '\\' ? "\\%c" : "%c", *c);
        std::printf("\"]");
    }
    std::printf("],\"valid\":%d}\n", opxy::draw::valid(f) ? 1 : 0);
}
// Nodes whose transform or paint changed, plus paths whose key changed.
unsigned changed(const galileo::Frame& a, const galileo::Frame& b) {
    unsigned n = 0;
    for (unsigned i = 0; i < galileo::Frame::node_count; ++i)
        if (std::memcmp(&a.nodes[i], &b.nodes[i], sizeof(a.nodes[i])) != 0) ++n;
    for (unsigned p = 0; p < galileo::Frame::path_count; ++p)
        if (a.paths[p].key != b.paths[p].key && std::memcmp(&a.nodes[p], &b.nodes[p], sizeof(a.nodes[p])) == 0) ++n;
    return n;
}
void step(double fps) {
    input.elapsed = static_cast<opxy::i32>(1024.0 / fps + 0.5);
    galileo::display(state, input, frame);
}
}

int main() {
    char word[32];
    double t = 0;
    while (std::scanf("%31s", word) == 1) {
        if (!std::strcmp(word, "scene")) {
            std::scanf("%63s", name);
            std::memset(&state, 0, sizeof(state));
            std::memset(&frame, 0, sizeof(frame));
            input = opxy::draw::Input();
            input.track = 1;
            input.held_notes = 0;
            input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = opxy::draw::envelope_unavailable;
            for (int i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(4 + i);   // the motion row
            galileo::display_reset(state);
            t = 0;
        } else if (!std::strcmp(word, "model")) {
            unsigned m; std::scanf("%u", &m); input.model = m;
        } else if (!std::strcmp(word, "slots")) {
            for (int i = 0; i < 8; ++i) { int v; std::scanf("%d", &v); input.slots[i] = v >= 127 ? 0x7fffffff : v << 24; }
            for (int i = 0; i < 4; ++i) input.parameters[i] = input.slots[input.shown[i]];
        } else if (!std::strcmp(word, "row")) {
            int r; std::scanf("%d", &r);
            for (int i = 0; i < 4; ++i) { input.shown[i] = static_cast<opxy::u8>(4 * r + i); input.parameters[i] = input.slots[4 * r + i]; }
            input.line = static_cast<opxy::u8>(r);
        } else if (!std::strcmp(word, "pad")) {
            for (int i = 0; i < 4; ++i) { int v; std::scanf("%d", &v); input.pad[i] = static_cast<opxy::i8>(v); }
        } else if (!std::strcmp(word, "shift")) {
            unsigned s, line; std::scanf("%u %u", &s, &line); input.shift = s; input.line = static_cast<opxy::u8>(line);
            for (int i = 0; i < 4; ++i) input.shown[i] = static_cast<opxy::u8>(4 * line + i);
        } else if (!std::strcmp(word, "choices")) {
            for (int i = 0; i < 4; ++i) { unsigned v; std::scanf("%u", &v); input.choices[i] = static_cast<opxy::u8>(v); }
        } else if (!std::strcmp(word, "pan")) {
            int v; std::scanf("%d", &v); input.pan = static_cast<opxy::i8>(v);
        } else if (!std::strcmp(word, "level")) {
            int v; std::scanf("%d", &v); input.level = static_cast<opxy::i8>(v);
        } else if (!std::strcmp(word, "amp")) {
            char a[16]; std::scanf("%15s", a);
            if (!std::strcmp(a, "unknown")) {
                input.amp.attack = input.amp.decay = input.amp.sustain = input.amp.release = opxy::draw::envelope_unavailable;
            } else {
                long d, s, r; std::scanf("%ld %ld %ld", &d, &s, &r);
                input.amp.attack = static_cast<opxy::i32>(std::atol(a)); input.amp.decay = static_cast<opxy::i32>(d);
                input.amp.sustain = static_cast<opxy::i32>(s); input.amp.release = static_cast<opxy::i32>(r);
            }
        } else if (!std::strcmp(word, "held")) {
            unsigned h; std::scanf("%u", &h); input.held_notes = h;
        } else if (!std::strcmp(word, "note")) {
            ++input.note_serial;
            if (input.held_notes == 0) input.held_notes = 1;
        } else if (!std::strcmp(word, "release")) {
            input.held_notes = 0;
            ++input.note_off_serial;
        } else if (!std::strcmp(word, "run")) {
            double seconds, fps; std::scanf("%lf %lf", &seconds, &fps);
            const int frames = static_cast<int>(seconds * fps + 0.5);
            for (int i = 0; i < frames; ++i) { step(fps); t += 1 / fps; }
        } else if (!std::strcmp(word, "emit")) {
            if (t == 0) step(60);
            print_frame(frame, t);
        } else if (!std::strcmp(word, "churn")) {
            double seconds, fps; std::scanf("%lf %lf", &seconds, &fps);
            const int frames = static_cast<int>(seconds * fps + 0.5);
            std::printf("{\"scene\":\"%s\",\"churn\":[", name);
            for (int i = 0; i < frames; ++i) {
                previous = frame;
                step(fps);
                t += 1 / fps;
                std::printf("%s%u", i ? "," : "", changed(previous, frame));
                if (!opxy::draw::valid(frame)) std::fprintf(stderr, "invalid frame in %s at %.3f\n", name, t);
            }
            std::printf("]}\n");
        }
    }
    return 0;
}

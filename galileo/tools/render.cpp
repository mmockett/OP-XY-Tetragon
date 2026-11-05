// Renders a score through GALILEO's real engine on the host, the way the SDK's
// drum adapter drives it (drum_voice.cpp.in): a pool of voices, one per key
// (chokeSameKey: a note on a key fades the key's last voice over 128 frames),
// drum_select, drum_pad and drum_transport before every render, and a voice
// freed once finished() says so. Not part of the machine.
//
//   c++ -std=c++11 -O2 -I<sdk>/sdk/include -I. tools/render.cpp tables.cpp -o render
//   ./render < score > out.raw     (interleaved stereo int32, Q31)
//
// Score lines: "voices N", "bpm X" (tenths of BPM to pass as the transport; 0
// passes none), "length SAMPLES", "stats" (print each note's peak and when its
// voice was freed to stderr), and
// "note AT KEY MODEL c0 .. c7 SPACE FEEL PAN" (PAN -100..100), and "level KEY DB"
// (the key's mixer level, -30..0 dB, as stock applies it after the voice).
#include "../engine.cpp"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>

namespace {
struct Note { long at; int key, model; int c[8]; int space, feel, pan; };
struct Slot {
    galileo::Voice voice;
    unsigned owner;
    int key, model, pan, note;
    int c[8];
    int space, feel;
    unsigned fade;
    bool choked;
    long started;
    long peak;
};
}

int main() {
    std::vector<Note> notes;
    long length = 0;
    int capacity = 16;
    unsigned bpm_x10 = 0;
    bool stats = false;
    std::vector<double> level(128, 1.0);
    char word[32];
    while (std::scanf("%31s", word) == 1) {
        if (!std::strcmp(word, "voices")) std::scanf("%d", &capacity);
        else if (!std::strcmp(word, "bpm")) std::scanf("%u", &bpm_x10);
        else if (!std::strcmp(word, "length")) std::scanf("%ld", &length);
        else if (!std::strcmp(word, "stats")) stats = true;
        else if (!std::strcmp(word, "level")) {
            int key = 0, db = 0;
            std::scanf("%d %d", &key, &db);
            if (key >= 0 && key < 128) level[key] = std::pow(10.0, db / 20.0);
        }
        else if (!std::strcmp(word, "note")) {
            Note n;
            std::scanf("%ld %d %d", &n.at, &n.key, &n.model);
            for (int i = 0; i < 8; ++i) std::scanf("%d", &n.c[i]);
            std::scanf("%d %d %d", &n.space, &n.feel, &n.pan);
            notes.push_back(n);
        }
    }
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) { return a.at < b.at; });
    static galileo::Shared shared;
    std::vector<Slot> slots(capacity);
    for (auto& s : slots) { std::memset(&s, 0, sizeof(s)); s.owner = 0; }
    std::vector<long> note_peak(notes.size(), 0), note_end(notes.size(), -1);
    unsigned next_owner = 1;
    size_t next = 0;
    const long blocks = (length + 63) / 64;
    std::vector<int> out(static_cast<size_t>(blocks) * 64 * 2, 0);
    long started = 0;
    for (long block = 0; block < blocks; ++block) {
        const long t0 = block * 64;
        // Notes start between blocks.
        while (next < notes.size() && notes[next].at < t0 + 64) {
            const Note& n = notes[next];
            Slot* slot = nullptr;
            for (auto& s : slots) if (!s.owner) { slot = &s; break; }
            if (!slot) {
                slot = &slots[0];
                for (auto& s : slots) if (s.fade && (!slot->fade || s.fade < slot->fade)) slot = &s;
                if (!slot->fade) for (auto& s : slots) if (s.started < slot->started) slot = &s;
                galileo::destroy(slot->voice, shared, slot->owner);
                slot->owner = 0;
            }
            std::memset(&slot->voice, 0, sizeof(slot->voice));
            slot->owner = next_owner++;
            galileo::initialize(slot->voice, shared, slot->owner);
            slot->key = n.key; slot->model = n.model; slot->pan = n.pan; slot->note = static_cast<int>(next);
            std::memcpy(slot->c, n.c, sizeof(n.c));
            slot->space = n.space; slot->feel = n.feel; slot->fade = 0; slot->choked = false; slot->started = ++started; slot->peak = 0;
            for (auto& s : slots)
                if (s.owner && &s != slot && s.key == n.key && !s.fade) { s.fade = 128; s.choked = true; }
            galileo::start(slot->voice, shared, slot->owner);
            ++next;
        }
        for (auto& s : slots) {
            if (!s.owner) continue;
            opxy::i32 live[8];
            for (int i = 0; i < 8; ++i) live[i] = s.c[i] >= 127 ? 0x7fffffff : s.c[i] << 24;
            opxy::u32 pitch[64];
            for (int i = 0; i < 64; ++i) pitch[i] = 25479004u;
            galileo::drum_select(s.voice, shared, s.owner, static_cast<opxy::u32>(s.model));
            const opxy::i32 pads[4] = {s.space, s.feel, 0, 0};
            galileo::drum_pad(s.voice, shared, s.owner, pads);
            if (bpm_x10) {
                const opxy::Transport tr = {bpm_x10, 0, true};
                galileo::drum_transport(s.voice, shared, s.owner, tr);
            }
            opxy::i32 buf[64];
            galileo::render(s.voice, shared, s.owner, buf, live, pitch);
            if (s.fade) {
                unsigned left = s.fade;
                for (int i = 0; i < 64; ++i) {
                    buf[i] = left ? opxy::q31::multiply(buf[i], static_cast<opxy::i32>((left - 1) << 24)) : 0;
                    if (left) --left;
                }
                s.fade = left;
            }
            // Stock pans a mono voice: left 2 (1 - p), right 2 p, p = 0.5 centred; here equal power.
            const double p = (s.pan + 100) / 200.0;
            const double lv = level[s.key & 127];
            const double gl = std::min(1.0, 2 * (1 - p)) * lv, gr = std::min(1.0, 2 * p) * lv;
            for (int i = 0; i < 64; ++i) {
                long a = std::labs(static_cast<long>(buf[i]));
                if (a > s.peak) s.peak = a;
                long l = out[(t0 + i) * 2] + static_cast<long>(buf[i] * gl);
                long r = out[(t0 + i) * 2 + 1] + static_cast<long>(buf[i] * gr);
                out[(t0 + i) * 2] = static_cast<int>(std::max(-2147483647L, std::min(2147483647L, l)));
                out[(t0 + i) * 2 + 1] = static_cast<int>(std::max(-2147483647L, std::min(2147483647L, r)));
            }
            if (s.peak > note_peak[s.note]) note_peak[s.note] = s.peak;
            if (galileo::finished(s.voice, shared) || (s.choked && !s.fade)) {
                note_end[s.note] = t0 + 64;
                galileo::destroy(s.voice, shared, s.owner);
                s.owner = 0;
            }
        }
    }
    std::fwrite(out.data(), sizeof(int), out.size(), stdout);
    if (stats)
        for (size_t i = 0; i < notes.size(); ++i)
            std::fprintf(stderr, "note %zu at %ld peak %.4f freed %ld\n", i, notes[i].at, note_peak[i] / 2147483648.0, note_end[i]);
    std::fprintf(stderr, "tempo p16 %u samples (%.1f BPM)\n", shared.learner.p16,
                 shared.learner.p16 ? 44100.0 * 60 / 4 / shared.learner.p16 : 0.0);
    return 0;
}

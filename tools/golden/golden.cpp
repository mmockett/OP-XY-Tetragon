// Golden renders and counted cycles for one Tetragon kit (tools/golden/golden.py).
//
// The kit's engine runs through a small copy of the SDK drum adapter's voice
// handling (drum_voice.cpp.in: handles, voiceCapacity slots, oldest-first
// stealing, the same-key choke and its 128-frame fade, pads decoded only for a
// key the kit set up, drum_transport once a block), driven by the schedule of
// the SDK's opxy-drum-callback-v1 benchmark (benchmark_drum.cpp.in): every model
// at 64, 0 and 127 with every control there and the pads at their top, the kit's
// own keys at full polyphony with neutral pads, two voices retriggering one key,
// and the heaviest model at full polyphony, a strike every 100 blocks.
//
// Host build (g++ or clang++): writes, per scenario, a hash of every block's
// raw int32 output (every voice, before stock's gain), so two builds can be
// compared bit for bit, and renders single hits for the compare report.
// Blackfin build (-DGOLDEN_BFIN, pinned bfin-elf-g++, run in the GDB simulator):
// reads CYCLES around each note start and render, as the benchmark does, and
// prints each run's mean and worst block.
//
// Compile-time kit description (golden.py passes it from definition.json):
//   GOLDEN_NS, GOLDEN_MODELS, GOLDEN_CAPACITY, GOLDEN_CHOKE, GOLDEN_PAD_COUNT,
//   GOLDEN_PAD_BITS (a brace list), GOLDEN_PAD_SIGNS, and for host runs
//   GOLDEN_HEAVY_MODEL and GOLDEN_HEAVY_LEVEL (the benchmark's level index:
//   0 = 64, 1 = 0, 2 = 127).
#include "engine.cpp"
#include <opxy/q31.hpp>
#include <opxy/transport.hpp>
#ifndef GOLDEN_BFIN
#include <cstdio>
#include <cstdlib>
#include <cstring>
#endif

#ifndef GOLDEN_BENCH_BLOCKS
#define GOLDEN_BENCH_BLOCKS 1500
#endif
#ifndef GOLDEN_SWEEP_BLOCKS
#define GOLDEN_SWEEP_BLOCKS 300
#endif
#ifndef GOLDEN_HEAVY_MODEL
#define GOLDEN_HEAVY_MODEL 0
#endif
#ifndef GOLDEN_HEAVY_LEVEL
#define GOLDEN_HEAVY_LEVEL 0
#endif

namespace golden {
using opxy::i32;
using opxy::u32;
using opxy::u8;
using Voice = GOLDEN_NS::Voice;
using Shared = GOLDEN_NS::Shared;
const u32 kFrames = opxy::block_frames;
const u32 kCapacity = GOLDEN_CAPACITY;
const u32 kModels = GOLDEN_MODELS;
const bool kChoke = GOLDEN_CHOKE != 0;
const u32 kPadCount = GOLDEN_PAD_COUNT;
const u8 kPadBits[] = GOLDEN_PAD_BITS;
const u32 kPadSigns = GOLDEN_PAD_SIGNS;
const u32 kHandles = 256, kChokeFrames = 128, kHit = 100;
const u8 kLevels[3] = {64, 0, 127};

// Optional engine callbacks, found as the adapter finds them.
namespace optional_finished {
struct Absent {};
template <class V, class S> Absent finished(const V&, const S&) { return Absent(); }
inline bool ended(Absent) { return false; }
template <class R> bool ended(R value) { return static_cast<bool>(value); }
}
namespace optional_pads {
struct Absent {};
template <class V, class S> Absent drum_pad(V&, S&, u32, const i32*) { return Absent(); }
}
namespace optional_transport {
struct Absent {};
template <class V, class S> Absent drum_transport(V&, S&, u32, const opxy::Transport&) { return Absent(); }
}

#ifdef GOLDEN_BFIN
inline u32 cycles() {
    u32 c;
    __asm__ __volatile__("%0 = CYCLES;" : "=d"(c));
    return c;
}
#endif

// What one voice plays: a model and its eight stored controls, with its pads
// (ready: a key the kit set up, so its pad word is decoded).
struct Payload {
    bool marked, ready;
    u8 model, stored[8];
    i32 pads[4];
};

struct Handle { u32 slot, key; };
struct Slot { Voice voice; u32 owner, started, pad, fade; };
Handle handles[kHandles];
Slot slots[kCapacity];
Shared shared;
u32 notes;

volatile i32 sink;
inline void zero(void* p, u32 bytes) {
    unsigned char* b = static_cast<unsigned char*>(p);
    for (u32 i = 0; i < bytes; ++i) b[i] = 0;
}
inline u32 owner(const Handle* h) { return static_cast<u32>(h - handles) + 1; }
inline Slot* held(const Handle* h) {
    return h->slot && slots[h->slot - 1].owner == owner(h) ? &slots[h->slot - 1] : nullptr;
}
inline void release(Slot* s) {
    GOLDEN_NS::destroy(s->voice, shared, s->owner);
    s->owner = 0;
    s->fade = 0;
}
inline bool sooner(const Slot& a, const Slot& b) {
    if (kChoke && (a.fade || b.fade)) return a.fade && (!b.fade || a.fade < b.fade);
    return notes - a.started > notes - b.started;
}
void reset() {
    zero(handles, sizeof handles);
    zero(slots, sizeof slots);
    zero(&shared, sizeof shared);
    notes = 0;
}
Handle* factory(u32 index) {
    Handle* h = &handles[index];
    if (Slot* s = held(h)) release(s);
    h->key = 53;
    h->slot = 0;
    return h;
}
void destroy(Handle* h) {
    if (Slot* s = held(h)) release(s);
    h->slot = 0;
}
// The adapter's start, on track 0 at octave 0, where key k plays key k.
void start(Handle* h, u32 key) {
    h->key = 53u + (key + 19u) % 24u;
    const u32 id = owner(h);
    Slot* s = held(h);
    if (!s) {
        s = &slots[0];
        for (u32 i = 0; i < kCapacity; ++i) {
            if (!slots[i].owner) { s = &slots[i]; break; }
            if (sooner(slots[i], *s)) s = &slots[i];
        }
        if (s->owner) release(s);
        zero(&s->voice, sizeof(Voice));
        s->owner = id;
        GOLDEN_NS::initialize(s->voice, shared, id);
        h->slot = static_cast<u32>(s - slots) + 1;
    }
    s->started = ++notes;
    s->pad = h->key + 1;
    s->fade = 0;
    if (kChoke)
        for (u32 i = 0; i < kCapacity; ++i)
            if (slots[i].owner && &slots[i] != s && slots[i].pad == s->pad && !slots[i].fade) slots[i].fade = kChokeFrames;
    GOLDEN_NS::start(s->voice, shared, id);
}
// The adapter's render: the kit's raw output goes to `raw` (what golden runs
// compare); `out` gets stock's view after the choke fade.
void render(Handle* h, const Payload& p, const opxy::Transport* time, i32* raw) {
    Slot* s = held(h);
    if (!s) {
        for (u32 i = 0; i < kFrames; ++i) raw[i] = 0;
        return;
    }
    u8 model, stored[8];
    GOLDEN_NS::drum_defaults(h->key, &model, stored);
    if (p.marked) {
        model = p.model;
        for (u32 i = 0; i < 8; ++i) stored[i] = p.stored[i];
    }
    if (model >= kModels) GOLDEN_NS::drum_defaults(h->key, &model, stored);
    i32 live[8];
    for (u32 i = 0; i < 8; ++i) live[i] = stored[i] == 127 ? 0x7fffffff : static_cast<i32>(static_cast<u32>(stored[i]) << 24);
    u32 pitch[kFrames];
    for (u32 i = 0; i < kFrames; ++i) pitch[i] = (25480118u >> 16) << 16;   // C4, as the benchmark plays
    GOLDEN_NS::drum_select(s->voice, shared, s->owner, model);
    if (kPadCount) {
        i32 pads[4];
        for (u32 i = 0; i < 4; ++i) pads[i] = p.marked && p.ready && i < kPadCount ? p.pads[i] : 0;
        using namespace optional_pads;
        drum_pad(s->voice, shared, s->owner, static_cast<const i32*>(pads));
    }
    {
        using namespace optional_transport;
        drum_transport(s->voice, shared, s->owner, time ? *time : opxy::Transport());
    }
    GOLDEN_NS::render(s->voice, shared, s->owner, raw, live, pitch);
    // Stock's amplitude (full) and, for a key set up, the pad's level cut, as
    // the benchmark's adapter applies them: counted, not compared.
    i32 out[kFrames];
    for (u32 i = 0; i < kFrames; ++i) out[i] = opxy::q31::multiply(raw[i], 0x7fffffff);
    if (p.marked && p.ready)
        for (u32 i = 0; i < kFrames; ++i) out[i] = opxy::q31::multiply(out[i], 0x40000000);
    if (kChoke && s->fade) {
        u32 left = s->fade;
        for (u32 i = 0; i < kFrames; ++i) {
            out[i] = left ? opxy::q31::multiply(out[i], static_cast<i32>((left - 1) << 24)) : 0;
            if (left) --left;
        }
        s->fade = left;
        if (!left) release(s);
    }
    // Keep the stock-side work from being optimised away.
    sink = out[kFrames - 1];
}

i32 pad_max(u32 f) { return (kPadSigns >> f & 1u) ? (1 << (kPadBits[f] - 1)) - 1 : (1 << kPadBits[f]) - 1; }
i32 pad_min(u32 f) { return (kPadSigns >> f & 1u) ? -(1 << (kPadBits[f] - 1)) : 0; }

Payload level_payload(u32 model, u32 level, bool pads_top) {
    Payload p;
    p.marked = true;
    p.ready = pads_top;
    p.model = static_cast<u8>(model);
    for (u32 i = 0; i < 8; ++i) p.stored[i] = kLevels[level];
    for (u32 i = 0; i < 4; ++i) p.pads[i] = pads_top && i < kPadCount ? pad_max(i) : 0;
    return p;
}
Payload key_payload(u32 key) {
    Payload p;
    p.marked = true;
    p.ready = false;
    GOLDEN_NS::drum_defaults(key, &p.model, p.stored);
    for (u32 i = 0; i < 4; ++i) p.pads[i] = 0;
    return p;
}

// One scenario's results, written as it goes.
struct Sink {
    virtual void block(const i32* raw, u32 words) = 0;
    virtual void spent(u32 cycles) = 0;
};

enum Kind { kKit, kOne, kChokeRun, kCeiling };
// The benchmark's run: kind, the payload every voice plays (kit: each key's
// own), blocks, and whether the transport runs.
void run(Kind kind, const Payload& one, u32 blocks, bool transport, Sink& sink) {
    reset();
    const u32 sounding = kind == kOne ? 1 : kind == kChokeRun ? 2 : (kCapacity < 2 ? 2 : kCapacity);
    Handle* voices[kCapacity < 2 ? 2 : kCapacity];
    Payload payloads[24];
    for (u32 k = 0; k < 24; ++k) payloads[k] = kind == kKit ? key_payload(53 + k) : one;
    for (u32 v = 0; v < sounding; ++v) voices[v] = factory(kHandles - 1 - v);
    static i32 raw[(kCapacity < 2 ? 2 : kCapacity) * kFrames];
    for (u32 b = 0; b < blocks; ++b) {
        u32 spent = 0;
#ifdef GOLDEN_BFIN
        spent = cycles();
#endif
        if (kind == kChokeRun) {
            if (b % 8u == 0) start(voices[(b / 8u) % 2u], 53);
        } else {
            for (u32 v = 0; v < sounding; ++v)
                if ((b + v * 5u) % kHit == 0) start(voices[v], 53 + v % 24u);
        }
#ifdef GOLDEN_BFIN
        spent = cycles() - spent;
#endif
        const opxy::Transport time{1200, b * 64u * 192u / 44100u, true};
        for (u32 v = 0; v < sounding; ++v) {
#ifdef GOLDEN_BFIN
            const u32 begin = cycles();
#endif
            render(voices[v], payloads[kind == kKit ? v % 24u : 0], transport ? &time : nullptr, raw + v * kFrames);
#ifdef GOLDEN_BFIN
            spent += cycles() - begin;
#endif
        }
        sink.block(raw, sounding * kFrames);
        sink.spent(spent);
    }
    for (u32 v = 0; v < sounding; ++v) destroy(voices[v]);
}
}  // namespace golden

#ifdef GOLDEN_BFIN
// ---- Blackfin: counted cycles in the GDB simulator (newlib syscalls via EXCPT 0).
extern "C" int golden_syscall(int number, int* args);
namespace {
void put(const char* text, unsigned n) {
    int args[3] = {1, static_cast<int>(reinterpret_cast<unsigned long>(text)), static_cast<int>(n)};
    golden_syscall(5, args);
}
void say(const char* text) {
    unsigned n = 0;
    while (text[n]) ++n;
    put(text, n);
}
void number(unsigned v) {
    char b[12];
    int i = 11;
    b[i] = 0;
    do { b[--i] = static_cast<char>('0' + v % 10); v /= 10; } while (v);
    say(b + i);
}
struct Count : golden::Sink {
    unsigned blocks = 0, most = 0, low = 0, high = 0;
    void block(const opxy::i32*, opxy::u32) override {}
    void spent(opxy::u32 c) override {
        ++blocks;
        if (c > most) most = c;
        const unsigned before = low;
        low += c;
        high += low < before;
    }
    // The mean block; a total past 32 bits reports as 0xffffffff.
    unsigned mean() const { return high ? 0xffffffffu : low / blocks; }
};
}  // namespace
namespace {
void report(const char* name, int model, int level, const Count& c) {
    say("cycles ");
    say(name);
    say(" ");
    if (model >= 0) number(static_cast<unsigned>(model)); else say("-");
    say(" ");
    if (level >= 0) number(golden::kLevels[level]); else say("-");
    say(" ");
    number(c.mean());
    say(" ");
    number(c.most);
    say("\n");
}
}  // namespace
extern "C" int main() {
    using namespace golden;
    const u32 blocks = GOLDEN_BENCH_BLOCKS;
    u32 heavy_model = 0, heavy_level = 0, heaviest = 0;
    {
        Count c;
        run(kKit, Payload(), blocks, true, c);
        report("kit", -1, -1, c);
    }
    for (u32 r = 0; r < kModels * 3; ++r) {
        Count c;
        run(kOne, level_payload(r / 3, r % 3, true), blocks, true, c);
        report("voice", static_cast<int>(r / 3), static_cast<int>(r % 3), c);
        if (r == 0 || c.mean() > heaviest) { heaviest = c.mean(); heavy_model = r / 3; heavy_level = r % 3; }
    }
    {
        Count c;
        run(kChokeRun, level_payload(heavy_model, heavy_level, true), blocks, true, c);
        report("choke", static_cast<int>(heavy_model), static_cast<int>(heavy_level), c);
    }
    {
        Count c;
        run(kCeiling, level_payload(heavy_model, heavy_level, true), blocks, true, c);
        report("ceiling", static_cast<int>(heavy_model), static_cast<int>(heavy_level), c);
    }
    say("done\n");
    return 0;
}
#else
// ---- Host: golden hashes and single hits.
namespace {
using golden::i32;
using golden::u32;
using golden::u8;
FILE* hashes;
struct Hash : golden::Sink {
    unsigned long long total = 1469598103934665603ull;
    u32 blocks = 0;
    void block(const i32* raw, u32 words) override {
        unsigned long long h = 1469598103934665603ull;
        for (u32 i = 0; i < words; ++i) {
            const u32 w = static_cast<u32>(raw[i]);
            for (u32 b = 0; b < 4; ++b) { h ^= (w >> (8 * b)) & 0xffu; h *= 1099511628211ull; }
        }
        std::fwrite(&h, sizeof h, 1, hashes);
        total ^= h;
        total *= 1099511628211ull;
        ++blocks;
    }
    void spent(u32) override {}
};
void scene(const char* name, golden::Kind kind, const golden::Payload& p, u32 blocks, bool transport) {
    Hash h;
    golden::run(kind, p, blocks, transport, h);
    std::printf("scene %s %u %016llx\n", name, h.blocks, h.total);
}
int scenes(const char* path) {
    using namespace golden;
    hashes = std::fopen(path, "wb");
    if (!hashes) { std::perror(path); return 1; }
    char name[160];
    const u32 bench = GOLDEN_BENCH_BLOCKS, sweep = GOLDEN_SWEEP_BLOCKS;
    // The benchmark's runs, with the transport playing (as the benchmark) and stopped.
    for (int t = 1; t >= 0; --t) {
        const char* tag = t ? "on" : "off";
        std::snprintf(name, sizeof name, "bench-%s/kit", tag);
        scene(name, kKit, Payload(), bench, t);
        for (u32 r = 0; r < kModels * 3; ++r) {
            std::snprintf(name, sizeof name, "bench-%s/voice-m%u-l%u", tag, r / 3, kLevels[r % 3]);
            scene(name, kOne, level_payload(r / 3, r % 3, true), bench, t);
        }
        std::snprintf(name, sizeof name, "bench-%s/choke", tag);
        scene(name, kChokeRun, level_payload(GOLDEN_HEAVY_MODEL, GOLDEN_HEAVY_LEVEL, true), bench, t);
        std::snprintf(name, sizeof name, "bench-%s/ceiling", tag);
        scene(name, kCeiling, level_payload(GOLDEN_HEAVY_MODEL, GOLDEN_HEAVY_LEVEL, true), bench, t);
    }
    // Every model at each level with neutral pads.
    for (u32 m = 0; m < kModels; ++m)
        for (u32 l = 0; l < 3; ++l) {
            std::snprintf(name, sizeof name, "level/m%u-l%u", m, kLevels[l]);
            scene(name, kOne, level_payload(m, l, false), sweep, true);
        }
    // Each control alone at 0, 32, 96 and 127, the others at 64.
    static const u8 values[4] = {0, 32, 96, 127};
    for (u32 m = 0; m < kModels; ++m)
        for (u32 c = 0; c < 8; ++c)
            for (u32 v = 0; v < 4; ++v) {
                Payload p = level_payload(m, 0, false);
                p.stored[c] = values[v];
                std::snprintf(name, sizeof name, "control/m%u-c%u-v%u", m, c, values[v]);
                scene(name, kOne, p, sweep, true);
            }
    // Each pad control over its whole range, the other neutral, controls at 64.
    for (u32 m = 0; m < kModels; ++m)
        for (u32 f = 0; f < kPadCount; ++f)
            for (i32 v = pad_min(f); v <= pad_max(f); ++v) {
                Payload p = level_payload(m, 0, false);
                p.ready = true;
                p.pads[f] = v;
                std::snprintf(name, sizeof name, "pad/m%u-p%u-v%d", m, f, v);
                scene(name, kOne, p, sweep, true);
            }
    // Every key at its defaults.
    for (u32 k = 0; k < 24; ++k) {
        std::snprintf(name, sizeof name, "key/k%u", 53 + k);
        scene(name, kOne, key_payload(53 + k), sweep, true);
    }
    std::fclose(hashes);
    return 0;
}
// Single hits for the compare report. Each stdin line:
//   out.raw model c0 c1 c2 c3 c4 c5 c6 c7 pad0 pad1 blocks
// renders one voice from note-on, transport playing, until the kit says it
// finished or `blocks` pass, and writes raw int32 little-endian samples.
struct Writer : golden::Sink {
    FILE* f;
    u32 frames = 0;
    void block(const i32* raw, u32 words) override { std::fwrite(raw, 4, words, f); frames += words; }
    void spent(u32) override {}
};
int hits() {
    using namespace golden;
    char line[1024], path[700];
    while (std::fgets(line, sizeof line, stdin)) {
        unsigned m, c[8], n;
        int pad[2];
        if (std::sscanf(line, "%699s %u %u %u %u %u %u %u %u %u %d %d %u", path, &m, &c[0], &c[1], &c[2], &c[3], &c[4],
                        &c[5], &c[6], &c[7], &pad[0], &pad[1], &n) != 13) continue;
        Payload p;
        p.marked = true;
        p.ready = true;
        p.model = static_cast<u8>(m);
        for (u32 i = 0; i < 8; ++i) p.stored[i] = static_cast<u8>(c[i]);
        for (u32 i = 0; i < 4; ++i) p.pads[i] = i < 2 ? pad[i] : 0;
        Writer w;
        w.f = std::fopen(path, "wb");
        if (!w.f) { std::perror(path); return 1; }
        reset();
        Handle* h = factory(kHandles - 1);
        start(h, 53);
        i32 raw[kFrames];
        u32 b = 0;
        for (; b < n; ++b) {
            const opxy::Transport time{1200, b * 64u * 192u / 44100u, true};
            render(h, p, &time, raw);
            w.block(raw, kFrames);
            Slot* s = held(h);
            if (s && optional_finished::ended(GOLDEN_NS::finished(s->voice, shared))) break;
        }
        destroy(h);
        std::fclose(w.f);
        std::printf("%s %u\n", path, w.frames);
        std::fflush(stdout);
    }
    return 0;
}
}  // namespace
int main(int argc, char** argv) {
    if (argc == 3 && !std::strcmp(argv[1], "scenes")) return scenes(argv[2]);
    if (argc == 2 && !std::strcmp(argv[1], "hits")) return hits();
    std::fprintf(stderr, "usage: golden scenes <hashes.bin> | golden hits < jobs\n");
    return 2;
}
#endif

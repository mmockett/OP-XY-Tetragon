// GALILEO's learned tempo (tempo.h) and the clock its voices keep (engine.cpp):
// it starts at 120 BPM, holds through fills, flams, chords, swing and stray
// notes, follows a real change within a few bars, skips gaps across silence,
// and the kit's clock counts each block once however many voices render it.
#include "../engine.cpp"
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { std::printf("FAIL " __VA_ARGS__); std::printf("\n"); ++failures; } } while (0)

// A sixteenth in samples at a tempo.
unsigned p16(double bpm) { return static_cast<unsigned>(44100.0 * 60 / 4 / bpm + 0.5); }
bool near(unsigned a, unsigned b, double tolerance = 0.015) {
    const double r = static_cast<double>(a) / b;
    return r > 1 - tolerance && r < 1 + tolerance;
}
struct Song {
    galileo::tempo::Learner l;
    double now;
    Song() : now(0) { l.reset(); }
    // Notes every `sixteenths` of a beat at `bpm`, `count` of them.
    void play(double bpm, double sixteenths, int count) {
        for (int i = 0; i < count; ++i) {
            l.note(static_cast<unsigned>(now), true);
            now += sixteenths * 44100.0 * 60 / 4 / bpm;
        }
    }
    // The clock's resolution: note starts fall on 64-sample blocks.
    void note() { l.note(static_cast<unsigned>(now) & ~63u, true); }
};
}

int main() {
    using galileo::tempo::Learner;
    // It starts at 120 BPM and stays there for a pattern at 120.
    {
        Song s;
        CHECK(s.l.p16 == 5512, "starts at %u, not 5512 (120 BPM)", s.l.p16);
        s.play(120, 1, 64);
        CHECK(near(s.l.p16, p16(120)), "16ths at 120 BPM drift it to %u", s.l.p16);
    }
    // Close tempos are followed by the steady pull: 126 BPM in 16ths and 8ths.
    {
        Song s;
        for (int bar = 0; bar < 4; ++bar) { s.play(126, 1, 8); s.play(126, 2, 4); }
        CHECK(near(s.l.p16, p16(126)), "126 BPM learned as %u, not %u", s.l.p16, p16(126));
    }
    // A new tempo is found again within a few bars: 8ths at 100, quarters at 90, 16ths at 140.
    {
        const double tempos[3] = {100, 90, 140}, steps[3] = {2, 4, 1};
        for (int k = 0; k < 3; ++k) {
            Song s;
            s.play(120, 1, 32);
            s.play(tempos[k], steps[k], static_cast<int>(64 / steps[k]));   // four bars
            CHECK(near(s.l.p16, p16(tempos[k]), 0.02), "%g BPM in %g-sixteenth notes learned as %u, not %u",
                  tempos[k], steps[k], s.l.p16, p16(tempos[k]));
        }
    }
    // A fill doesn't move it: a bar of 32nds, a bar of 16th triplets, a swung bar,
    // then the beat again.
    {
        Song s;
        s.play(120, 1, 32);
        s.play(120, 0.5, 32);
        s.play(120, 2.0 / 3, 24);
        for (int i = 0; i < 8; ++i) { s.play(120, 1.33, 1); s.play(120, 0.67, 1); }
        s.play(120, 1, 8);
        CHECK(near(s.l.p16, p16(120)), "a fill moved it to %u", s.l.p16);
    }
    // A stray note (off the grid) doesn't move it.
    {
        Song s;
        s.play(120, 1, 16);
        s.now += 0.37 * p16(120);
        s.play(120, 1, 16);
        CHECK(near(s.l.p16, p16(120)), "a stray note moved it to %u", s.l.p16);
    }
    // Chords and flams are one note: 16ths at 120 each with a second note 0 and 23 ms later.
    {
        Song s;
        for (int i = 0; i < 32; ++i) {
            const double t = s.now;
            s.l.note(static_cast<unsigned>(t), true);
            s.l.note(static_cast<unsigned>(t), true);
            s.l.note(static_cast<unsigned>(t + 1014), true);
            s.now = t + p16(120);
        }
        CHECK(near(s.l.p16, p16(120)), "chords and flams moved it to %u", s.l.p16);
    }
    // A gap across a silence (the clock stopped) is no gap.
    {
        Song s;
        s.play(120, 1, 16);
        for (int i = 0; i < 8; ++i) {
            s.l.note(static_cast<unsigned>(s.now), false);
            s.now += 3 * p16(120) + 517;   // nothing to do with the beat
        }
        CHECK(near(s.l.p16, p16(120)), "gaps across silences moved it to %u", s.l.p16);
    }
    // The SDK's tempo, where it is passed, wins: 75 BPM.
    CHECK(galileo::tempo::sixteenth(750) == p16(75), "75 BPM is %u samples", galileo::tempo::sixteenth(750));
    CHECK(galileo::tempo::sixteenth(0) == 0, "no tempo is not 0");

    // The kit's clock: three voices rendering each block move it one block.
    {
        static galileo::Shared shared;
        static galileo::Voice voices[3];
        std::memset(&shared, 0, sizeof(shared));
        std::memset(voices, 0, sizeof(voices));
        const opxy::i32 live[8] = {64 << 24, 64 << 24, 64 << 24, 64 << 24, 88 << 24, 0, 0, 0};
        opxy::u32 pitch[64];
        for (int i = 0; i < 64; ++i) pitch[i] = 25479004u;
        opxy::i32 out[64];
        for (unsigned v = 0; v < 3; ++v) {
            galileo::initialize(voices[v], shared, v + 1);
            galileo::start(voices[v], shared, v + 1);
            galileo::drum_select(voices[v], shared, v + 1, 7);   // rides: long enough to sound throughout
        }
        for (unsigned b = 0; b < 100; ++b)
            for (unsigned v = 0; v < 3; ++v) galileo::render(voices[v], shared, v + 1, out, live, pitch);
        CHECK(shared.now == 6400, "three voices over 100 blocks moved the clock %u samples, not 6400", shared.now);
        // A note while others sound counts its gap; the gap is the clock's.
        CHECK(shared.learner.has_last, "notes while the kit sounds don't start a gap");
        // The newest note's voice keeps the clock after its sound has ended; another's is freed.
        static galileo::Voice hat;
        std::memset(&hat, 0, sizeof(hat));
        galileo::initialize(hat, shared, 9);
        galileo::start(hat, shared, 9);
        const opxy::i32 short_hat[8] = {64 << 24, 10 << 24, 64 << 24, 64 << 24, 88 << 24, 0, 0, 0};
        unsigned freed = 0, sounded = 0;
        for (unsigned b = 0; b < 1500 && !freed; ++b) {
            galileo::drum_select(hat, shared, 9, 6);
            galileo::render(hat, shared, 9, out, short_hat, pitch);
            for (int i = 0; i < 64; ++i) sounded += out[i] != 0;
            if (galileo::finished(hat, shared)) freed = b;
        }
        CHECK(sounded, "the hat is silent");
        CHECK(!freed, "the newest note's voice was freed %u blocks in, before 2.5 s of silence", freed);
        CHECK(hat.done, "a closed hat is still sounding after 2 s");
        shared.latest = 1;   // another note came since
        CHECK(galileo::finished(hat, shared), "a finished voice that isn't the newest note's is kept");
    }
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}

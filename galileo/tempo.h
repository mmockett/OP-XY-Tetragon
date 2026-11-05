#pragma once
// The tempo GALILEO plays its locked frets to.
//
// A drum kit's render gets no Transport on SDK 1.10 as released, nor does the
// screen's Input. Where the SDK passes one (drum_transport, the proposal on
// drum/kit-transport) the kit uses its tempo. Otherwise it learns the beat
// from its own notes: the gaps between note starts, across every key and
// track, timed by a clock the voices advance as they render.
//
// The estimate is of a sixteenth, 60..260 ms, and starts at 120 BPM. A gap
// counts only when it is a whole number (1..16) of sixteenths within 12%; it
// then pulls the estimate a quarter of the way. Gaps under 40 ms are chords
// and flams and are skipped. Gaps that fit a half, a third or a quarter of
// the sixteenth, and quicker than it, are fills, swing or triplets: they
// neither count nor disturb. A gap that fits nothing is a stray note. Only when
// five of the last eight gaps fit nothing, across at least two bars, is the beat searched
// for again among the recent gaps, preferring sixteenths that explain most of
// them in plain 1, 2, 4, 8 multiples and lie nearest the old beat. So a fill
// or a stray note never moves it, and a new tempo takes a couple of bars.
// A gap across a silence (no voice sounding, so no clock) is not a gap.
#include "dsp.h"

namespace galileo {
namespace tempo {
const u32 shortest = 2646, longest = 11466;   // 60 .. 260 ms
const u32 cluster = 1764;                     // 40 ms
const u32 start = 5512;                       // 125 ms: a sixteenth at 120 BPM
const u32 recent = 8;
// The divisions a gap may be of a sixteenth when searching, and how plain each is.
const u8 divisors[8] = {1, 2, 3, 4, 6, 8, 12, 16};
const u8 plainness[8] = {4, 4, 2, 4, 2, 4, 2, 4};

struct Learner {
    u32 p16;          // the estimate: a sixteenth, samples (Q4 below)
    u32 p16_q4;
    u32 last;         // when the last counted note started
    u32 has_last;
    u32 gaps[recent]; // the latest gaps, newest at count % recent
    u8 fits[recent];  // 1 fits the beat, 2 a fill (a subdivision), 0 fits nothing
    u32 count;
    u32 since;        // samples since the beat last fitted a gap

    void reset() {
        p16 = start;
        p16_q4 = start << 4;
        last = 0;
        has_last = 0;
        count = 0;
        since = 0;
        for (u32 i = 0; i < recent; ++i) { gaps[i] = 0; fits[i] = 0; }
    }
    // How a gap sits on a beat p: the whole multiple it is within 12% of a
    // sixteenth, else 0. q16: the gap in sixteenths, Q16.
    static u32 multiple(u32 gap, u32 p, u32& error) {
        const u32 q = dsp::udiv(gap << 8, p) << 8;        // Q16 (gap < 2^24)
        const u32 m = (q + 32768) >> 16;
        const u32 near = m << 16;
        error = q > near ? q - near : near - q;
        return m;
    }
    static u8 classify(u32 gap, u32 p) {
        u32 error;
        const u32 m = multiple(gap, p, error);
        if (m >= 1 && m <= 16 && error <= 7864) return 1;           // 0.12
        // A fill: quicker than the beat, a whole number of its halves, thirds or quarters.
        if (gap >= p) return 0;
        for (u32 k = 2; k <= 4; ++k) {
            const u32 sub = (p * table::reciprocal[k]) >> 16;
            const u32 mm = multiple(gap, sub, error);
            if (mm >= 1 && mm <= 32 && error <= 5243) return 2;      // 0.08 of the part
        }
        return 0;
    }
    // A note started at `now` on the kit's clock; continuous: the clock ran
    // without a break since the last note.
    void note(u32 now, bool continuous) {
        if (!continuous || !has_last) { last = now; has_last = 1; return; }
        const u32 gap = now - last;
        if (gap < cluster) return;                 // a chord or a flam: keep the first's time
        last = now;
        since += gap;
        if (gap > 16 * longest) return;            // too long to tell
        const u8 fit = classify(gap, p16);
        gaps[count % recent] = gap;
        fits[count % recent] = fit;
        ++count;
        if (fit == 1) {
            u32 error;
            const u32 m = multiple(gap, p16, error);
            const u32 measured_q4 = dsp::udiv(gap << 4, m);
            // A quarter of the way; a long gap (a bar) counts as much as a sixteenth.
            const i32 pull = (static_cast<i32>(measured_q4) - static_cast<i32>(p16_q4)) >> 2;
            p16_q4 = static_cast<u32>(static_cast<i32>(p16_q4) + pull);
            if (p16_q4 < (shortest << 4)) p16_q4 = shortest << 4;
            if (p16_q4 > (longest << 4)) p16_q4 = longest << 4;
            p16 = p16_q4 >> 4;
            since = 0;
            return;
        }
        // Misfits outnumbering fits over the last eight gaps and two bars: search again.
        if (count < recent || since < 32 * p16) return;
        u32 bad = 0, good = 0;
        for (u32 i = 0; i < recent; ++i) {
            bad += fits[i] == 0;
            good += fits[i] == 1;
        }
        if (bad < 5 || bad <= good) return;
        search();
    }
    // The sixteenth that best explains the recent gaps.
    void search() {
        u32 best = 0, best_score = 0, best_distance = 0xffffffffu;
        for (u32 i = 0; i < recent; ++i)
            for (u32 d = 0; d < 8; ++d) {
                const u32 p = dsp::udiv(gaps[i], divisors[d]);
                if (p < shortest || p > longest) continue;
                u32 score = 0;
                for (u32 j = 0; j < recent; ++j) {
                    u32 error;
                    const u32 m = multiple(gaps[j], p, error);
                    if (m < 1 || m > 16 || error > 5243) continue;
                    score += (m & (m - 1)) == 0 ? 4 : (m == 3 || m == 6 || m == 12) ? 2 : 1;
                }
                score = score * 4 + plainness[d];
                // nearest the old beat as a ratio, not a difference
                const u32 distance = p > p16 ? dsp::udiv((p - p16) << 10, p16) : dsp::udiv((p16 - p) << 10, p);
                if (score > best_score || (score == best_score && distance < best_distance)) {
                    best = p; best_score = score; best_distance = distance;
                }
            }
        // Adopt it only if it explains most of them.
        if (!best || best_score < 4 * 4 * (recent / 2)) return;
        p16 = best;
        p16_q4 = best << 4;
        since = 0;
        for (u32 i = 0; i < recent; ++i) fits[i] = classify(gaps[i], p16);
    }
};

// A sixteenth in samples from a tempo in tenths of BPM: 44100 * 60 / (4 bpm).
inline u32 sixteenth(u32 bpm_x10) {
    if (!bpm_x10) return 0;
    const u32 p = dsp::udiv(6615000u, bpm_x10);
    return p < 1000 ? 1000 : p > 22050 ? 22050 : p;
}
}
}

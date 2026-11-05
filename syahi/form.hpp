#pragma once
// FORM: seven anchors of the struck object's geometry, the same on every model.
// Each anchor gives eight mode ratios (log2, Q16), the weights a strike at the
// centre and at the edge gives each mode (Q15), and the material's tilt alpha
// (Q10): a mode at ratio r rings r^-alpha as long as the lowest. FORM moves
// between neighbouring anchors, ratios in log space.
//
// The ratios are plain physics: one sine; a mass-loaded head pulled to the
// harmonic series (Raman); the ideal circular membrane's Bessel-zero ratios; a
// thick slab; a free bar with a tuned undercut; a thin circular plate; and a
// bell's hum, prime, tierce, quint and nominal.
#include "dsp.hpp"

namespace syahi {
namespace form {
const u32 anchors = 7;
struct Anchor {
    i32 ratio[8];    // log2 of each mode's ratio to the lowest named partial, Q16
    i16 centre[8];   // strike weights at the centre, Q15
    i16 edge[8];     // and at the edge
    i32 alpha;       // Q10
};
constexpr i16 w(double x) { return static_cast<i16>(q15(x)); }
constexpr Anchor anchor[anchors] = {
    // SINE: one mode, the 808's body; struck at the edge it gains harmonics.
    {{lg(1), lg(2), lg(3), lg(4), lg(5), lg(6), lg(7), lg(8)}, {w(.999), w(0), w(0), w(0), w(0), w(0), w(0), w(0)}, {w(.9), w(.4), w(.25), w(.12), w(.06), w(.03), w(0), w(0)}, 1024},
    // LOAD: Raman's loaded head, near-harmonic.
    {{lg(1), lg(2), lg(3), lg(4), lg(5), lg(6.05), lg(7.1), lg(8.2)}, {w(.999), w(.45), w(.22), w(.12), w(.06), w(.04), w(.02), w(.01)},
     {w(.45), w(.6), w(.5), w(.42), w(.34), w(.26), w(.2), w(.15)}, 563},
    // SKIN: the ideal circular membrane.
    {{lg(1), lg(1.594), lg(2.136), lg(2.296), lg(2.653), lg(2.918), lg(3.156), lg(3.501)}, {w(.999), w(.06), w(.05), w(.45), w(.05), w(.04), w(.04), w(.22)},
     {w(.5), w(.8), w(.7), w(.3), w(.6), w(.5), w(.45), w(.35)}, 1280},
    // WOOD: a thick slab, a slit drum.
    {{lg(1), lg(2.57), lg(4.21), lg(5.9), lg(6.7), lg(8.5), lg(9.9), lg(12.3)}, {w(.999), w(.32), w(.16), w(.1), w(.06), w(.05), w(.03), w(.02)},
     {w(.7), w(.55), w(.4), w(.3), w(.22), w(.18), w(.12), w(.1)}, 1741},
    // BAR: a free bar with its undercut partial tuned.
    {{lg(1), lg(2.756), lg(3.98), lg(5.404), lg(7.1), lg(8.933), lg(11.0), lg(13.34)}, {w(.999), w(.05), w(.42), w(.28), w(.1), w(.14), w(.05), w(.05)},
     {w(.6), w(.7), w(.3), w(.5), w(.3), w(.3), w(.2), w(.2)}, 563},
    // PLATE: a thin circular plate. The prototype's plate was flat-weighted and
    // rang its upper modes as long as its lowest, which clanged; here the
    // fundamental leads and the upper modes fall away twice as fast.
    {{lg(1), lg(1.71), lg(2.31), lg(2.83), lg(3.39), lg(3.91), lg(4.47), lg(5.12)}, {w(.999), w(.34), w(.3), w(.2), w(.16), w(.12), w(.09), w(.07)},
     {w(.6), w(.52), w(.45), w(.38), w(.32), w(.26), w(.2), w(.16)}, 717},
    // BELL: prime, tierce, quint, nominal and the partials above, and last the
    // hum an octave under the prime. The prime stays on the note as the plate
    // closes up into the bell; the hum comes in from nothing (below).
    {{lg(1), lg(1.19), lg(1.5), lg(2.0), lg(2.51), lg(2.66), lg(3.01), lg(0.5)}, {w(.999), w(.6), w(.4), w(.5), w(.3), w(.25), w(.2), w(.5)},
     {w(.7), w(.6), w(.55), w(.6), w(.45), w(.4), w(.35), w(.35)}, 154},
};

// The nearest anchor's name, for the header.
static inline const char* name(u32 raw) {
    static const char* const names[anchors] = {"sine", "load", "skin", "wood", "bar", "plate", "bell"};
    const u32 i = (raw * 6u + 63u) / 127u;
    return names[i < anchors ? i : anchors - 1];
}

// The geometry at FORM raw 0..127: ratios, weights for a strike at spot (Q31
// 0 centre .. 1 edge) and alpha (Q10).
struct Shape {
    i32 ratio[8];
    i32 weight[8];   // Q15
    i32 alpha;       // Q10
};
static inline void shape(u32 raw, i32 spot, Shape& out) {
    const u32 position = (raw > 127 ? 127u : raw) * 3096u;   // 6 anchors' worth, Q16 (127 * 3096 = 6 * 65532)
    u32 i = position >> 16;
    if (i > anchors - 2) i = anchors - 2;
    const i32 f = static_cast<i32>(position - (i << 16));    // 0..65536
    const Anchor& a = anchor[i];
    const Anchor& b = anchor[i + 1];
    for (u32 k = 0; k < 8; ++k) {
        const i32 span = b.ratio[k] - a.ratio[k];
        const i32 c = a.centre[k] + ((b.centre[k] - a.centre[k]) * (f >> 1) >> 15);
        const i32 e = a.edge[k] + ((b.edge[k] - a.edge[k]) * (f >> 1) >> 15);
        out.weight[k] = c + multiply(spot, e - c);
        if (span < -2 * 65536 || span > 2 * 65536) {
            // A mode that would slide more than two octaves (the plate's top
            // mode becoming the bell's hum) would sweep through all the others
            // and clang: it fades out and comes back in where it lands.
            const i32 g = f < 32768 ? 32768 - (f >> 1) * 2 : (f >> 1) * 2 - 32768;   // |1 - 2 f|, Q15
            out.ratio[k] = f < 32768 ? a.ratio[k] : b.ratio[k];
            out.weight[k] = (out.weight[k] * g) >> 15;
        } else {
            out.ratio[k] = a.ratio[k] + ((span >> 4) * f >> 12);
        }
    }
    out.alpha = a.alpha + ((b.alpha - a.alpha) * (f >> 1) >> 15);
    // From the bar on, between two anchors the modes stand where no object puts
    // them and beat roughly against each other: there the upper modes die
    // sooner (0.75 more tilt half way), so the strike's clang passes and the
    // note rings. The drums' stretch, sine to wood, keeps its own.
    if (i >= 4) {
        const i32 between = static_cast<i32>((static_cast<u32>(f >> 1) * static_cast<u32>(32768 - (f >> 1))) >> 13);   // 4 f (1 - f), Q15
        out.alpha += (between * 768) >> 15;
    }
    // Every shape and strike point strikes with the same energy: the weights'
    // squares sum to one, as the sine's single mode does. So FORM and SPOT change
    // the tone, not the level.
    u32 sum = 0;
    for (u32 k = 0; k < 8; ++k) {
        const u32 q = static_cast<u32>(out.weight[k] < 0 ? -out.weight[k] : out.weight[k]) >> 2;
        sum += q * q;
    }
    // Integer square root (Q13), once a setup.
    u32 root = 0;
    for (u32 bit = 1u << 30; bit; bit >>= 2) {
        if (sum >= root + bit) { sum -= root + bit; root = (root >> 1) + bit; }
        else root >>= 1;
    }
    if (root < 64) root = 64;
    // At most unity: the root is taken of the weights shifted down by 2, so one
    // mode alone (the sine struck dead centre) came out at 32771, and the core's
    // weight << 16 then wrapped negative: 13 dB too loud at SPOT 0.
    for (u32 k = 0; k < 8; ++k) {
        const u32 q = (static_cast<u32>(out.weight[k] < 0 ? 0 : out.weight[k]) << 13) / root;
        out.weight[k] = static_cast<i32>(q < 32767u ? q : 32767u);
    }
}
}
}

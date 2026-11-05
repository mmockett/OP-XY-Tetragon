#pragma once
// HUMAN: each hit's own nudge to the strike point, the force and a few cents
// of pitch, drawn from the key's note count. The engine plays it and the
// screen draws the strike mark where it lands, from the same function.
#include "dsp.hpp"

namespace syahi {
struct Variation {
    i32 spot;       // added to SPOT, 0..127 units
    i32 pitch;      // octaves, Q16
    i32 velocity;   // Q31
};
// About a unit normal from three uniforms of a hash, Q16 (sigma 1).
static inline i32 normal(u32 h) {
    const i32 sum = static_cast<i32>(h & 1023u) + static_cast<i32>((h >> 10) & 1023u) + static_cast<i32>((h >> 20) & 1023u);
    return (sum - 1535) * 128;   // (sum / 1024 - 1.5) * 2 in Q16
}
// HUMAN 0..15 on hit `serial`: spot sigma 14 * h, pitch sigma 0.012 h octave,
// force down by up to 0.3 h, with h = HUMAN / 15.
static inline Variation vary(u32 serial, i32 human) {
    Variation v = {0, 0, one};
    if (human <= 0) return v;
    const i32 h = human > 15 ? 15 : human;
    const u32 a = hash(serial * 2654435761u + 0x9e3779b9u), b = hash(a + 0x85ebca6bu);
    v.spot = (normal(a) * 14 * h / 15) >> 16;
    v.pitch = (normal(b) * h) / 1250;              // 0.012 / 15 octave a step, Q16 * Q0
    v.velocity = one - static_cast<i32>((b >> 22) * 41943u * static_cast<u32>(h));   // up to 0.3 h of full
    return v;
}
}

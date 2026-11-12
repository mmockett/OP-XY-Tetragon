#pragma once
// Outlines the OP-XY draws cleanly: adapted from MD KIT's md_view.hpp (Max
// Mockett's own code). The device draws an outline with no area as streaks,
// merges points within an eighth of a pixel and draws a closing edge under a
// quarter pixel as a sliver, so every reshaping outline goes through outline().
#include <opxy/draw.hpp>
#include "dsp.hpp"

namespace syahi {
namespace view {
using namespace opxy::draw;
using opxy::i32;
using opxy::u32;

// The device's SVG loader merges points that (nearly) repeat, leaving a path
// fewer points than the frame binds, and then it stops reshaping that path:
// its old shape stays on screen. Points closer than an eighth of a pixel on
// both axes count as one.
const i32 apart = unit / 8;
inline bool near(const Point& a, const Point& b) {
    const i32 dx = a.x - b.x, dy = a.y - b.y;
    return dx > -apart && dx < apart && dy > -apart && dy < apart;
}
// An edge's length on its longer axis.
inline i32 span(const Point& a, const Point& b) {
    const i32 dx = b.x - a.x, dy = b.y - a.y, ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
    return ax > ay ? ax : ay;
}

// Moves q, reached from `from`, an eighth of a pixel to the left of its way in
// while it lands near one of the m points already placed.
inline void aside(Point& q, const Point& from, const Point* placed, u32 m) {
    const i32 side = apart;
    for (u32 tries = 0; tries < 4; ++tries) {
        bool seen = false;
        for (u32 j = 0; j < m && !seen; ++j) seen = near(placed[j], q);
        if (!seen) return;
        const i32 dx = q.x - from.x, dy = q.y - from.y;
        q.x += dy > 0 ? side : dy < 0 ? -side : 0;
        q.y += dx > 0 ? -side : dx < 0 ? side : 0;
    }
}

// Squares off every turn sharper than 150 degrees, an eighth of a pixel across:
// the OP-XY strokes with mitred joins and no mitre limit, so a stroke's mitre at
// such a point would run far out as a spike. An outline with no room left gives
// up the point that bends it least for it.
inline void blunt(Point* p, u32& m) {
    for (u32 i = 0; i < m && m > 3; ++i) {
        const Point& a = p[i ? i - 1 : m - 1];
        const Point b = p[i];
        const Point& c = p[i + 1 < m ? i + 1 : 0];
        const i32 ux = (b.x - a.x) >> 4, uy = (b.y - a.y) >> 4, vx = (c.x - b.x) >> 4, vy = (c.y - b.y) >> 4;
        const i32 dot = ux * vx + uy * vy, cross = ux * vy - uy * vx;
        if (dot >= 0 || (cross < 0 ? -cross : cross) * 7 > -dot * 4) continue;   // tan 30 degrees, about 4/7
        const i32 side = cross < 0 ? -1 : 1;
        Point q = b;
        q.x += side * (uy > 0 ? -apart : uy < 0 ? apart : 0);
        q.y += side * (ux > 0 ? apart : ux < 0 ? -apart : 0);
        if (near(q, c)) continue;
        if (m == 64) {
            u32 straightest = 64;
            i32 least = 0x7fffffff;
            for (u32 j = 0; j < m; ++j) {
                if (j == i || j == (i ? i - 1 : m - 1) || j == (i + 1 < m ? i + 1 : 0)) continue;
                const Point& f = p[j ? j - 1 : m - 1];
                const Point& g = p[j];
                const Point& h = p[j + 1 < m ? j + 1 : 0];
                const i32 sx = (g.x - f.x) >> 4, sy = (g.y - f.y) >> 4, tx = (h.x - g.x) >> 4, ty = (h.y - g.y) >> 4;
                if (span(f, g) < 4 * apart || span(g, h) < 4 * apart) continue;   // an end already squared
                const i32 bend = sx * ty - sy * tx, size = bend < 0 ? -bend : bend;
                if (size < least) { least = size; straightest = j; }
            }
            if (straightest == 64) return;
            for (u32 j = straightest; j + 1 < m; ++j) p[j] = p[j + 1];
            --m;
            if (straightest < i) --i;
        }
        for (u32 j = m; j > i + 1; --j) p[j] = p[j - 1];
        p[i + 1] = q;
        ++m;
        ++i;   // past the new point
    }
}

// Writes n path-local points (Q10) into a path as an outline the OP-XY draws
// without streaks: it draws them from an outline with no area, as a line
// traced out and back over itself, or one that turns straight back at a
// point. Repeated points go (near: within an eighth of a pixel); a point that
// revisits an earlier one moves an eighth of a pixel to the left of its way in
// (aside), so a retraced line becomes a thin ribbon and a spike a sliver; the
// longest edges are halved, each midpoint aside too, until there are 64
// points, each at least an eighth of a pixel from the others. An outline of
// one point collapses, which draws nothing.
template <class Path>
inline void outline(Path& path, const Point* points, u32 n, u32 key) {
    Point p[64];
    u32 m = 0;
    for (u32 i = 0; i < n && i < 64; ++i) {
        Point q = points[i];
        if (m && near(q, p[m - 1])) continue;
        if (m) aside(q, p[m - 1], p, m);
        p[m++] = q;
    }
    while (m > 1 && near(p[m - 1], p[0])) --m;
    if (!m) p[m++] = Point();
    blunt(p, m);
    // An edge halves while it spans four eighths of a pixel on an axis, so both
    // halves keep the quarter pixel a closing edge needs; edge m - 1 closes the
    // outline.
    while (m < 64) {
        u32 at = 0;
        i32 longest = 0;
        for (u32 i = 0; i < m && m > 1; ++i) {
            const Point& next = p[i + 1 < m ? i + 1 : 0];
            const i32 dx = next.x - p[i].x, dy = next.y - p[i].y;
            const i32 ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, span = ax > ay ? ax : ay;
            if (span > longest) { longest = span; at = i; }
        }
        if (longest < 4 * apart) break;
        const Point a = p[at], b = p[at + 1 < m ? at + 1 : 0];
        Point mid;
        mid.x = a.x + (b.x - a.x) / 2;
        mid.y = a.y + (b.y - a.y) / 2;
        aside(mid, a, p, m);
        for (u32 i = m; i > at + 1; --i) p[i] = p[i - 1];
        p[at + 1] = mid;
        ++m;
    }
    blunt(p, m);   // again: a midpoint moved aside can sharpen a turn past the limit
    // The outline is a loop, so it may start anywhere: start it where both its
    // edges are longest. The device draws a closing edge only an eighth of a
    // pixel long (a ribbon's end, on a level or upright line) as a long sliver
    // along the next edge.
    u32 start = 0;
    i32 best = -1;
    for (u32 i = 0; i < m && m > 2; ++i) {
        const Point& a = p[i ? i - 1 : m - 1];
        const Point& b = p[i];
        const Point& c = p[i + 1 < m ? i + 1 : 0];
        const i32 in = span(a, b), out = span(b, c), shorter = in < out ? in : out;
        if (shorter > best) { best = shorter; start = i; }
    }
    for (u32 i = 0; i < 64; ++i) path.points[i] = p[i < m ? (start + i) % m : (start + m - 1) % m];
    path.key = key ? key : 1;
}

// Makes a path a polyline of n points (up to 32), out and back as a thin
// ribbon (outline). `points` are path-local Q10.
template <class Path>
inline void polyline(Path& path, const Point* points, u32 n, u32 key) {
    if (n > 32) n = 32;
    Point both[64];
    for (u32 i = 0; i < n; ++i) both[i] = both[2 * n - 1 - i] = points[i];
    outline(path, both, 2 * n, key);
}

}
}

// CURIO's screen checks, run by sdk.py test: every frame valid and every
// reshaped outline drawable on the device over every control's whole travel
// (points at least 1/8 px apart, a closing edge of at least 1/4 px, no turn
// straight back), still at rest, light on churn while a hit plays, every
// control moving the picture, a hit's motion its sound's own length, and
// every caption fitting its column in the OP-XY's face.
#include "../engine.cpp"
#include "../display.cpp"
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
curio::DisplayState state;
curio::Frame frame, last;

opxy::draw::Input input_for(opxy::u32 model, const opxy::u8* raw) {
    opxy::draw::Input in = opxy::draw::Input();
    in.model = model;
    for (int c = 0; c < 8; ++c) in.slots[c] = raw[c] >= 127 ? 0x7fffffff : static_cast<opxy::i32>(raw[c]) << 24;
    in.track = 1;
    in.elapsed = 17;   // 60 frames a second, Q10 seconds
    in.amp.attack = in.amp.decay = in.amp.sustain = in.amp.release = opxy::draw::envelope_unavailable;
    return in;
}
int changed(const curio::Frame& a, const curio::Frame& b) {
    int n = 0;
    for (unsigned i = 0; i < curio::Frame::node_count; ++i) {
        const bool path = i < curio::Frame::path_count && a.paths[i].key != b.paths[i].key;
        if (path || std::memcmp(&a.nodes[i], &b.nodes[i], sizeof(a.nodes[i]))) ++n;
    }
    return n;
}
// The device's rules for the reshaped outlines that are showing.
bool outlines_ok(const curio::Frame& f, const char* what) {
    bool ok = opxy::draw::valid(f);
    if (!ok) std::printf("FAIL invalid frame: %s\n", what);
    for (unsigned p = 0; p < curio::Frame::path_count && ok; ++p) {
        if (f.nodes[p].x >= opxy::draw::hidden_x * 1024) continue;
        const opxy::draw::Point* q = f.paths[p].points;
        for (int i = 0; i < 64; ++i) {
            const opxy::draw::Point& a = q[(i + 63) % 64];
            const opxy::draw::Point& b = q[i];
            const opxy::draw::Point& c = q[(i + 1) % 64];
            const long long dx = c.x - b.x, dy = c.y - b.y;
            const long long d2 = dx * dx + dy * dy;
            if (d2 < 128LL * 128) { std::printf("FAIL outline %u points %d, %d closer than 1/8 px: %s\n", p, i, (i + 1) % 64, what); ok = false; break; }
            if (i == 63 && d2 < 256LL * 256) { std::printf("FAIL outline %u closing edge under 1/4 px: %s\n", p, what); ok = false; break; }
            const long long ux = b.x - a.x, uy = b.y - a.y;
            const double dot = static_cast<double>(ux * dx + uy * dy);
            const double n1 = static_cast<double>(ux * ux + uy * uy), n2 = static_cast<double>(d2);
            if ((a.x == c.x && a.y == c.y) || (dot < 0 && dot * dot > 0.998 * n1 * n2)) { std::printf("FAIL outline %u turns straight back at %d: %s\n", p, i, what); ok = false; break; }
        }
    }
    if (!ok) ++failures;
    return ok;
}
// Univers Next Pro Light's advances in thousandths of an em (the device lays
// text out by advance, without kerning).
struct Advance { char c; int width; };
const Advance advances[] = {
    {' ', 280}, {'+', 676}, {'-', 329}, {'.', 280}, {'0', 560}, {'1', 560}, {'2', 560}, {'3', 560}, {'4', 560}, {'5', 560},
    {'6', 560}, {'7', 560}, {'8', 560}, {'9', 560}, {'A', 693}, {'B', 656}, {'C', 664}, {'D', 728}, {'E', 602}, {'F', 555},
    {'G', 715}, {'H', 742}, {'I', 275}, {'J', 494}, {'K', 621}, {'L', 557}, {'M', 944}, {'N', 751}, {'O', 745}, {'P', 602},
    {'Q', 745}, {'R', 659}, {'S', 617}, {'T', 546}, {'U', 744}, {'V', 650}, {'W', 1020}, {'X', 638}, {'Y', 613}, {'Z', 565},
    {'a', 536}, {'b', 581}, {'c', 522}, {'d', 581}, {'e', 532}, {'f', 318}, {'g', 589}, {'h', 574}, {'i', 235}, {'j', 235},
    {'k', 513}, {'l', 235}, {'m', 894}, {'n', 574}, {'o', 571}, {'p', 581}, {'q', 581}, {'r', 345}, {'s', 480}, {'t', 325},
    {'u', 574}, {'v', 512}, {'w', 820}, {'x', 506}, {'y', 507}, {'z', 445}};
double width(const char* text, double px) {
    int total = 0;
    for (; *text; ++text) {
        int w = 2000;   // unknown: too wide
        for (const Advance& a : advances) if (a.c == *text) w = a.width;
        total += w;
    }
    return total * px / 1000.0;
}
void fits(const char* text, double px, double room, const char* what) {
    const double w = width(text, px);
    if (w > room) { std::printf("FAIL caption \"%s\" (%s) is %.1f px, over %.0f\n", text, what, w, room); ++failures; }
}
}

int main() {
    using namespace curio;
    double churn_sum = 0;
    int churn_frames = 0, churn_peak = 0, worst_model = 0;
    double worst_avg = 0;
    for (opxy::u32 m = 0; m < kit::models; ++m) {
        char what[96];
        // Every control through every value, at rest and in a hit.
        for (int c = 0; c < 8; ++c) {
            int moves = 0;
            for (int v = 0; v < 128; ++v) {
                opxy::u8 raw[8];
                std::memcpy(raw, kit::model[m].defaults, 8);
                raw[c] = static_cast<opxy::u8>(v);
                opxy::draw::Input in = input_for(m, raw);
                display_reset(state);
                display(state, in, frame);
                std::snprintf(what, sizeof(what), "%s control %d at %d", kit::model[m].name, c + 1, v);
                if (!outlines_ok(frame, what)) break;
                if (v && changed(frame, last)) ++moves;
                last = frame;
                if (v % 16 == 0) {
                    // Through a hit: 0.6 s of frames.
                    curio::Frame rest = frame;
                    in.note_serial = 1;
                    for (int f = 0; f < 36; ++f) {
                        display(state, in, frame);
                        std::snprintf(what, sizeof(what), "%s control %d at %d, %d ms into a hit", kit::model[m].name, c + 1, v, f * 17);
                        if (!outlines_ok(frame, what)) break;
                    }
                    frame = rest;
                }
            }
            // Every control moves the picture over its travel.
            if (moves < 4) { std::printf("FAIL %s's %s moves the screen at only %d of 127 steps\n", kit::model[m].name, kit::model[m].labels[c], moves); ++failures; }
        }
        // Churn: at rest nothing changes; a hit at the defaults and with every control up.
        for (int setting = 0; setting < 2; ++setting) {
            opxy::u8 raw[8];
            for (int c = 0; c < 8; ++c) raw[c] = setting ? 127 : kit::model[m].defaults[c];
            opxy::draw::Input in = input_for(m, raw);
            display_reset(state);
            display(state, in, frame);
            last = frame;
            for (int f = 0; f < 10; ++f) {
                display(state, in, frame);
                if (changed(frame, last)) { std::printf("FAIL %s changes at rest\n", kit::model[m].name); ++failures; break; }
            }
            in.note_serial = 1;
            int sum = 0, n = 0, peak = 0;
            for (int f = 0; f < 600; ++f) {
                in.held_notes = f < 6 ? 1 : 0;
                display(state, in, frame);
                const int k = changed(frame, last);
                last = frame;
                if (state.age < 0) break;
                sum += k;
                ++n;
                peak = k > peak ? k : peak;
            }
            // After the hit: still again.
            display(state, in, frame);
            if (changed(frame, last)) { std::printf("FAIL %s keeps moving after a hit\n", kit::model[m].name); ++failures; }
            const double avg = n ? static_cast<double>(sum) / n : 0;
            churn_sum += sum;
            churn_frames += n;
            if (peak > churn_peak) churn_peak = peak;
            if (avg > worst_avg) { worst_avg = avg; worst_model = static_cast<int>(m); }
            if (avg > 22) { std::printf("FAIL %s churns %.1f nodes a frame in a hit (setting %d)\n", kit::model[m].name, avg, setting); ++failures; }
        }
        // Captions: every value of every control fits 53 px at 20 px; titles 56 px at 10 px.
        for (int c = 0; c < 8; ++c) {
            char title[16];
            copy(title, sizeof(title), kit::model[m].labels[c]);
            for (int i = 0; title[i]; ++i) if (title[i] >= 'A' && title[i] <= 'Z') title[i] = static_cast<char>(title[i] - 'A' + 'a');
            fits(title, 10, 56, "title");
            for (int v = 0; v < 128; ++v) {
                char text[8];
                view::caption(m, static_cast<opxy::u32>(c), v >= 127 ? 0x7fffffff : v << 24, text, sizeof(text));
                fits(text, 20, 53, kit::model[m].labels[c]);
            }
        }
        // The specimen's name under Shift, at the header's size, and its drawer as the title.
        fits(kit::model[m].name, 20, 53, "name");
        fits(kit::drawer_name[kit::model[m].drawer], 10, 56, "drawer");
    }
    // Shift's captions.
    const char* const shift[] = {"none", "1", "2", "3", "-100", "100", "-9dB", "0dB", "-16st", "+15st", "15", "TRK", "100", "83"};
    for (const char* s : shift) fits(s, 20, 53, "shift");
    // Stock's own level caption at its lowest is 0.7 px over: kept as stock writes it.
    fits("-30dB", 20, 54, "shift level");
    const char* const shift_titles[] = {"machine", "choke", "pan", "level", "tune", "vary", "fx i", "fx ii", "thump", "crack", "tick", "ring"};
    for (const char* s : shift_titles) fits(s, 10, 56, "shift title");
    // A drum kit's voices ignore the track's amp envelope and note-off, so
    // the screen does too: a glass hit moves for its own ring whatever the
    // envelope says and whether the key is let go at once or held.
    {
        int frames_for[4];
        for (int e = 0; e < 4; ++e) {
            opxy::draw::Input in = input_for(12, kit::model[12].defaults);
            if (e == 1) { in.amp.attack = 0; in.amp.decay = 20 << 24; in.amp.sustain = 0; in.amp.release = 0x7f000000; }
            if (e == 2) { in.amp.attack = 0; in.amp.decay = 0x7fffffff; in.amp.sustain = 0x7fffffff; in.amp.release = 0; }
            display_reset(state);
            display(state, in, frame);
            in.note_serial = 1;
            int n = 0;
            for (int f = 0; f < 600; ++f) {
                in.held_notes = e == 3 ? 1 : f < 6 ? 1 : 0;
                display(state, in, frame);
                if (state.age < 0) break;
                ++n;
            }
            frames_for[e] = n;
        }
        if (frames_for[1] != frames_for[0] || frames_for[2] != frames_for[0] || frames_for[3] != frames_for[0]) {
            std::printf("FAIL the screen follows the envelope or note-off: %d, %d, %d, %d frames\n", frames_for[0], frames_for[1], frames_for[2], frames_for[3]); ++failures;
        }
        std::printf("screen: a glass hit moves for %d frames, whatever the envelope and note-off\n", frames_for[0]);
    }
    std::printf("screen: %d nodes; a hit changes %.1f a frame on average (worst %s %.1f), %d at most\n",
                static_cast<int>(curio::Frame::node_count), churn_frames ? churn_sum / churn_frames : 0.0, kit::model[worst_model].name, worst_avg, churn_peak);
    if (failures) std::printf("%d failures\n", failures);
    return failures ? 1 : 0;
}

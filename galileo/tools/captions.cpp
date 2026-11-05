// Prints every caption GALILEO's header can show, one a line ("title: " for
// titles), for tools/check_captions.py to measure in the device font.
#include "../engine.cpp"
#include "../display.cpp"
#include <cstdio>

int main() {
    using namespace galileo;
    char text[32];
    for (u32 m = 0; m < kit::models; ++m) {
        std::printf("%s\n", kit::model[m].name);                    // MACHINE's value
        for (u32 s = 0; s < 8; ++s) {
            std::printf("title: %s\n", kit::model[m].labels[s]);
            for (u32 raw = 0; raw < 128; ++raw)
                for (i32 feel = 0; feel < 16; feel += (s == 4 ? 1 : 16)) {
                    caption::value(text, opxy::draw::header_capacity, m, s, raw, feel);
                    std::printf("%s\n", text);
                }
        }
    }
    for (u32 g = 0; g < kit::group_count; ++g) std::printf("title: %s\n", kit::groups[g]);
    for (u32 r = 0; r < 2; ++r)
        for (u32 c = 0; c < 4; ++c) std::printf("title: %s\n", view::shift_titles[r][c]);
    for (i32 v = -16; v < 16; ++v) { caption::space(text, 16, v); std::printf("%s\n", text); }
    for (i32 v = 0; v < 16; ++v) { caption::feel(text, 16, v); std::printf("%s\n", text); }
    for (u32 i = 0; i < 4; ++i) std::printf("%s\n", view::chokes[i]);
    std::printf("-100\n100\n-30dB\n0dB\nTRK\n100\n83\n");
    return 0;
}

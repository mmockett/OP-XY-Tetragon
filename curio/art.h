#pragma once
// Written by tools/make_panel.py: edit the drawing there, then run it.
// The cabinet's open drawer: its nodes, and where each moving part stands on
// screen, Q10 px (1024 a pixel).
#include <opxy/types.hpp>

namespace curio {
namespace art {
using opxy::i32;
const unsigned node_count = 73, header_node = 65;
enum Node : unsigned {
    plumb = 0,
    crate_left = 1,
    crate_right = 2,
    crate_top = 3,
    pad_left = 4,
    pad_right = 5,
    pad_top = 6,
    spot = 7,
    rod_0 = 8,
    rod_1 = 9,
    rod_2 = 10,
    rod_3 = 11,
    rod_4 = 12,
    rod_5 = 13,
    rod_6 = 14,
    rod_7 = 15,
    rod_8 = 16,
    rod_9 = 17,
    grit_0 = 18,
    grit_1 = 19,
    grit_2 = 20,
    grit_3 = 21,
    grit_4 = 22,
    grit_5 = 23,
    grit_6 = 24,
    grit_7 = 25,
    grit_8 = 26,
    grit_9 = 27,
    stand_left = 28,
    stand_right = 29,
    stand_top = 30,
    arm_left = 31,
    arm_right = 32,
    arm_top = 33,
    mark_0 = 34,
    mark_1 = 35,
    mark_2 = 36,
    mark_3 = 37,
    mark_4 = 38,
    ball_0 = 39,
    glint_0 = 40,
    ball_1 = 41,
    glint_1 = 42,
    ball_2 = 43,
    glint_2 = 44,
    ball_3 = 45,
    glint_3 = 46,
    ball_4 = 47,
    glint_4 = 48,
    symbol_ball = 49,
    symbol_cask = 50,
    symbol_stone = 51,
    symbol_drip = 52,
    symbol_ice = 53,
    symbol_sheet = 54,
    symbol_spark = 55,
    symbol_cork = 56,
    symbol_cicada = 57,
    symbol_ratchet = 58,
    symbol_pod = 59,
    symbol_match = 60,
    symbol_glass = 61,
    symbol_bell_jar = 62,
    symbol_chalk = 63,
    symbol_roar = 64
};
// Sixteen symbols for the card holder, one a specimen, in the kit's order.
const unsigned symbol_first = symbol_ball;
// The plumb line's foot on the head, where the ball lands; the drop over the
// head at DROP 0, and how much more at 100. The ball rests 0.55 of the way up.
const i32 plumb_x = 252908, plumb_y = 147456;
const i32 drop_lo = 12288, drop_span = 38912;
// The clamp and the stand's upper length at DROP 0: they rise with the drop.
const i32 arm_x = 237833, arm_y = 118272;
const i32 stand_x = 226304, stand_y = 120832;
// The damper (DECAY), the crate (FLOOR) and the spot (SPOT) at 0, and how far each slides by 100.
const i32 pad_x = 267984, pad_y = 146432, pad_dx = 3547, pad_dy = -2048;
const i32 crate_x = 192605, crate_y = 148992, crate_dx = 14189, crate_dy = 8192;
const i32 spot_x = 251135, spot_y = 154624, spot_dx = 11529, spot_dy = 6656;
// The rods' tops round the head, for 4 to 10 rods.
const i32 rod_x[7][10] = {
    {264296, 243919, 229492, 230559, 0, 0, 0, 0, 0, 0},
    {264296, 248984, 235172, 228134, 230559, 0, 0, 0, 0, 0},
    {264296, 252120, 240140, 231319, 227838, 230559, 0, 0, 0, 0},
    {264296, 254221, 243919, 235172, 229492, 227860, 230559, 0, 0, 0},
    {264296, 255717, 246780, 238624, 232290, 228586, 227983, 230559, 0, 0},
    {264296, 256832, 248984, 241521, 235172, 230559, 228134, 228134, 230559, 0},
    {264296, 257694, 250722, 243919, 237813, 232876, 229492, 227921, 228286, 230559}};
const i32 rod_y[7][10] = {
    {166503, 167120, 158790, 147026, 0, 0, 0, 0, 0, 0},
    {166503, 167903, 163840, 155865, 147026, 0, 0, 0, 0, 0},
    {166503, 168074, 166065, 160972, 154055, 147026, 0, 0, 0, 0},
    {166503, 168062, 167120, 163840, 158790, 152842, 147026, 0, 0, 0},
    {166503, 167990, 167643, 165504, 161847, 157138, 151979, 147026, 0, 0},
    {166503, 167903, 167903, 166503, 163840, 160174, 155865, 151335, 147026, 0},
    {166503, 167815, 168026, 167120, 165165, 162315, 158790, 154862, 150837, 147026}};
// The grains on the head; the bounce marks' places at the head's height (each
// raised by its bounce's height).
const i32 grit_x[10] = {258960, 251381, 258473, 256838, 261936, 249918, 235523, 257194, 265145, 241842};
const i32 grit_y[10] = {151125, 148486, 152628, 146893, 146176, 159708, 152085, 154059, 156496, 150327};
const i32 mark_x[5] = {258229, 263550, 268871, 274192, 279513};
const i32 mark_y[5] = {150528, 153600, 156672, 159744, 162816};
// The symbol on the card holder.
const i32 symbol_x = 205021, symbol_y = 181248;
}
}

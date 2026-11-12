#pragma once
// Written by tools/make_panel.py: edit the drawing there, then run it.
// The cabinet's open drawer: its nodes, and where each moving part stands on
// screen, Q10 px (1024 a pixel).
#include <opxy/types.hpp>

namespace curio {
namespace art {
using opxy::i32;
const unsigned node_count = 99, header_node = 91;
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
    spec_ball_ball = 39,
    spec_ball_glint = 40,
    spec_cask_cask = 41,
    spec_cask_hoop_top = 42,
    spec_cask_hoop_foot = 43,
    spec_cask_bung = 44,
    spec_stone_stone = 45,
    spec_stone_hole = 46,
    spec_stone_chip = 47,
    spec_drip_drop = 48,
    spec_drip_shine = 49,
    spec_ice_ice = 50,
    spec_ice_crack = 51,
    spec_sheet_sheet = 52,
    spec_sheet_hole_l = 53,
    spec_sheet_hole_r = 54,
    spec_sheet_fold = 55,
    spec_spark_jar = 56,
    spec_spark_foil = 57,
    spec_spark_rod = 58,
    spec_spark_knob = 59,
    spec_cork_cork = 60,
    spec_cork_grain = 61,
    spec_cork_screw = 62,
    spec_cork_handle = 63,
    spec_cicada_cicada = 64,
    spec_cicada_collar = 65,
    spec_cicada_seam = 66,
    spec_cicada_vein_l = 67,
    spec_cicada_vein_r = 68,
    spec_ratchet_wheel = 69,
    spec_ratchet_hub = 70,
    spec_ratchet_handle = 71,
    spec_pod_pod = 72,
    spec_pod_seam = 73,
    spec_pod_stalk = 74,
    spec_match_stick = 75,
    spec_match_head = 76,
    spec_match_flame = 77,
    spec_glass_glass = 78,
    spec_glass_water = 79,
    spec_bell_jar_dome = 80,
    spec_bell_jar_bell = 81,
    spec_bell_jar_clapper = 82,
    spec_bell_jar_knob = 83,
    spec_chalk_slate = 84,
    spec_chalk_face = 85,
    spec_chalk_squeal = 86,
    spec_roar_slat = 87,
    spec_roar_hole = 88,
    spec_roar_groove = 89,
    spec_roar_cord = 90
};
// The sixteen specimens in the kit's order: each one's first node, its node
// count, and its height at scale 1 (Q10 px); its five sizes (encoder 4) as a
// scale (Q10); the gap between the clamp and the specimen it holds (Q10 px).
const unsigned spec_first[16] = {39, 41, 45, 48, 50, 52, 56, 60, 64, 69, 72, 75, 78, 80, 84, 87};
const unsigned spec_count[16] = {2, 4, 3, 2, 2, 4, 4, 4, 5, 3, 3, 3, 2, 4, 3, 4};
const i32 spec_h[16] = {20480, 24576, 20480, 27648, 26624, 27648, 28672, 27136, 28672, 27493, 28526, 29184, 27648, 30742, 22528, 29889};
const i32 spec_scale[5] = {819, 922, 1024, 1147, 1280};
const i32 gap = 2048;
// The plumb line's foot on the head, where the specimen lands; the drop over
// the head at DROP 0, and how much more at 100. The specimen hangs from the
// clamp, its top the gap under it.
const i32 plumb_x = 252908, plumb_y = 147456;
const i32 drop_lo = 43008, drop_span = 30720;
// The clamp and the stand's upper length at DROP 0: they rise with the drop.
const i32 arm_x = 238276, arm_y = 93952;
const i32 stand_x = 220983, stand_y = 105472;
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
}
}

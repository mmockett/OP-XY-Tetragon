#pragma once
// Written by tools/make_panel.py: each node's outline as authored, its reach
// about the node's origin in px: left, top, right, bottom.
namespace curio {
namespace bounds {
const float node[65][4] = {
    {0, 0, 0, 0},   // the plumb line: its points are in the frame
    {-8.66f, -3.50f, 0.00f, 8.50f},   // 1 crate_left
    {0.00f, -3.50f, 8.66f, 8.50f},   // 2 crate_right
    {-8.66f, -8.50f, 8.66f, 1.50f},   // 3 crate_top
    {-4.33f, -1.00f, 0.87f, 3.00f},   // 4 pad_left
    {0.87f, 0.00f, 4.33f, 3.00f},   // 5 pad_right
    {-4.33f, -3.00f, 4.33f, 2.00f},   // 6 pad_top
    {-2.20f, -1.27f, 2.20f, 1.27f},   // 7 spot
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 8 rod_0
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 9 rod_1
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 10 rod_2
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 11 rod_3
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 12 rod_4
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 13 rod_5
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 14 rod_6
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 15 rod_7
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 16 rod_8
    {-0.15f, 0.00f, 0.15f, 14.00f},   // 17 rod_9
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 18 grit_0
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 19 grit_1
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 20 grit_2
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 21 grit_3
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 22 grit_4
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 23 grit_5
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 24 grit_6
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 25 grit_7
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 26 grit_8
    {-0.80f, -0.80f, 0.80f, 0.80f},   // 27 grit_9
    {-1.73f, -21.00f, 0.00f, 22.00f},   // 28 stand_left
    {0.00f, -21.00f, 1.73f, 22.00f},   // 29 stand_right
    {-1.73f, -22.00f, 1.73f, -20.00f},   // 30 stand_top
    {-12.99f, -7.50f, 9.53f, 9.50f},   // 31 arm_left
    {9.53f, 3.50f, 12.99f, 9.50f},   // 32 arm_right
    {-12.99f, -9.50f, 12.99f, 5.50f},   // 33 arm_top
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 34 mark_0
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 35 mark_1
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 36 mark_2
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 37 mark_3
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 38 mark_4
    {-4.02f, -8.05f, 4.02f, 0.00f},   // 39 ball_0
    {-2.54f, -6.16f, -0.28f, -4.71f},   // 40 glint_0
    {-4.89f, -9.77f, 4.89f, 0.00f},   // 41 ball_1
    {-3.08f, -7.48f, -0.34f, -5.72f},   // 42 glint_1
    {-5.75f, -11.50f, 5.75f, 0.00f},   // 43 ball_2
    {-3.62f, -8.80f, -0.40f, -6.73f},   // 44 glint_2
    {-6.61f, -13.22f, 6.61f, 0.00f},   // 45 ball_3
    {-4.17f, -10.12f, -0.46f, -7.74f},   // 46 glint_3
    {-7.47f, -14.95f, 7.47f, 0.00f},   // 47 ball_4
    {-4.71f, -11.44f, -0.52f, -8.75f},   // 48 glint_4
    {-2.20f, -2.20f, 2.20f, 2.20f},   // 49 symbol_ball
    {-2.10f, -2.60f, 2.10f, 2.60f},   // 50 symbol_cask
    {-3.40f, -1.20f, 3.40f, 1.20f},   // 51 symbol_stone
    {-1.48f, -2.80f, 1.48f, 2.40f},   // 52 symbol_drip
    {-2.08f, -2.40f, 2.08f, 2.40f},   // 53 symbol_ice
    {-1.80f, -2.60f, 2.00f, 2.60f},   // 54 symbol_sheet
    {-2.80f, -2.80f, 2.80f, 2.80f},   // 55 symbol_spark
    {-1.60f, -2.20f, 1.60f, 2.20f},   // 56 symbol_cork
    {-2.00f, -2.80f, 2.00f, 2.80f},   // 57 symbol_cicada
    {-2.56f, -2.56f, 2.56f, 2.56f},   // 58 symbol_ratchet
    {-1.50f, -2.60f, 1.50f, 2.60f},   // 59 symbol_pod
    {-0.80f, -2.70f, 0.80f, 2.80f},   // 60 symbol_match
    {-2.00f, -2.80f, 2.00f, 2.80f},   // 61 symbol_glass
    {-2.40f, -2.60f, 2.40f, 2.40f},   // 62 symbol_bell_jar
    {-2.18f, -2.76f, 2.18f, 2.76f},   // 63 symbol_chalk
    {-3.01f, -1.44f, 3.01f, 1.44f},   // 64 symbol_roar
};
}
}

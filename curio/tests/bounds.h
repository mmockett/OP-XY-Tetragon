#pragma once
// Written by tools/make_panel.py: each node's outline as authored, its reach
// about the node's origin in px: left, top, right, bottom.
namespace curio {
namespace bounds {
const float node[91][4] = {
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
    {-1.73f, -25.00f, 0.00f, 26.00f},   // 28 stand_left
    {0.00f, -25.00f, 1.73f, 26.00f},   // 29 stand_right
    {-1.73f, -26.00f, 1.73f, -24.00f},   // 30 stand_top
    {-18.62f, -10.75f, 15.16f, 12.75f},   // 31 arm_left
    {15.16f, 6.75f, 18.62f, 12.75f},   // 32 arm_right
    {-18.62f, -12.75f, 18.62f, 8.75f},   // 33 arm_top
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 34 mark_0
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 35 mark_1
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 36 mark_2
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 37 mark_3
    {-2.50f, -1.00f, 2.50f, 1.00f},   // 38 mark_4
    {-10.00f, -20.00f, 10.00f, 0.00f},   // 39 spec_ball_ball
    {-6.40f, -15.80f, -1.20f, -12.60f},   // 40 spec_ball_glint
    {-10.00f, -24.00f, 10.00f, 0.00f},   // 41 spec_cask_cask
    {-8.90f, -20.20f, 8.90f, -18.80f},   // 42 spec_cask_hoop_top
    {-8.90f, -5.20f, 8.90f, -3.80f},   // 43 spec_cask_hoop_foot
    {-1.60f, -13.52f, 1.60f, -10.48f},   // 44 spec_cask_bung
    {-13.00f, -20.00f, 13.00f, 0.00f},   // 45 spec_stone_stone
    {-9.00f, -16.00f, -5.00f, -12.00f},   // 46 spec_stone_hole
    {2.72f, -9.64f, 10.28f, -5.36f},   // 47 spec_stone_chip
    {-8.00f, -27.00f, 8.00f, 0.00f},   // 48 spec_drip_drop
    {-4.50f, -12.10f, -1.90f, -6.90f},   // 49 spec_drip_shine
    {-10.00f, -26.00f, 12.00f, 0.00f},   // 50 spec_ice_ice
    {-3.15f, -23.14f, 1.33f, -0.77f},   // 51 spec_ice_crack
    {-10.00f, -27.00f, 10.00f, -0.40f},   // 52 spec_sheet_sheet
    {-7.30f, -24.74f, -4.70f, -22.26f},   // 53 spec_sheet_hole_l
    {4.70f, -24.74f, 7.30f, -22.26f},   // 54 spec_sheet_hole_r
    {-10.00f, -9.60f, 10.00f, -8.40f},   // 55 spec_sheet_fold
    {-8.00f, -17.00f, 8.00f, 0.00f},   // 56 spec_spark_jar
    {-8.50f, -9.70f, 8.50f, -8.30f},   // 57 spec_spark_foil
    {-0.80f, -24.00f, 0.80f, -16.50f},   // 58 spec_spark_rod
    {-3.00f, -28.00f, 3.00f, -22.00f},   // 59 spec_spark_knob
    {-6.50f, -14.00f, 6.50f, 0.00f},   // 60 spec_cork_cork
    {-3.50f, -7.60f, 3.50f, -6.40f},   // 61 spec_cork_grain
    {-0.80f, -23.00f, 0.80f, -14.00f},   // 62 spec_cork_screw
    {-8.00f, -26.50f, 8.00f, -23.00f},   // 63 spec_cork_handle
    {-10.00f, -28.00f, 10.00f, 0.00f},   // 64 spec_cicada_cicada
    {-6.00f, -23.80f, 6.00f, -22.60f},   // 65 spec_cicada_collar
    {-0.60f, -22.60f, 0.60f, -4.60f},   // 66 spec_cicada_seam
    {-6.88f, -19.63f, -2.12f, -5.37f},   // 67 spec_cicada_vein_l
    {2.12f, -19.63f, 6.88f, -5.37f},   // 68 spec_cicada_vein_r
    {-9.90f, -26.85f, 9.90f, -7.15f},   // 69 spec_ratchet_wheel
    {-2.60f, -19.60f, 2.60f, -14.40f},   // 70 spec_ratchet_hub
    {-1.80f, -9.00f, 1.80f, 0.00f},   // 71 spec_ratchet_handle
    {-6.50f, -23.00f, 6.50f, 0.00f},   // 72 spec_pod_pod
    {-0.60f, -21.08f, 1.80f, -1.92f},   // 73 spec_pod_seam
    {-0.72f, -27.86f, 3.22f, -22.14f},   // 74 spec_pod_stalk
    {-1.40f, -15.00f, 1.40f, 0.00f},   // 75 spec_match_stick
    {-2.60f, -19.40f, 2.60f, -12.60f},   // 76 spec_match_head
    {-3.39f, -28.50f, 3.39f, -19.30f},   // 77 spec_match_flame
    {-7.50f, -27.00f, 7.50f, 0.00f},   // 78 spec_glass_glass
    {-7.00f, -22.15f, 7.00f, -20.85f},   // 79 spec_glass_water
    {-12.50f, -27.00f, 12.50f, 0.00f},   // 80 spec_bell_jar_dome
    {-7.50f, -19.00f, 7.50f, -5.00f},   // 81 spec_bell_jar_bell
    {-1.30f, -5.44f, 1.30f, -2.96f},   // 82 spec_bell_jar_clapper
    {-1.60f, -30.02f, 1.60f, -26.98f},   // 83 spec_bell_jar_knob
    {-13.00f, -22.00f, 13.00f, 0.00f},   // 84 spec_chalk_slate
    {-11.00f, -20.00f, 11.00f, -2.00f},   // 85 spec_chalk_face
    {-8.63f, -16.40f, 8.63f, -5.60f},   // 86 spec_chalk_squeal
    {-5.97f, -22.50f, 5.97f, 0.50f},   // 87 spec_roar_slat
    {-1.30f, -20.24f, 1.30f, -17.76f},   // 88 spec_roar_hole
    {-0.60f, -15.00f, 0.60f, -3.00f},   // 89 spec_roar_groove
    {-0.41f, -29.19f, 3.41f, -22.31f},   // 90 spec_roar_cord
};
}
}

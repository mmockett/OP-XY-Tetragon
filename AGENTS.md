# Machine development

This repository holds the Tetragon Suite: four synthesis drum kits, one per
folder (`thunder`, `curio`, `galileo`, `syahi`). Read README.md and each
kit's own README first.

All four build against the [Open-XY SDK](https://github.com/etcetc000/open-xy)
`staging` branch: per-key mixer channels (`padMix`) and per-key sends in 1%
steps (SDK 1.15). GALILEO also defines `drum_transport`, the optional callback
that passes a kit the project's tempo (etcetc000/open-xy#443). Without it
GALILEO learns the beat from its own notes. Do not
edit the SDK to make a kit work; propose an SDK change separately.

Follow the SDK's [machine API](https://github.com/etcetc000/open-xy/blob/staging/docs/api.md),
[UX conventions](https://github.com/etcetc000/open-xy/blob/staging/docs/ux.md)
and [display rules](https://github.com/etcetc000/open-xy/blob/staging/docs/display.md).
For every kit you change, run

```sh
python ../open-xy/sdk.py test --machine <kit>
python ../open-xy/sdk.py test --machine <kit> --firmware
```

and keep generated builds, firmware, stock OS files and the screen font
outside the repository. CI (`.github/workflows/checks.yml`) runs the first
for all four kits and compiles them together into one firmware module.

## Rules

- **One kit, one folder.** A change to one kit touches only its folder.
  Shared tools live in `tools/`.
- **Written from first principles.** No code, tables, constants or samples
  from another instrument, drum machine or ROM. Measure, then write what you
  learn as formulas or fitted values.
- **Fixed point.** The OP-XY's Blackfin has no floating point, and 64-bit
  multiplies and divides call runtime helpers the firmware lacks.
- **Generated files stay generated.** `tables.h`, `levels.h`, `art.h`,
  `panel.svg.fragment` and the like are written by the kit's `tools/`; change
  the generator and run it, then commit both.
- **Presets.** A command-line build installs no presets; each kit's
  `tools/presets.py` writes them. Keep the presets and the README's preset
  table in step.

## Screens: check them the way the OP-XY draws them

The browser preview does not show everything the device does.

- **Judge at 1:1.** The screen is 480 × 222 px. No detail under about 3–4 px,
  no lines closer than about 3 px, no text under about 8 px.
- **Measure text in the device's font.** The OP-XY draws text in Univers Next
  Pro Light, which is stock content and never committed here. Header values
  are 20 px and must fit in 53 px; titles are 10 px and must fit in 56 px.
  Measure captions with `tools/caption_width.py` (set `XY_FONT` to your copy
  of the font) and make a too-wide caption fail the kit's tests.
- **Follow the device's rules for reshaping outlines.** An outline with no
  area, points closer than 1/8 px or a closing edge under 1/4 px draws a
  streak on the device. Draw lines as thin closed ribbons. Each kit's
  `tests/screen.cpp` checks its outlines over a sweep of its controls; keep
  it passing.
- **Keep joined parts joined.** Build parts that must meet as one outline, or
  as nodes sharing one pivot, so the device cannot place them apart.
- **Keep the device fast.** About 20 changed nodes a frame on average, at
  every control setting; nothing that moves is filled; an idle screen changes
  nothing. The device fills only nodes whose artwork has a fill, and takes
  one subpath of at most 64 points per path.

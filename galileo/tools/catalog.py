"""GALILEO's catalog: the models, their controls and defaults, the default kit
and the named kit presets. tools/make_kit.py writes kit.h from it, and
tools/presets.py the kit presets. Everything here is first-principles: no
drum-machine tables or constants from anywhere else.

Controls are 0..127, as the drum kind stores them. Slots 0..3 are the body
(the encoders' first row), slots 4..7 the motion (the second row), the same
four on every model: tilt, frets, bounce, balls.
"""

MOTION = ("tilt", "frets", "bounce", "balls")

# name, group, the body's four controls, their defaults (0..127), motion defaults,
# the output gain (Q28 multiplier, calibrated by tools/calibrate.py so each
# model's default key peaks at its level).
MODELS = [
    # SKIN
    ("KICK",    "SKIN",  ("tune", "punch", "decay", "beater"), (44, 57, 44, 38), (88, 0, 0, 0)),
    ("SNARE",   "SKIN",  ("tune", "wires", "decay", "tone"),   (57, 76, 44, 64), (88, 0, 0, 0)),
    ("TOM",     "SKIN",  ("tune", "decay", "bend", "skin"),    (51, 44, 44, 51), (88, 0, 0, 0)),
    ("FILL",    "SKIN",  ("tune", "decay", "spread", "skin"),  (32, 38, 57, 51), (57, 40, 0, 0)),
    ("HAND",    "SKIN",  ("tune", "slap", "decay", "mute"),    (57, 32, 51, 0),  (88, 0, 0, 0)),
    ("RIM",     "SKIN",  ("tune", "wood", "decay", "body"),    (51, 76, 38, 51), (88, 0, 0, 0)),
    # METAL
    ("HAT",     "METAL", ("tune", "decay", "tone", "metal"),   (64, 23, 70, 64), (88, 0, 0, 0)),
    ("RIDE",    "METAL", ("tune", "decay", "bell", "wash"),    (57, 57, 44, 51), (88, 0, 0, 0)),
    ("BELL",    "METAL", ("tune", "decay", "pair", "metal"),   (51, 38, 51, 57), (64, 8, 0, 0)),
    ("TINE",    "METAL", ("tune", "decay", "step", "hard"),    (51, 44, 51, 57), (64, 32, 0, 0)),
    ("CLOCK",   "METAL", ("tune", "decay", "tock", "case"),    (57, 38, 51, 44), (57, 24, 0, 0)),
    # PERC
    ("BLOCK",   "PERC",  ("tune", "decay", "hollow", "click"), (57, 44, 57, 51), (88, 0, 0, 0)),
    ("CLAP",    "PERC",  ("tone", "decay", "room", "snap"),    (57, 44, 38, 64), (88, 0, 0, 36)),
    ("SHAKER",  "PERC",  ("tone", "decay", "shell", "grit"),   (64, 38, 44, 51), (88, 0, 0, 70)),
    ("RATCHET", "PERC",  ("tune", "decay", "teeth", "wood"),   (51, 38, 57, 76), (100, 47, 0, 0)),
]
NAMES = [m[0] for m in MODELS]
# The names the header shows (MACHINE), in lower case to fit its 53 px: a
# shaker of shot is "shot", a ratchet the "rattle" it is.
SHOWN = {"SHAKER": "shot", "RATCHET": "rattle"}
GROUPS = [("SKIN", 6), ("METAL", 5), ("PERC", 4)]

# Each model's default key (calibrate.py) is set to its role's loudness: the
# loudest 400 ms, K-weighted (LUFS-M max), in dB re the SDK's half-scale
# ceiling. The Tetragon suite's common targets: the low drums (kicks, toms,
# the tenor fill) at -16.5, everything else at -19, MD KIT's loudest-sitting
# kick (-17.7) and snare (-21.4) for scale. A default key peaks under the
# engine's soft knee (0.42 of full scale) and its true peak at most TRUE_PEAK.
ROLE = {"KICK": "low", "TOM": "low", "FILL": "low"}
ROLE.update({m[0]: "rest" for m in MODELS if m[0] not in ROLE})
TARGET = {"low": -16.5, "rest": -19.0}
TRUE_PEAK = -1.0


def model_index(name):
    return NAMES.index(name)


PAD = ("space", "feel", "fx1", "fx2", "pan", "level")


class Key(tuple):
    """A key: (model, eight controls 0..127), and .pad, its pad fields and mixer
    channel: SPACE -16..15, FEEL 0..15, FX I and FX II codes 0..7 (0 follows the
    track's send, 1..7 send 0, 17, 33, 50, 67, 83 or 100), pan -100..100 and
    level -30..0 dB. All 0 is neutral."""
    pad = {}


def key(model, **over):
    """A key: a model and its eight controls, its defaults with some changed, and
    any pad fields."""
    m = MODELS[model_index(model)]
    values = dict(zip(m[2], m[3]))
    values.update(zip(MOTION, m[4]))
    pad = {}
    for k, v in over.items():
        if k in PAD:
            pad[k] = v
            continue
        assert k in values, f"{model} has no control {k}"
        assert 0 <= v <= 127, f"{model} {k}={v}"
        values[k] = v
    assert -16 <= pad.get("space", 0) <= 15 and 0 <= pad.get("feel", 0) <= 15
    assert 0 <= pad.get("fx1", 0) <= 7 and 0 <= pad.get("fx2", 0) <= 7
    assert -100 <= pad.get("pan", 0) <= 100 and -30 <= pad.get("level", 0) <= 0
    k = Key((model, [values[c] for c in m[2] + MOTION]))
    k.pad = pad
    return k


# Motion values that land on musical settings (tools/motion.py checks them):
# tilt in GRID LOCK picks an eighth of its travel each: 1/4, 1/8, 1/8T, 1/16,
# 1/16T, 1/32, 1/32T, 1/64.
TILT = {"1/4": 8, "1/8": 24, "1/8T": 40, "1/16": 56, "1/16T": 72, "1/32": 88, "1/32T": 104, "1/64": 120}


def frets(n):
    """The frets control for n frets (0..16)."""
    return min(127, round(n * 127 / 16))


def balls(m):
    """The balls control for a handful of m (1..24): the inverse of ball_count."""
    if m <= 1:
        return 0
    return min(127, round(((m - 1) / 23) ** (1 / 1.6) * 127))


def bounce(e):
    """The bounce control for a restitution e (0..0.93)."""
    return min(127, round((e / 0.93) ** (1 / 0.5) * 127))


# The default kit (keys 53..76), as the concept lays it out.
DEFAULT_KIT = [
    key("KICK"),
    key("KICK", bounce=bounce(0.5), tilt=TILT["1/16"]),                 # a rebound on the next 1/16
    key("SNARE"),
    key("SNARE", balls=balls(2)),                                        # flam
    key("RIM"),
    key("CLAP"),
    key("HAT"),
    key("HAT", frets=frets(4), tilt=TILT["1/32"]),                       # four 1/32s into the next 1/8
    key("HAT", decay=96, tone=64),                                       # open
    key("TOM", tune=30, decay=58),                                       # low
    key("SHAKER"),
    key("TOM", tune=82, decay=40),                                       # high
    key("RIDE"),
    key("FILL", frets=frets(5), tilt=TILT["1/16"]),
    key("BELL", frets=frets(1)),
    key("SNARE", frets=frets(2), tilt=TILT["1/32"]),                     # drag
    key("BLOCK"),
    key("HAND"),
    key("TINE", frets=frets(4), tilt=TILT["1/16"]),
    key("HAND", slap=86, mute=51, decay=38),                             # slap
    key("RATCHET"),
    key("CLOCK"),
    key("SNARE", frets=frets(12), tilt=TILT["1/32"], decay=36),          # a roll into the accent
    key("RIDE", bounce=bounce(0.9), tilt=72),                            # a coin settling
]

# Keys whose notes cut each other's in stock's mute groups (none in the default kit:
# chokeSameKey already gives each key one voice).
CHOKED = set()

# The named kit presets: 24 keys each.
def _clockwork():   # trap and hip hop: 808 kick with locked rebounds, one-note hat rolls
    k808 = dict(tune=24, punch=51, decay=108, beater=32)
    hat = dict(tune=70, decay=18, tone=76, metal=57)
    return [
        key("KICK", **k808),
        key("KICK", **k808, bounce=bounce(0.5), tilt=TILT["1/16"]),
        key("CLAP", tone=64, decay=44, room=44, snap=70, balls=balls(4)),
        key("SNARE", tune=70, wires=89, decay=38, tone=76),
        key("RIM", tune=57, wood=89, decay=32, body=25, bounce=bounce(0.55), tilt=TILT["1/16T"]),
        key("CLAP", tone=57, decay=76, room=89, snap=51, balls=balls(6)),
        key("HAT", **hat),
        key("HAT", **hat, frets=frets(4), tilt=TILT["1/32"]),
        key("HAT", **{**hat, "decay": 70}),
        key("HAT", **hat, frets=frets(3), tilt=TILT["1/32T"]),
        key("HAT", **hat, frets=frets(8), tilt=TILT["1/64"]),
        key("KICK", **{**k808, "tune": 40}, bounce=bounce(0.35), tilt=TILT["1/16T"]),
        key("KICK", **{**k808, "tune": 52, "decay": 94}),
        key("SNARE", tune=70, wires=89, decay=34, tone=76, frets=frets(12), tilt=TILT["1/32"]),
        key("RATCHET", tune=57, decay=32, teeth=51, wood=76, frets=frets(4), tilt=110),
        key("BELL", tune=70, decay=25, pair=76, metal=51, frets=frets(2), tilt=TILT["1/16T"]),
        key("RIDE", tune=64, decay=44, bell=89, wash=32),
        key("SHAKER", tone=76, decay=25, shell=38, grit=44, balls=balls(8)),
        key("TOM", tune=40, decay=64, bend=89, skin=38),
        key("TOM", tune=60, decay=56, bend=89, skin=38),
        key("TINE", tune=64, decay=51, step=64, hard=64, frets=frets(4), tilt=TILT["1/16T"]),
        key("BLOCK", tune=76, decay=32, hollow=38, click=76),
        key("SNARE", tune=64, wires=76, decay=44, tone=70, balls=balls(2)),
        key("CLAP", tone=70, decay=38, room=25, snap=89, balls=balls(3), frets=frets(2), tilt=TILT["1/32"]),
    ]


def _rudiments():   # marching and drumline: flams, drags, ruffs, rolls
    sn = dict(tune=76, wires=96, decay=34, tone=89)
    ten = dict(tune=57, decay=40, spread=70, skin=57)
    bass = dict(tune=40, punch=25, decay=77, beater=64)
    return [
        key("KICK", **bass),
        key("KICK", **bass, balls=balls(2)),
        key("SNARE", **sn),
        key("SNARE", **sn, balls=balls(2)),                                      # flam
        key("SNARE", **sn, frets=frets(2), tilt=TILT["1/32"]),                    # drag
        key("SNARE", **sn, frets=frets(3), tilt=TILT["1/32"]),                    # ruff
        key("SNARE", **sn, frets=frets(4), tilt=TILT["1/32"]),                    # five-stroke
        key("SNARE", **sn, frets=frets(16), tilt=TILT["1/64"]),                   # press roll
        key("SNARE", **sn, frets=frets(6), tilt=TILT["1/16T"]),                   # six-stroke
        key("RIM", tune=76, wood=25, decay=25, body=64),
        key("FILL", **ten, frets=frets(3), tilt=TILT["1/16"]),
        key("FILL", **ten, frets=frets(5), tilt=TILT["1/16T"]),
        key("TOM", tune=64, decay=38, bend=25, skin=76),
        key("TOM", tune=80, decay=34, bend=25, skin=76),
        key("TOM", tune=96, decay=30, bend=25, skin=76),
        key("FILL", **ten, frets=frets(8), tilt=TILT["1/32"]),
        key("KICK", **{**bass, "tune": 52}),
        key("KICK", **{**bass, "tune": 64}),
        key("RIDE", tune=76, decay=38, bell=25, wash=70),                         # crash-like cymbal
        key("BELL", tune=64, decay=44, pair=0, metal=76),
        key("HAT", tune=70, decay=20, tone=89, metal=76),
        key("CLAP", tone=76, decay=32, room=19, snap=96, balls=balls(5)),
        key("SNARE", **sn, frets=frets(9), tilt=TILT["1/32"], bounce=bounce(0.3)),
        key("SNARE", **{**sn, "wires": 25}, frets=frets(4), tilt=TILT["1/16"]),    # tenor-like, wires off
    ]


def _workshop():    # broken beat and acoustic funk: ghost drags, a shaker of shot, a tick-tock clock
    return [
        key("KICK", tune=57, punch=38, decay=75, beater=70, bounce=bounce(0.3), tilt=TILT["1/16"]),
        key("KICK", tune=57, punch=38, decay=75, beater=70),
        key("SNARE", tune=57, wires=70, decay=48, tone=57, balls=balls(2)),
        key("SNARE", tune=57, wires=70, decay=38, tone=51, frets=frets(2), tilt=TILT["1/32"]),
        key("SHAKER", tone=57, decay=44, shell=38, grit=51, balls=balls(18), frets=frets(1), tilt=TILT["1/8"]),
        key("SHAKER", tone=57, decay=32, shell=38, grit=44, balls=balls(10)),
        key("HAT", tune=57, decay=25, tone=64, metal=51),
        key("HAT", tune=57, decay=64, tone=60, metal=51),
        key("TINE", tune=57, decay=51, step=44, hard=51, frets=frets(5), bounce=bounce(0.4), tilt=TILT["1/16"]),
        key("FILL", tune=32, decay=38, spread=64, skin=57, frets=frets(6), tilt=TILT["1/16"]),
        key("CLOCK", tune=51, decay=32, tock=57, case=51, frets=frets(3), tilt=TILT["1/8"]),
        key("HAND", tune=51, slap=25, decay=57, mute=0, bounce=bounce(0.3), tilt=TILT["1/16"]),
        key("HAND", tune=51, slap=108, decay=38, mute=51),
        key("RIDE", tune=57, decay=64, bell=57, wash=44, bounce=bounce(0.9), tilt=110),
        key("RIDE", tune=57, decay=64, bell=57, wash=44),
        key("RIM", tune=51, wood=96, decay=32, body=64),
        key("BLOCK", tune=51, decay=51, hollow=89, click=38),
        key("TOM", tune=44, decay=64, bend=38, skin=64),
        key("TOM", tune=70, decay=51, bend=38, skin=64),
        key("CLAP", tone=51, decay=51, room=57, snap=51, balls=balls(4)),
        key("RATCHET", tune=44, decay=38, teeth=102, wood=102, frets=frets(3), tilt=TILT["1/16"]),
        key("BELL", tune=44, decay=44, pair=64, metal=38, frets=frets(1), tilt=TILT["1/8"]),
        key("SNARE", tune=57, wires=70, decay=44, tone=57, frets=frets(7), tilt=TILT["1/32"]),
        key("KICK", tune=44, punch=32, decay=90, beater=57, bounce=bounce(0.6), tilt=TILT["1/8"]),
    ]


def _marble():      # melodic and IDM: tine and block runs down a pentatonic scale
    return [
        key("KICK", tune=38, punch=64, decay=90, beater=25),
        key("TINE", tune=51, decay=64, step=32, hard=44, frets=frets(4), tilt=TILT["1/16"]),
        key("TINE", tune=51, decay=64, step=64, hard=44, frets=frets(6), tilt=TILT["1/16T"]),
        key("TINE", tune=64, decay=76, step=96, hard=57, frets=frets(8), tilt=TILT["1/32"]),
        key("TINE", tune=38, decay=89, step=32, hard=32, frets=frets(3), bounce=bounce(0.5), tilt=TILT["1/8"]),
        key("TINE", tune=76, decay=51, step=127, hard=70, frets=frets(12), tilt=TILT["1/32T"]),
        key("BLOCK", tune=64, decay=44, hollow=96, click=38, frets=frets(3), tilt=TILT["1/16"]),
        key("BLOCK", tune=89, decay=32, hollow=57, click=64, frets=frets(5), tilt=TILT["1/32"]),
        key("BELL", tune=57, decay=57, pair=57, metal=32, frets=frets(3), tilt=TILT["1/16"]),
        key("BELL", tune=70, decay=44, pair=102, metal=51, frets=frets(5), tilt=TILT["1/16T"]),
        key("CLOCK", tune=64, decay=38, tock=76, case=64, frets=frets(6), tilt=TILT["1/16"]),
        key("HAT", tune=76, decay=14, tone=89, metal=38, frets=frets(3), tilt=TILT["1/32"]),
        key("RIM", tune=64, wood=89, decay=32, body=32, bounce=bounce(0.6), tilt=TILT["1/16"]),
        key("SNARE", tune=64, wires=64, decay=32, tone=76),
        key("SHAKER", tone=89, decay=32, shell=89, grit=32, balls=balls(7)),
        key("RIDE", tune=70, decay=76, bell=102, wash=19),
        key("FILL", tune=44, decay=51, spread=38, skin=76, frets=frets(4), tilt=TILT["1/16T"]),
        key("HAND", tune=64, slap=19, decay=64, mute=19),
        key("TINE", tune=44, decay=102, step=0, hard=25, frets=frets(16), tilt=TILT["1/64"]),
        key("BLOCK", tune=38, decay=64, hollow=127, click=19, bounce=bounce(0.75), tilt=TILT["1/8"]),
        key("RATCHET", tune=76, decay=25, teeth=25, wood=25, frets=frets(8), tilt=TILT["1/32"]),
        key("CLAP", tone=76, decay=32, room=76, snap=51, balls=balls(3)),
        key("BELL", tune=44, decay=76, pair=32, metal=19, bounce=bounce(0.6), tilt=TILT["1/16"]),
        key("KICK", tune=50, punch=76, decay=77, beater=44, bounce=bounce(0.45), tilt=TILT["1/16"]),
    ]


def _shot():        # Afro-Latin percussion: conga and bongo, shaker, cabasa, guiro, agogo, clave
    return [
        key("KICK", tune=38, punch=32, decay=86, beater=51),
        key("HAND", tune=44, slap=19, decay=64, mute=0, bounce=bounce(0.25), tilt=TILT["1/16"]),   # conga open
        key("HAND", tune=44, slap=102, decay=38, mute=38),                                        # conga slap
        key("HAND", tune=44, slap=19, decay=44, mute=102),                                        # conga muted
        key("HAND", tune=76, slap=32, decay=51, mute=0, bounce=bounce(0.3), tilt=TILT["1/16T"]),   # bongo
        key("HAND", tune=89, slap=89, decay=32, mute=25),
        key("HAND", tune=32, slap=25, decay=76, mute=0),                                          # tumba
        key("SHAKER", tone=57, decay=32, shell=32, grit=51, balls=balls(12)),
        key("SHAKER", tone=57, decay=32, shell=32, grit=51, balls=balls(12), frets=frets(1), tilt=TILT["1/16"]),
        key("SHAKER", tone=96, decay=25, shell=114, grit=32, balls=balls(18)),                   # cabasa
        key("RATCHET", tune=51, decay=51, teeth=127, wood=127, frets=frets(1), tilt=TILT["1/16"]),  # guiro
        key("RATCHET", tune=51, decay=38, teeth=127, wood=127, frets=frets(3), tilt=TILT["1/16"]),
        key("BELL", tune=51, decay=44, pair=76, metal=57, frets=frets(1), tilt=TILT["1/8"]),      # agogo
        key("BELL", tune=51, decay=44, pair=76, metal=57, frets=frets(3), tilt=TILT["1/16"]),
        key("BLOCK", tune=89, decay=38, hollow=19, click=70),                                     # clave
        key("BLOCK", tune=57, decay=51, hollow=76, click=44),                                     # woodblock
        key("RIM", tune=57, wood=108, decay=32, body=76),
        key("TOM", tune=64, decay=51, bend=64, skin=51, bounce=bounce(0.3), tilt=TILT["1/16"]),   # timbale
        key("TOM", tune=89, decay=44, bend=64, skin=51),
        key("BELL", tune=32, decay=64, pair=0, metal=89),                                         # cowbell
        key("RIDE", tune=64, decay=51, bell=114, wash=19),
        key("CLAP", tone=64, decay=38, room=38, snap=64, balls=balls(5)),
        key("HAT", tune=64, decay=20, tone=76, metal=44),
        key("SHAKER", tone=44, decay=44, shell=19, grit=96, balls=balls(24), frets=frets(2), tilt=TILT["1/8"]),
    ]


def _zeno():        # texture and ambient: long bounces, coins settling, free ratchets
    return [
        key("KICK", tune=32, punch=44, decay=113, beater=19, bounce=bounce(0.8), tilt=TILT["1/8"]),
        key("KICK", tune=44, punch=32, decay=98, beater=19, bounce=bounce(0.9), tilt=TILT["1/4"]),
        key("RIDE", tune=57, decay=89, bell=70, wash=57, bounce=bounce(0.9), tilt=110),
        key("RIDE", tune=76, decay=102, bell=114, wash=25, bounce=bounce(0.93), tilt=120),
        key("BELL", tune=51, decay=89, pair=38, metal=19, bounce=bounce(0.85), tilt=TILT["1/8"]),
        key("TINE", tune=44, decay=114, step=32, hard=25, frets=frets(6), bounce=bounce(0.8), tilt=TILT["1/8T"]),
        key("TINE", tune=64, decay=102, step=64, hard=38, bounce=bounce(0.9), tilt=TILT["1/8"]),
        key("BLOCK", tune=51, decay=76, hollow=114, click=19, bounce=bounce(0.85), tilt=TILT["1/8"]),
        key("SHAKER", tone=76, decay=64, shell=102, grit=19, balls=balls(24), bounce=bounce(0.7), tilt=TILT["1/8"]),
        key("RATCHET", tune=38, decay=64, teeth=89, wood=64, frets=frets(10), tilt=24),
        key("RATCHET", tune=64, decay=57, teeth=32, wood=19, frets=frets(16), tilt=24),
        key("CLOCK", tune=44, decay=64, tock=76, case=102, frets=frets(8), tilt=TILT["1/8"]),
        key("HAND", tune=38, slap=0, decay=89, mute=0, bounce=bounce(0.85), tilt=TILT["1/8"]),
        key("TOM", tune=32, decay=102, bend=102, skin=89, bounce=bounce(0.8), tilt=TILT["1/8"]),
        key("SNARE", tune=44, wires=51, decay=89, tone=38, frets=frets(16), tilt=TILT["1/8T"]),
        key("HAT", tune=44, decay=89, tone=38, metal=89, bounce=bounce(0.9), tilt=TILT["1/16"]),
        key("CLAP", tone=38, decay=114, room=127, snap=25, balls=balls(10)),
        key("FILL", tune=25, decay=89, spread=127, skin=89, frets=frets(10), tilt=TILT["1/8"]),
        key("BELL", tune=76, decay=76, pair=114, metal=32, frets=frets(8), bounce=bounce(0.6), tilt=TILT["1/8T"]),
        key("RIM", tune=38, wood=19, decay=89, body=19, bounce=bounce(0.9), tilt=TILT["1/16"]),
        key("SHAKER", tone=32, decay=89, shell=19, grit=127, balls=balls(24), frets=frets(4), tilt=TILT["1/4"]),
        key("TINE", tune=32, decay=127, step=0, hard=19, frets=frets(2), tilt=TILT["1/4"]),
        key("RIDE", tune=38, decay=127, bell=19, wash=102),
        key("KICK", tune=20, punch=89, decay=127, beater=0),
    ]


def _escapement():  # techno and industrial: dry kick, ratchets and clocks as hats, metal bounces
    return [
        key("KICK", tune=44, punch=70, decay=40, beater=89),
        key("KICK", tune=44, punch=70, decay=40, beater=89, bounce=bounce(0.4), tilt=TILT["1/16"]),
        key("CLAP", tone=51, decay=51, room=64, snap=76, balls=balls(5)),
        key("RIM", tune=44, wood=19, decay=44, body=38),
        key("CLOCK", tune=70, decay=19, tock=38, case=19),
        key("CLOCK", tune=70, decay=19, tock=38, case=19, frets=frets(2), tilt=TILT["1/32"]),
        key("RATCHET", tune=76, decay=19, teeth=38, wood=0, frets=frets(1), tilt=TILT["1/16"]),
        key("RATCHET", tune=76, decay=19, teeth=64, wood=0, frets=frets(4), tilt=TILT["1/32"]),
        key("HAT", tune=51, decay=28, tone=57, metal=114),
        key("HAT", tune=51, decay=76, tone=57, metal=114),
        key("RIDE", tune=44, decay=57, bell=38, wash=76),
        key("BELL", tune=38, decay=32, pair=38, metal=114, bounce=bounce(0.6), tilt=TILT["1/16"]),
        key("TOM", tune=38, decay=51, bend=114, skin=25),
        key("TOM", tune=57, decay=44, bend=114, skin=25, bounce=bounce(0.5), tilt=TILT["1/16"]),
        key("SNARE", tune=51, wires=57, decay=44, tone=89),
        key("SNARE", tune=51, wires=57, decay=38, tone=89, frets=frets(8), tilt=TILT["1/32"]),
        key("BLOCK", tune=96, decay=28, hollow=19, click=102),
        key("SHAKER", tone=102, decay=19, shell=64, grit=89, balls=balls(6)),
        key("TINE", tune=38, decay=38, step=0, hard=114, bounce=bounce(0.5), tilt=TILT["1/16"]),
        key("FILL", tune=38, decay=32, spread=89, skin=19, frets=frets(4), tilt=TILT["1/16"]),
        key("CLOCK", tune=89, decay=25, tock=102, case=38, frets=frets(7), tilt=TILT["1/16"]),
        key("RATCHET", tune=57, decay=32, teeth=89, wood=38, frets=frets(2), tilt=TILT["1/8"]),
        key("KICK", tune=57, punch=96, decay=63, beater=114, balls=balls(2)),
        key("HAT", tune=51, decay=20, tone=64, metal=114, frets=frets(16), tilt=TILT["1/64"]),
    ]


# Name (file and preset name), role, keys.
PRESETS = [
    ("galileo", "all-rounder", DEFAULT_KIT),
    ("clockwork", "trap and hip hop", _clockwork()),
    ("rudiments", "marching and drumline", _rudiments()),
    ("workshop", "broken beat and acoustic funk", _workshop()),
    ("marble run", "melodic and IDM", _marble()),
    ("shot and skin", "Afro-Latin percussion", _shot()),
    ("zeno", "texture and ambient", _zeno()),
    ("escapement", "techno and industrial", _escapement()),
]
# Each kit's pad fields and mixer channels, by key (0 is key 53): SPACE and
# FEEL (tools/catalog.py PAD), the key's own sends, pan and level. The presets
# carry them (tools/presets.py); the default kit's keys on the device start
# neutral, as drum_defaults gives only models and controls.
PADS = {
    "galileo": {4: dict(pan=-15), 6: dict(pan=25), 7: dict(pan=25), 8: dict(pan=25), 9: dict(pan=20),
                10: dict(pan=40, feel=9), 11: dict(pan=-20), 12: dict(pan=-30), 14: dict(pan=-35),
                5: dict(fx2=3), 17: dict(pan=-40), 18: dict(pan=30, fx2=3), 19: dict(pan=-40),
                20: dict(pan=35), 21: dict(pan=-25), 22: dict(space=-8), 23: dict(pan=-30, fx2=4)},
    "clockwork": {2: dict(fx2=4), 4: dict(pan=-35, feel=3), 5: dict(fx2=5), 6: dict(pan=30), 7: dict(pan=30),
                  8: dict(pan=30), 9: dict(pan=30), 10: dict(pan=30), 14: dict(pan=-40), 15: dict(pan=-25, fx1=3),
                  16: dict(pan=-20), 17: dict(pan=40, feel=9), 18: dict(pan=25), 19: dict(pan=-25), 20: dict(pan=30, fx1=3)},
    "rudiments": {7: dict(space=-10), 8: dict(feel=3), 10: dict(pan=-30), 11: dict(pan=30), 12: dict(pan=-30),
                  13: dict(pan=0), 14: dict(pan=30), 15: dict(pan=-30, space=-6), 18: dict(fx2=3), 22: dict(space=6)},
    "workshop": {3: dict(feel=3), 4: dict(pan=45, feel=9), 5: dict(pan=45, feel=9), 6: dict(pan=25, feel=3),
                 7: dict(pan=25), 8: dict(pan=-30, fx2=3), 9: dict(pan=-10), 10: dict(pan=35, feel=3),
                 11: dict(pan=-40), 12: dict(pan=-40), 13: dict(pan=30, fx2=3), 14: dict(pan=30), 15: dict(pan=-15),
                 16: dict(pan=15), 17: dict(pan=-25), 18: dict(pan=25), 20: dict(pan=-35), 21: dict(pan=40)},
    "marble run": {1: dict(space=-12, pan=-30), 2: dict(pan=30, fx2=3), 3: dict(space=10, pan=-20), 4: dict(feel=10, fx2=4),
                   5: dict(space=-16, pan=20), 6: dict(pan=-35), 7: dict(pan=35, feel=4), 8: dict(pan=-25, fx1=3),
                   9: dict(pan=25, fx1=3), 10: dict(pan=40, feel=5), 11: dict(pan=20), 18: dict(space=-16, fx2=5),
                   19: dict(pan=-30), 20: dict(pan=30, feel=15), 22: dict(pan=-20, fx2=4)},
    "shot and skin": {1: dict(pan=-35), 2: dict(pan=-35), 3: dict(pan=-35), 4: dict(pan=30), 5: dict(pan=30),
                      6: dict(pan=-45), 7: dict(pan=45, feel=9), 8: dict(pan=45, feel=9), 9: dict(pan=50, feel=10),
                      10: dict(pan=-20), 11: dict(pan=-20, feel=3), 12: dict(pan=25), 13: dict(pan=25, feel=3),
                      14: dict(pan=-10), 15: dict(pan=10), 17: dict(pan=15), 18: dict(pan=20), 19: dict(pan=-25),
                      20: dict(pan=35, fx2=3), 22: dict(pan=30), 23: dict(pan=50, feel=11)},
    "zeno": {0: dict(fx2=4), 2: dict(pan=-30, fx2=5), 3: dict(pan=30, fx2=6), 4: dict(pan=-40, feel=14, fx2=5),
             5: dict(pan=40, feel=13, fx2=5), 6: dict(pan=20, space=-12), 7: dict(pan=-20, feel=12), 8: dict(pan=45, feel=14),
             9: dict(pan=-45, feel=15), 10: dict(pan=45, feel=15), 11: dict(pan=-25, feel=12), 14: dict(space=12, fx2=4),
             16: dict(fx2=7), 17: dict(space=-16, fx2=4), 18: dict(pan=35, feel=13, fx2=6), 20: dict(pan=-50, feel=14),
             21: dict(pan=50, fx2=6), 22: dict(fx2=7)},
    "escapement": {2: dict(fx1=3), 4: dict(pan=30, feel=4), 5: dict(pan=30, feel=4), 6: dict(pan=-30, feel=4),
                   7: dict(pan=-30, feel=4), 8: dict(pan=25), 9: dict(pan=25), 10: dict(pan=-20, fx2=3),
                   11: dict(pan=-25, fx1=2), 12: dict(pan=-20), 13: dict(pan=20), 16: dict(pan=35), 17: dict(pan=40, feel=4),
                   18: dict(pan=-35, fx1=3), 19: dict(pan=-15), 20: dict(pan=30, feel=5), 21: dict(pan=-30, feel=4),
                   23: dict(pan=20, feel=4)},
}
# Each kit preset's key levels (dB, -30..0), written by tools/balance.py: a
# key whose own settings sound over its role's loudness (TARGET) is cut to it;
# in the default kit only a key more than 0.75 dB over its target is cut.
import json as _json
import pathlib as _pathlib
_levels_path = _pathlib.Path(__file__).resolve().parent / "key_levels.json"
LEVELS = _json.loads(_levels_path.read_text()) if _levels_path.exists() else {}
for _name, _role, _keys in PRESETS:
    assert len(_keys) == 24, f"{_name} has {len(_keys)} keys"
    for _i, _pad in PADS.get(_name, {}).items():
        _k = _keys[_i]
        _k.pad = {**_k.pad, **_pad}
    for _i, _level in LEVELS.get(_name, {}).items():
        _k = _keys[int(_i)]
        _k.pad = {**_k.pad, "level": _level} if _level else {f: v for f, v in _k.pad.items() if f != "level"}
    for _k in _keys:
        key(_k[0], **_k.pad)   # checks the ranges

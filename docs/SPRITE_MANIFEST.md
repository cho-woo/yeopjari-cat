# Authored sprite manifests

`python3 tools/import_care_sprites.py --manifest path/to/manifest.json` validates
original PNGs, copies their bytes unchanged, and generates the C++ descriptors
and Windows resources. `python3 tools/import_care_sprites.py --check` verifies the
registered PNG checksums, canonical manifest and generated files without writing.

Schema 1 remains supported with the existing 8/12/16/24-frame layouts. Schema 2
supports 1–256 source regions per action, independently of playlist length,
exposure times, action duration and the application's refresh timer:

```json
{
  "schema": 2,
  "clips": [{
    "clip": "groom",
    "character": 0,
    "source": "groom-128.png",
    "columns": 16,
    "rows": 8,
    "frame_count": 128,
    "reference_height": 220,
    "anchor": [128, 240],
    "sequence": {
      "intro": [{"frame": 0, "ms": 350}],
      "loop": [{"frame": 1, "ms": 100}, {"frame": 2, "ms": 100}],
      "outro": [{"frame": 127, "ms": 500}],
      "finish": "complete",
      "duration_ms": 0,
      "events": []
    }
  }]
}
```

This abbreviated playlist illustrates the schema; a production 128-pose action
should explicitly list its intended source poses. Repeating a playlist step does
not create new artwork. `frame_count` can be below the grid capacity. Supplying
`frames` replaces uniform grid slicing with explicit regions and local anchors:

```json
"frames": [
  {"rect": [0, 0, 256, 256], "anchor": [128, 240]},
  {"rect": [256, 0, 256, 256], "anchor": [128, 240]}
]
```

`frames` must contain exactly `frame_count` regions. Each anchor is local to its
rectangle. The runtime applies one `reference_height` per action, using the cat's
neutral height without props; it never rescales each individual pose to fit its
silhouette. Coordinate adjustments belong in anchors, not rewritten pixels.

The playlist concatenates `intro`, `loop`, and `outro`. Each step lasts 1–60000ms;
at most 512 steps and 64 cues are allowed. `duration_ms: 0` uses their intrinsic
duration. A nonzero duration may extend the looping/held middle but cannot shorten
the entrance or ending. `finish` is `complete` or `hold`. Sleep and Box remain in
their resting region until the host explicitly enters the exit region. Jump's
three regions are selected by physical takeoff/contact and velocity, not a timer
that moves the cat independently of Box2D.

Events use absolute step indices in that concatenated playlist:

```json
"events": [{"step": 5, "offset_ms": 0, "type": "contact"}]
```

Supported event names are `contact`, `takeoff`, `landing`, and `marker`. Offsets
must fit inside the addressed step. A cue is consumed at most once per action,
including when rendering skips frames. Event registration does not enable audio;
the application's sound policy still applies.

Walk and Run also require positive `stride_pixels`, the logical travel distance
per cycle. Their playlist phase follows measured travel. When they refer to the
same original sheet, list their separate source regions and local frame indices;
the importer embeds the PNG once and aliases the Run resource to Walk. Original
resource IDs 113–192 and box prop 193 are preserved; new walking resources start
at 400, leaving the retired audio identifiers 301–314 reserved.

Limits: non-interlaced RGBA8 PNG, real transparent padding and visible artwork in
every region; 32MiB compressed per PNG, 16384px maximum side, 32Mi source pixels
per atlas, and 48Mi copied frame pixels across the registered set. These bounds
apply equally to 64, 128 and 256-frame actions. Invalid dimensions, checksums,
regions, timing or event data fail before output files are changed.

`python3 tools/render_care_sprite_review.py` uses the canonical manifest and
compiles the real C++ sampler for its previews. It supports both schemas and
checks source hashes again afterwards. Its Pillow rendering and synthetic jump
trajectory are developer previews, not Windows captures or Box2D performance
measurements. The final November 2026 upload registers 120 original source regions
from nine PNGs; high-frame-count tests use separate synthetic fixtures.

# Tuxedo material transfer for the user's videos

`transfer.py` is an offline appearance authoring tool. Authored image-gen keys
define the black/white coat. Correspondence is calculated **between the original
video frames** and transports only a Lab color-offset field. Original poses,
four-foot trajectories, canvas and final alpha are never warped or interpolated.
The app receives ready-to-render PNGs and does no optical-flow work at runtime.

The existing 20-frame Walk export remains frozen. New per-clip JSON configuration
supports a different period (Run: 13 frames), nonperiodic actions (Eat), and an
appearance key reused at another time in a front/side turn. This does not repeat
or invent motion frames. Iris color is applied to original eye pixels, while
iris material is excluded from a turn's transported field. Eat props have an
explicit preservation mask, independently checked against original RGB.

Run from the repository root; use the corrected matte export from
`tools/matte_walk_video.py`. Python dependencies are NumPy, Pillow, SciPy and
OpenCV. The tool also discovers an offline OpenCV install in `build/flow-deps`.

```sh
python3 tools/video_tuxedo/transfer.py \
  --config build/user-run-video/tuxedo/transfer-config.json \
  --key-dir build/user-run-video/tuxedo/authored-keys \
  --matte-dir build/user-run-video/matting/corrected \
  --start 57 --end 70 --output build/user-run-video/tuxedo
```

Each config records zero-based source indices and appearance key filenames (PNG
filenames are one-based), temporal weights, eye regions, and prop protection.
The resulting `transfer-proof.json` records material-key hashes, registration,
output hashes and exact source/output alpha hashes. It records numerical checks;
the fixed-size contact sheet and video are the visual review.

`review.py` makes an explicitly labelled offline original/Tuxedo comparison at
24 FPS, one fixed scale, and the original video poses. It checks every output
alpha and fully decodes the H.264/yuv420p/faststart MP4 after writing. It is not
a Windows desktop or app capture.

The v23 bridge correction uses local pose-locked material keys for newly
exposed tail/face surfaces. `temporal_mode: bracketing` prevents unrelated
front/profile views from contributing simultaneously. `absolute_material`
transports the authored coat luminance rather than retaining ginger stripes
as gray blotches, while preserving fine original detail; optional neutral coat
chroma is applied before original pink skin and iris restoration. These options
are enabled only by the bridge job config. Existing Walk loop, Run and Eat PNGs
remain unchanged. `validate_bridges.py` records alpha equality and warm-coat
counts independently of the full 41-frame visual review.

```sh
python3 tools/video_tuxedo/review.py \
  --matte-dir build/user-run-video/matting/corrected \
  --output-dir build/user-run-video/tuxedo \
  --indices 57,58,59,60,61,62,63,64,65,66,67,68,69 --repeat 12
```

These material keys are not new walk/run/eat poses. The actual motion is supplied
by the uploaded videos. There is no claim that the clipped Run video's abrupt
front-sit cut is a newly authored start or stop animation.

## Shared Tuxedo palette

`tools/lock_tuxedo_identity.py` uses **Pillow only**. It bakes one common neutral
black/white fur palette into all 16 existing runtime atlases. It does not change
alpha, white-mark locations, frame geometry, registration, motion or timing.
Original skin/blush, olive/gold eyes and explicit fish/package/hand/bowl/crumb
masks remain byte-for-byte intact. There is no recoloring cost during playback.

```sh
python3 tools/lock_tuxedo_identity.py \
  --mask-dir assets/styled-sprites/identity-masks --check
python3 tools/pack_walk_video.py
python3 tools/import_styled_sprites.py --check --require-character 8
```

The video packer always reads the retained original Tuxedo source PNGs and then
applies the same palette once. To reproduce a legacy atlas bake, obtain the
original style snapshot at `identity-policy.json`'s `input_git_revision` and use
that original directory as `--source-dir`; do not rebake already normalized PNGs:

```sh
python3 tools/lock_tuxedo_identity.py \
  --source-dir ORIGINAL_SNAPSHOT/assets/styled-sprites \
  --mask-dir assets/styled-sprites/identity-masks \
  --output build/tuxedo-identity-rebuild
```

`assets/styled-sprites/identity-policy.json` records original/output hashes and
separate alpha, skin, iris and prop checks. The v2.0.24 authored-atlas review in
`docs/previews/v2.0.24/authored-tuxedo-*` uses identical 128px before/after
layouts, including fallback actions. It is explicitly **not Windows capture**.

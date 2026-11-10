# Supplied-video actions

The three source MP4s are retained byte-for-byte. Runtime poses come from their decoded pictures. No pose interpolation, whole-sprite stretching or translucent image mixing is used.

| Action | Source frame IDs (zero-based) | Runtime sequence | Source registration | Stride at 110px |
|---|---|---|---|---|
| Walk | 50–69; 2–29; 108–120 | 28-frame actual entrance, 20-frame distance-driven loop, 13-frame actual stop | (335,551), referenceHeight420 | 68px |
| Run | 57–69 | 13-frame distance-driven loop | (335,551), referenceHeight420 | 96px |
| Meal | 8–94 | 87 original frames once, including head lift/lick; no resetting crumbs | (455,535), referenceHeight420; horizontally mirrored | none |

All three videos are710×592/24FPS. At their native cadence, Walk is81.6logical px/s and Run177.230769logical px/s. Runtime phase follows actual ground-relative Box2D travel. The initial front turn is stationary for500ms, followed by the original first steps; stopping and reversal finish the real side-to-front footage before a new entrance. Meals use only the stationary front turn. Physical motion, support, contact and window interaction remain owned by the existing hosts.

`packing.json` is the editable registration: frame IDs, fixed crop, common root, reference height, timed entrance/exit and loop membership. Every action has one fixed crop/scale; no per-frame bounding-box fitting is performed. Source RGB and true alpha are retained in the source RGBA folders. Tuxedo RGBA uses exactly the same geometry and alpha, with material fields from the retained authored keys. Meal bowl, kibble and crumbs keep original RGB.

Repack with `python3 tools/pack_walk_video.py`, then run `python3 tools/import_care_sprites.py`. Import the three matching Tuxedo atlases with `tools/import_styled_sprites.py`. Source videos, registrations, output atlases and authoring scripts are bound in `provenance.json`; source-frame records and appearance proofs describe individual inputs. Original care assets and archived flow-derived assets remain under their existing directories but do not replace these three runtime registrations.

Matting and Tuxedo field transport are offline tools under `tools/matte_walk_video.py` and `tools/video_tuxedo/`. Their authoring dependencies are OpenCV, NumPy, Pillow, SciPy and ONNXRuntime. The Windows app uses only the packed PNG resources. The161 source-video poses are separate from appearance-key paintings. No new motion poses are counted for material edits.

The downloadable walking-native.mp4 and motions-native.mp4 are60FPS Windows GDI+/Box2D offscreen captures through the same PrimaryDisplayedPose path as the product. Offline authoring previews are24FPS and are not desktop recordings.

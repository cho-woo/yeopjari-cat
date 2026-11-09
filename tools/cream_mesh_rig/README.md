# Cream side-walk editable source

This is an offline layered 2D mesh authoring pipeline. It does not restore or
execute the old runtime rig. The approved application Walk remains unchanged
unless the separate review asset is explicitly selected.

`author.py` creates a Blender source with packed original artwork and separately
painted hidden limb/belly material. Original face, visible outer torso, tail and
near-side painted toes keep the original source UV pixels. The otherwise occluded
far-side toes use their newly painted independent parts. These are rig-rendered poses,
not independently painted intermediate drawings.

## Create, inspect and render

```sh
blender -b --python tools/cream_mesh_rig/author.py -- --mode rest --output build/cream-mesh-rig
blender build/cream-mesh-rig/authoring.blend
blender -b --python tools/cream_mesh_rig/author.py -- --mode short --output build/cream-mesh-rig
blender -b --python tools/cream_mesh_rig/author.py -- --mode bake --output build/cream-mesh-rig
python tools/cream_mesh_rig/pack.py build/cream-mesh-rig
python tools/calibrate_walk_contacts.py build/cream-mesh-rig/annotations.json --output build/cream-mesh-rig/calibrated-manifest.json --report build/cream-mesh-rig/calibration-report.json
python tools/cream_mesh_rig/assemble_review.py build/cream-mesh-rig
python tools/import_mesh_walk_review.py --manifest build/cream-mesh-rig/review-manifest.json --annotations build/cream-mesh-rig/annotations.json
```

The source camera and canvas stay fixed at 313×313. Source anchor is (161,275),
reference height is221 source pixels for110 logical pixels. BlenderX points
right, Z points up, and Y controls layer depth: more negativeY is in front.
Near-side limbs are in front of the torso; far-side limbs are behind it.

Frame0 is a separate neutral side stand. Frames1..N are the periodic walk;
frameN+1 duplicates its exact endpoint. Foot controls `FOOT_near_front`,
`FOOT_far_front`, `FOOT_near_rear`, `FOOT_far_rear` locate the **sole**, not the
ankle. The `planted` custom property is keyframed. Their child `ANKLE_*` offsets
keep the painted toe rigid while upper/lower bones use two-link IK with stretch
disabled. Edit the control curves and their contact properties together.

`*.continuous_fur` meshes have connected longitudinal topology, multi-joint
weights and preserve-volume skinning. The UV map `PaintedArtworkUV` references
the packed image; `Coverage` vertex alpha feathers only hidden root/ankle
overlaps. Edit these meshes, UVs, weights, textures or bones normally in Blender.
Do not independently rescale every part by its alpha bounding box.

## Re-bake an existing edited source without rebuilding

Save the edited source as, for example, `build/cream-mesh-rig/artist-edited.blend`.

```sh
blender -b --python tools/cream_mesh_rig/author.py -- --existing-blend build/cream-mesh-rig/artist-edited.blend --mode bake --output build/cream-mesh-edited
python tools/cream_mesh_rig/pack.py build/cream-mesh-edited
python tools/calibrate_walk_contacts.py build/cream-mesh-edited/annotations.json --output build/cream-mesh-edited/calibrated-manifest.json --report build/cream-mesh-edited/calibration-report.json
python tools/cream_mesh_rig/assemble_review.py build/cream-mesh-edited
python tools/import_mesh_walk_review.py --manifest build/cream-mesh-edited/review-manifest.json --annotations build/cream-mesh-edited/annotations.json
```

This branch opens that exact file and evaluates existing meshes, UVs, paint,
weights, constraints, control curves and root transforms. It never reconstructs
the source, re-applies procedural poses or overwrites the edited `.blend`.
Contacts/landmarks are exported from evaluated bones and sole controls.
The rig's `motion_contract_json` stores the intended low-order periodic body/head
motion. Update that contract explicitly for intentional motion changes; an
accidental edited jitter must not be explained away by fitting a new contract.

`motion.py` specifies the original procedural curves and timings. Reach is
scanned from the actual fixed link lengths, then a conservative stride is chosen.
The pose count is derived from stride/max1logical pixel gap and60 source poses
per second at22logical pixels/second, distinct from video output FPS.

## Validation and review status

`rig-evaluation.json` reports evaluated bone lengths, sole-vs-control error,
cross-section width and triangle-area ratios. `bake-report.json` also checks
actual rendered alpha near each planted sole. `annotations.json` retains only
N periodic poses in its calibration entry and appends the neutral drawing in
`auxiliary_annotations`. `manifest.json` registersN+1 images but excludes the
neutral image from the loop. No numerical check grants visual approval.
`assemble_review.py` verifies that the packed, calibrated and annotated loop
geometry and actual PNG hashes match, then copies the calibrated distance
weights/stride exactly and retains the separate neutral standing frame. It has
no uniform-timing fallback and never adds the neutral frame to the loop. Run
the import only after calibration and assembly succeed.

Inspect the same-size22logical px/s comparison and separate enlarged slow
contact video before using a candidate. A failed visual candidate remains a
review source; it must not replace the approved8 drawings or be called a
completed natural walk.

# Experimental cream kitten

Procedurally authored with Blender 4.3.2 in this repository. This asset is used
only by the separately built 3D preview, not by the default 2D release.

Regenerate from the repository root:

```bash
blender --background --factory-startup -t 4 --python-exit-code 1 \
  --python tools/create_3d_cat_preview.py -- --render
```

The editable `.blend`, interchange `.glb`, native `cat3d.bin`, report and review
renders are written to `build/3d-preview-model/`. The checked-in binary is a
reviewed export; do not replace it without inspecting its motions and running
the model loader, motion audit and native Windows renderer checks.

`model-report.json` records bone names, object vertex ranges and the authored
clips. These names/ranges are audit metadata; the runtime file is decoded using
[`tools/3d_model_export_spec.md`](../../../tools/3d_model_export_spec.md).

The model has a rounded face, vertex-painted cream/ginger coat, small geometric
fur tufts, four articulated legs, ears, eye assemblies and tail. Walk/run/stalk
use planted paw targets and two-bone inverse kinematics during authoring.
Root travel per cycle is .48/.72/.36 model units. The standing paw's relative
travel is that distance multiplied by the stance fraction; app movement supplies
the rest. Airborne animation contains no ballistic root trajectory.

This is a candidate model, not a completed conversion of every animal and
behavior. The preview keeps existing 2D paths for other characters and motions.

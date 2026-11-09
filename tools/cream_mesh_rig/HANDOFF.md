# Unapproved mesh walk: specific next work

The current source is editable and its sole controls lock geometrically, but
the short clip failed visual review. It must not replace the approved8 drawings.
The full82-pose bake is diagnostic/handoff material, not a completed natural walk.

Actual visible defects at110logical pixels include the near-rear ankle/thigh
flap, pasted chest/hip boundaries, a cutoff at the tail root and intermittent
black underbelly gaps. Keeping the original face did preserve recognizability;
that does not compensate for limb-surface defects.

The connected mesh uses fixed-link IK and rigid toes, so bone lengths and
cross-section widths remain stable. The current dual-quaternion weighting still
compresses and stretches the **longitudinal** skin between those rings severely.
Broadening the blending region did not resolve it. Do not treat width≈1 or
accurate foot targets as approval. `skin-distortion-audit.json` identifies the
exact Blender object, frame, polygon, vertex IDs, source-art UV pixels and weights
for the worst collapse/stretch in each limb, including a selection command.

The final diagnostic renderer enables material transparency overlap. Disabling
it discarded a valid far-front toe behind a transparent near-leg envelope:
isolating the toe gave alpha251 near its sole, while the composite had alpha0.
Enabling overlap made the composite match the isolated toe; this renderer fix
does not repair the underlying inverted skin polygons. The final tail mask also
excludes neighboring original torso pixels that a rectangular tail UV plate
mistakenly included. Earlier three short videos precede these final renderer
and tail-mask corrections and remain labelled rejected candidates.

The next artist should work on this same `.blend` or provide equivalent layered
313×313 source canvases in `.ora`/`.psd` plus transparent sRGB RGBA PNG plates.
Maintain the fixed camera, original face/coat silhouette and the (161,275) root
anchor. Requested paint/topology work is specific:

- Near-rear hip and knee: a stout forward-knee haunch with a continuous hidden
  inner thigh, plus corrected bending topology/UVs. Avoid the current narrow
  longitudinal flap when that foot is in late swing.
- Near-front shoulder and elbow: blend white chest fur into the independent
  upper limb during forward contact, midstance and rearward push-off; correct
  inner-elbow UV compression rather than hiding it behind the torso.
- Near-side ankles: paint complete toe-to-lower-leg transitions around the
  preserved visible toe pixels. The old partly occluded crop includes material
  from another connected source region and can expose a flat cut when lifted.
- Far-side knees/ankles: the new complete far-side toes solve the transparent
  original sole/cropped cap, but the skin between knee and rigid toe still needs
  corrective topology/UVs over stance→swing.
- Tail base and lower belly: one overlapping fur bridge behind the original
  outer coat, with actual alpha contours, not a coarse rectangular mask edge.

Supply a neutral side stance and at least forward contact, midstance, push-off,
lifted/return and pre-contact joint correctives for each visible limb. These are
correctives/material plates for the editable rig, not another full-sheet
regeneration request. Let the final pose count follow the corrected stride and
maximal actual pose gap; do not invent new drawings by duplicating renders.

There is still no approved walk→stand recovery, stand→walk weight transfer,
turnaround or side-stand↔sit connection. The separate neutral standing image is
only an endpoint reference. It does not complete those transitions.

Re-bake manually edited source with `--existing-blend` as documented in README.
That path preserves manual meshes, UVs, weights, paint, controls and keyframes;
its integrity report hashes the editing data before and after evaluation.

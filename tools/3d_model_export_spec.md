# Experimental 3D asset format, version 1

`src/cat_3d_model.hpp` is the authoritative decoder and portable sampler. This is an opt-in rendering asset, independent of the existing pet save format. Bytes are explicitly little-endian, with no padding or implicit C++ struct serialization.

## Header

| Field | Encoding |
| --- | --- |
| Magic | Exactly 8 bytes, ASCII `YPC3DM01` |
| Version | uint32, exactly 1 |
| Vertex count | uint32, 3–200000 |
| Triangle index count | uint32, 3–1200000, multiple of 3 |
| Bone count | uint32, 1–128 |
| Clip count | uint32, 1–64 |

The complete file must be at most 128 MiB. Trailing bytes are rejected.

## Vertices and triangle indices

Write all vertices, each exactly 76 bytes:

1. Bind position: 3 float32.
2. Bind normal: 3 float32, nonzero, preferably normalized.
3. UV: 2 float32.
4. Linear RGBA: 4 float32 in [0, 1]. Alpha represents coverage.
5. Roughness: float32 in [0, 1].
6. Bone indices: 4 uint16, all in range even for zero-weight slots.
7. Skin weights: 4 float32 in [0, 1], sum 1 within .001.

Then write the specified number of uint32 triangle indices. Each refers to a vertex. Split vertices at material, normal or UV seams as necessary.

The model coordinate system matches Blender: x lateral, y pointing backward (the animal faces negative y), z upward. Apply mesh/armature object transforms during export so vertices and bones use the same model coordinate system. Keep the armature modifier out of the bind mesh export; runtime skinning supplies that deformation.

## Skeleton

Each bone occupies 108 bytes:

1. Parent: int32. Bone zero has parent -1. Every later bone has parent in [0, current index).
2. Inverse bind matrix: 16 float32, row-major storage, multiplying column vectors. Translation is at offsets 3, 7 and 11. The last row is [0, 0, 0, 1].
3. Rest local transform: 10 float32, ordered translation xyz, quaternion xyzw, scale xyz.

Use the inverse of Blender's `bone.matrix_local` for the inverse bind matrix when the armature object transform is identity. For a child's rest local transform, decompose `parent.matrix_local.inverted() @ bone.matrix_local`; the root uses its full `matrix_local`.

Parents must precede children. The decoder checks that the composed global rest transform multiplied by the inverse bind matrix is approximately identity, within .005 per matrix entry. Quaternions must be normalized within .005 squared length and scales must be positive, between .0001 and 100. The format does not support shear or negative-scale reflection.

## Animation clips

For each clip, write:

1. Name byte length: uint32, 1–63.
2. UTF-8 name bytes, without a terminator or alignment padding. Names are unique and contain no controls, NUL, slash or backslash.
3. Frame count: uint32, 2–4096.
4. Frame rate: float32, 1–240.
5. Loop flag: uint32, 0 or 1.
6. `frame count × bone count` local transforms, each 10 float32 in the same TRS ordering as above. Frames are the outer loop, bones the inner loop.

Animated local transforms include the bone's rest placement. Export the evaluated `pose_bone.matrix` relative to its evaluated parent, then decompose. Exporting `matrix_basis` alone is incorrect.

Clip duration is `(frameCount - 1) / fps`. A looping clip repeats its initial pose at the last frame; endpoint translation and scale must match within .002 and the absolute quaternion dot product must exceed .9999. Quaternion signs may differ. Nonlooping clips hold the last frame. Negative playback times clamp to zero. Nonfinite times and blend values are rejected.

The planned application clip names are `idle`, `walk`, `run`, `stalk`, `belly`, `airborne`, `landing`, `ear`, `paw`. The loader accepts other valid names, and the caller resolves them using `Model::FindClip`.

Keep airborne animation in place: Box2D supplies the actual flight path. Root translation within a clip can correct body posture and support contact, but must not duplicate the desktop world's displacement. Grounding corrections for reclining should be baked into the local root transform consistently across transitions.

## Runtime API

`Model::Load` accepts byte pointer/size or `std::vector<uint8_t>`. `LoadFile` accepts a `std::filesystem::path`, including native Windows wide paths. A failed load preserves the previously valid model. Both return a boolean and error string.

`Sample(clip, seconds, output, optionalError)` interpolates local translation and scale linearly and rotation by shortest-path quaternion slerp, then composes the hierarchy and skins the bind vertices. `SampleBlended(from, fromSeconds, to, toSeconds, blend, output, optionalError)` blends the local poses before skinning, avoiding the shrinking caused by blending already-deformed positions.

CPU-skinned `RenderVertex` contains position3, normal3, uv2, rgba4 and roughness, all float32: 52 bytes. Normals use the inverse transpose of each vertex's weighted skin matrix. Degenerate transforms produce an explicit sampling failure rather than NaN output.

`Vertices`, `Indices`, `Bones`, `Clips`, `Bounds`, `FindClip` and `Duration` expose read-only model data. `Bounds` refers to the bind vertices. `BoundsOf(sampledVertices)` computes a posed bound.

## Audit the exported preview

The preview generator's `model-report.json` must accompany `cat3d.bin`. It records the actual parent-first `bone_names` and partitioned object vertex ranges (`first_vertex`, `vertex_count`, `structural_contact`). The runtime binary itself does not depend on this sidecar; it is used for repeatable asset review.

```bash
python3 tools/audit_3d_model.py \
  --model-dir build/3d-preview-model \
  --output build/3d-preview-runtime-audit.json
```

The standard-library Python wrapper compiles `tools/audit_3d_model.cpp` with `CXX` or `g++`; `--auditor PATH` can select an already-built executable. It derives joint indices and decorative exclusions from the sidecar instead of guessing bone order. The C++ program loads and skins the real binary and checks every exported frame and intervening half-frame, plus transition grids.

Exit 0 means the numerical gates passed: finite geometry; normal-length error below .0001; head scale error below .001; structural grounded contact within ±.015 model units; stance sole drift at most .002 model units per half-frame; and both head and torso support at settled belly phases .63 and .73 within [-.015, .04] model units. Airborne structural foot clearance must be within ±.015 model units near both clip ends (phases 0–.051 and .949–1), matching the host's velocity-driven pose clamp and preventing a downward visual snap on physical landing. Exit 2 retains a report with failed numerical gates. Exit 1 indicates malformed data, missing inputs or a tool error. The first walk/run/stalk stride contracts are .48/.72/.36 model units per cycle, with stance duties .68/.55/.78.

The contact check excludes explicitly marked short fur and whiskers. Bone-group support snapshots distinguish head, torso, feet and tail; a global low point alone is insufficient to establish a natural reclining pose. Transition grids report raw contact offsets before the app's grounding correction. Those raw blend offsets are not themselves a release gate because the host re-establishes the support plane after blending.

The report includes SHA-256 values for the exact model and metadata; a change during auditing fails the wrapper. These numerical gates do not certify facial appeal, anatomical intersections, desktop performance or Windows rendering. Native execution and visual review remain required.

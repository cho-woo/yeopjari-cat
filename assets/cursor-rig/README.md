# Connected pointer and grooming artwork

The product retains `assets/cat.png` frame4's original head, tail, outer hips
and hind soles. The selected character's base4 texels supply both forelimbs.
A continuous curved, two-link skin connects each shoulder, elbow and paw. The
links have bounded lengths; the cat never floats upward to reach a high cursor.
Box2D owns the actual pointer hop and ground contact.

Two purpose-painted transparent component boards are included:

- `source-parts.png`: separate headless body and two bent arms/palms. Only the
  registered palm surfaces are used, with their own small peach pads. The first
  torso candidate was rejected because it still contained front legs.
- `source-torso.png`: corrected headless, armless chest/hips with rear anatomy.
  The runtime uses its concealed chest fur behind lifted arms, preserving the
  original outer hips, tail and actual supporting hind soles.

These are **two component artworks, not generated whole-cat animation frames**.
No full-image morph or opacity transition creates the poses. Forelimb surface
UVs reveal the painted palm when the wrist turns. All geometry uses the same
registered source4 coordinate system. A curved fur mask joins the unchanged
head to the chest; blink texture comes from the selected coat's original frame5.

`rig-project.json` registers component UVs and reference height. Rebuild the
canonical three-cell texture atlas with:

```sh
python3 tools/author_cursor_parts.py
```

The 987×366 atlas holds three 329×366 canonical UV cells: left arm, right arm,
concealed torso. Runtime resources130/131/132 select Cream, soft Tuxedo and
OddEye material variants with identical alpha. Their PNGs are read once during
startup; only original source pixels remain resident. Each actor uses the
bounded display-sized source4 cache and persistent rig/output buffers.

Edit the source artwork or the JSON joint paths, rebake, regenerate coat
variants, and inspect real110px output:

```sh
python3 tools/render_cursor_reach_review.py --video
```

That script calls the product's C++ `AttackPose`/`cursor_rig::Render`. It labels
its MP4 as a **portable diagnostic**, distinct from the native Windows GDI+
output recorded by the application fixtures. Its contact marks come directly
from the rig geometry. Numeric checks do not replace review of wrists, fur,
contact, color or action transitions in the Windows output.

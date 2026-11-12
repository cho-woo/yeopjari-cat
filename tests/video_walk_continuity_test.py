"""Reject a visible pose discontinuity at the actual registered walk seam."""
import json
from pathlib import Path
import unittest

try:
    import numpy as np
    from PIL import Image
except ImportError:
    np = Image = None

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(np is not None and Image is not None, "Pixel dependencies unavailable")
class WalkContinuityTest(unittest.TestCase):
    def test_loop_boundary_is_no_larger_than_an_ordinary_step(self):
        directory = ROOT / "assets/video-actions"
        manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))

        def delta(a, b):
            visible = (a[:, :, 3] > 16) | (b[:, :, 3] > 16)
            return float(np.abs(a - b)[visible].mean())

        for clip in ("walk", "run"):
            walk = next(entry for entry in manifest["clips"] if entry["clip"] == clip)
            frames = [step["frame"] for step in walk["sequence"]["loop"]]
            for coat in ("cream", "tuxedo"):
                with self.subTest(clip=clip, coat=coat), Image.open(directory / f"{coat}-{clip}.png") as atlas:
                    def drawing(index):
                        frame = walk["frames"][index]
                        x, y, width, height = frame["rect"]
                        canvas = Image.new("RGBA", (320, 320))
                        canvas.paste(atlas.crop((x, y, x + width, y + height)),
                                     (round(160 - frame["anchor"][0]), round(240 - frame["anchor"][1])))
                        return np.asarray(canvas, dtype=np.float32)

                    first = previous = drawing(frames[0])
                    adjacent = []
                    for index in frames[1:]:
                        current = drawing(index)
                        adjacent.append(delta(previous, current))
                        previous = current
                    # v2.0.29 failed this in both coats (2.11 and 1.56 times
                    # ordinary frame change). A loop must not add a large pose jump.
                    self.assertLessEqual(delta(previous, first), float(np.mean(adjacent)) * 1.2)


if __name__ == "__main__":
    unittest.main()

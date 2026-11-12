"""Reject a global speed-up or missing recovery in retained source programs."""
import json
from pathlib import Path
import unittest

DIRECTORY = Path(__file__).resolve().parents[1] / "assets/video-actions"


class SourceClockTest(unittest.TestCase):
    def test_unretimed_actions_follow_the_original_video_clock(self):
        manifest = json.loads((DIRECTORY / "manifest.json").read_text(encoding="utf-8"))
        registration = json.loads((DIRECTORY / "registration.json").read_text(encoding="utf-8"))
        # Jump follows real ballistic takeoff/landing. Walk retains its separately
        # reviewed entrance/exit edit and a contact-distance driven loop.
        for entry in manifest["clips"]:
            if entry["clip"] in ("jump", "walk"):
                continue
            sequence = entry["sequence"]
            steps = sum((sequence[part] for part in ("intro", "loop", "outro")), [])
            for coat in ("cream", "tuxedo"):
                with self.subTest(clip=entry["clip"], coat=coat):
                    proof = registration[coat + "-" + entry["clip"]]
                    indices = proof["selected_frame_indices"]
                    self.assertEqual(proof["source_fps"], 24)
                    self.assertEqual(indices, list(range(indices[0], indices[-1] + 1)))
                    elapsed = 0
                    for ordinal, step in enumerate(steps):
                        self.assertEqual(step["frame"], ordinal)
                        # Older unretimed clips round their total duration
                        # before distributing boundaries: at most 1ms error.
                        self.assertLessEqual(abs(elapsed - ordinal * 1000 / proof["source_fps"]), 1)
                        elapsed += step["ms"]
                    self.assertLessEqual(abs(elapsed - len(indices) * 1000 / proof["source_fps"]), .51)


if __name__ == "__main__":
    unittest.main()

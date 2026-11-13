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
            if entry["clip"] == "jump":
                continue
            sequence = entry["sequence"]
            steps = sum((sequence[part] for part in ("intro", "loop", "outro")), [])
            for coat in ("cream", "tuxedo"):
                with self.subTest(clip=entry["clip"], coat=coat):
                    proof = registration[coat + "-" + entry["clip"]]
                    if entry["clip"] == "sleep":
                        self.assertEqual(proof["source_fps"], 24)
                        self.assertEqual(len(sequence["loop"]), 1)
                        self.assertEqual(sequence["loop"][0]["frame"], sequence["intro"][-1]["frame"])
                        for part in ("intro", "outro"):
                            segment = sequence[part]
                            indices = [proof["selected_frame_indices"][s["frame"]] for s in segment]
                            files = [proof["selected_source_files"][s["frame"]] for s in segment]
                            self.assertEqual(len(set(files)), 1)
                            self.assertEqual(indices, list(range(107)))
                            clock = 0
                            for ordinal, step in enumerate(segment):
                                self.assertLessEqual(abs(clock - ordinal * 1000 / 24), 1)
                                clock += step["ms"]
                            self.assertLessEqual(abs(clock - 107 * 1000 / 24), .51)
                        self.assertNotEqual(proof["selected_source_files"][0], proof["selected_source_files"][-1])
                        continue
                    if entry["clip"] in ("walk", "run"):
                        # Each boundary is its own complete consecutive source
                        # segment. The loop remains distance driven in the app.
                        for part in ("intro", "loop", "outro"):
                            part_steps = sequence[part]
                            self.assertTrue(part_steps)
                            indices = [proof["selected_frame_indices"][s["frame"]] for s in part_steps]
                            files = [proof["selected_source_files"][s["frame"]] for s in part_steps]
                            self.assertEqual(len(set(files)), 1)
                            self.assertEqual(indices, list(range(indices[0], indices[-1]+1)))
                            clock = 0
                            for ordinal, step in enumerate(part_steps):
                                self.assertLessEqual(abs(clock-ordinal*1000/24), 1)
                                clock += step["ms"]
                            self.assertLessEqual(abs(clock-len(part_steps)*1000/24), .51)
                        continue
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

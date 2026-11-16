"""Visible iris pigments must survive an upright scene's canonical return."""
from pathlib import Path
import hashlib
import json
import math
import unittest

try:
    import cv2
    import numpy as np
    from PIL import Image
except ImportError:
    cv2 = np = Image = None

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'assets/furniture-v39'


def pigments(image, rect):
    x, y, w, h = rect
    hsv = np.asarray(image.crop((x, y, x+w, y+h)).convert('RGB').convert('HSV'))
    visible = (hsv[:, :, 1] > 40) & (hsv[:, :, 2] > 35)
    return (int(np.count_nonzero(visible & (hsv[:, :, 0] >= 18) & (hsv[:, :, 0] <= 38))),
            int(np.count_nonzero(visible & (hsv[:, :, 0] >= 132) & (hsv[:, :, 0] <= 155))))


class FurnitureIrisIdentityTest(unittest.TestCase):
    @unittest.skipUnless(cv2 is not None, 'Authoring pixel dependencies unavailable')
    def test_packed_runtime_movie_preserves_endpoint_pigments_and_clock(self):
        manifest = json.loads((DIRECTORY / 'manifest.json').read_text())
        for asset in manifest['assets']:
            if asset['character'] != 10:
                continue
            with self.subTest(kind=asset['kind']):
                records = json.loads((DIRECTORY / f"selected/review/snowdrop-{asset['kind']}-eyes.json").read_text())
                eyes = sorted(records[0], key=lambda eye: eye['rect'][0])
                capture = cv2.VideoCapture(str(ROOT / asset['path']))
                self.assertEqual(round(capture.get(cv2.CAP_PROP_FRAME_COUNT)), asset['source_frames'])
                self.assertAlmostEqual(capture.get(cv2.CAP_PROP_FPS), asset['source_fps'], places=3)
                for index in (0, asset['source_frames']-1):
                    capture.set(cv2.CAP_PROP_POS_FRAMES, index)
                    ok, packed = capture.read()
                    self.assertTrue(ok)
                    image = Image.fromarray(cv2.cvtColor(packed[:asset['height']], cv2.COLOR_BGR2RGB))
                    def runtime_rect(eye):
                        x, y, w, h = eye['rect']
                        sx = asset['width']/asset['source_dimensions'][0]
                        sy = asset['height']/asset['source_dimensions'][1]
                        # Metadata records the dark pupil, whereas the painted
                        # iris ellipse extends three source pixels around it.
                        left, top = math.floor((x-3)*sx), math.floor((y-3)*sy)
                        right, bottom = math.ceil((x+w+3)*sx), math.ceil((y+h+3)*sy)
                        return left, top, right-left, bottom-top
                    gl, bl = pigments(image, runtime_rect(eyes[0]))
                    gr, br = pigments(image, runtime_rect(eyes[1]))
                    self.assertGreater(gl, bl, (asset['kind'], index, 'encoded left', gl, bl))
                    self.assertGreater(br, gr, (asset['kind'], index, 'encoded right', gr, br))
                capture.release()

    @unittest.skipUnless(Image is not None and np is not None, 'Authoring pixel dependencies unavailable')
    def test_canonical_first_and_last_have_the_same_visible_eye_identity(self):
        review = DIRECTORY / 'selected/review'
        for kind, last in [('tower', 191), ('scratch', 157), ('box', 191)]:
            with self.subTest(kind=kind):
                records = json.loads((review / f'snowdrop-{kind}-eyes.json').read_text())
                first = sorted(records[0], key=lambda eye: eye['rect'][0])
                self.assertEqual(len(first), 2)
                # Both endpoint source images have the same upright head pose.
                # Check actual painted RGB, independently of the metadata label.
                for index in (0, last):
                    image = Image.open(review / f'snowdrop-{kind}-{index:03}.png')
                    gold_left, blue_left = pigments(image, first[0]['rect'])
                    gold_right, blue_right = pigments(image, first[1]['rect'])
                    self.assertGreater(gold_left, blue_left, (kind, index, 'left', gold_left, blue_left))
                    self.assertGreater(blue_right, gold_right, (kind, index, 'right', gold_right, blue_right))

    def test_upright_pairs_and_source_timing_are_retained(self):
        manifest = json.loads((DIRECTORY / 'manifest.json').read_text())
        for asset in manifest['assets']:
            self.assertEqual(asset['selected_frames'], list(range(asset['source_frames'])))
            self.assertEqual(hashlib.sha256((ROOT / asset['source']).read_bytes()).hexdigest(), asset['source_sha256'])
            if asset['character'] != 10:
                continue
            records = json.loads((DIRECTORY / f"selected/review/snowdrop-{asset['kind']}-eyes.json").read_text())
            self.assertEqual(len(records), asset['source_frames'])
            for frame, eyes in enumerate(records):
                if len(eyes) == 2:
                    ordered = sorted(eyes, key=lambda eye: eye['rect'][0])
                    self.assertEqual([eye['material'] for eye in ordered], ['gold', 'blue'], (asset['kind'], frame))


if __name__ == '__main__':
    unittest.main()

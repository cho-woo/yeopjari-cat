"""Ball extraction must remove the prop without damaging the cat's identity."""
from pathlib import Path
import sys
import unittest

try:
    import cv2
    import numpy as np
except ImportError:
    cv2 = np = None

ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(cv2 is not None, 'Authoring pixel dependencies unavailable')
class BallMaskTest(unittest.TestCase):
    def test_snow_run_distinguishes_warm_fur_from_actual_pink_skin(self):
        sys.path.insert(0,str(ROOT/'tools'))
        from pack_video_actions import snow_material
        from PIL import Image
        original=np.array([[[225,190,178,255],[235,135,157,255],[15,10,5,0]]],dtype=np.uint8)
        result,proof=snow_material(Image.fromarray(original,'RGBA'),np.zeros((1,3),np.uint8),strict_skin=True)
        painted=np.asarray(result)
        self.assertLessEqual(int(painted[0,0,:3].max())-int(painted[0,0,:3].min()),2)
        np.testing.assert_array_equal(painted[0,1],original[0,1])
        np.testing.assert_array_equal(painted[:,:,3],original[:,:,3])
        self.assertTrue(proof['alpha_exact'] and proof['skin_rgb_exact'])

    def test_head_alpha_is_untouched_when_removing_the_ball(self):
        sys.path.insert(0, str(ROOT / 'tools'))
        from pack_video_actions import foreground
        for coat in ('cream', 'tuxedo'):
            cap = cv2.VideoCapture(str(ROOT / f'assets/video-actions/sources/{coat}-ball.mp4'))
            for index in (36, 45, 67, 90):
                with self.subTest(coat=coat, frame=index):
                    cap.set(cv2.CAP_PROP_POS_FRAMES, index)
                    ok, frame = cap.read()
                    self.assertTrue(ok)
                    rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
                    original = np.asarray(foreground(rgb))
                    cleaned = np.asarray(foreground(rgb, remove_ball=True))
                    # The red ball remains below the head. This catches an
                    # overbroad red mask erasing orange forehead/ear stripes.
                    yy, xx = np.where(original[:, :, 3] > 180)
                    top, bottom = yy.min(), yy.max()
                    boundary = round(top + (bottom - top) * .55)
                    np.testing.assert_array_equal(original[:boundary, :, 3], cleaned[:boundary, :, 3])
            cap.release()


if __name__ == '__main__':
    unittest.main()

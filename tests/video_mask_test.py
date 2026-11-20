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
    def test_snow_jump_inner_ears_survive_partial_blinks(self):
        import json
        from PIL import Image
        sys.path.insert(0, str(ROOT/'tools'))
        from pack_video_actions import snow_material

        library = json.loads((ROOT/'assets/video-actions/manifest.json').read_text(encoding='utf-8'))
        jump = next(c for c in library['clips'] if c['clip'] == 'jump' and c['character'] == 0)
        style = ROOT/'assets/styled-sprites'
        with Image.open(ROOT/'assets/video-actions'/jump['source']) as original, \
                Image.open(style/'odd-eye-masks/c10-r119.png') as irises, \
                Image.open(style/'c10-r119.png') as shipping:
            for index, frame in enumerate(jump['frames']):
                with self.subTest(frame=index):
                    x, y, w, h = frame['rect']
                    box = (x, y, x+w, y+h)
                    ax, ay = frame['anchor']
                    source = original.crop(box)
                    mask = np.asarray(irises.crop(box))
                    painted, proof = snow_material(source, mask, strict_skin=True, jump_anchor_x=ax)
                    np.testing.assert_array_equal(np.asarray(painted), np.asarray(shipping.crop(box)))
                    self.assertTrue(proof['alpha_exact'])
                    rgb = np.asarray(painted, dtype=np.int16)
                    yy, xx = np.indices(mask.shape)
                    above_muzzle = (yy >= ay-103) & (yy < ay-43) & (rgb[:,:,3] > 200)
                    for left, right, chroma, minimum in ((-9,10,30,90), (30,46,25,18)):
                        ear = above_muzzle & (xx >= ax+left) & (xx < ax+right)
                        self.assertGreaterEqual(np.count_nonzero(ear & (rgb[:,:,0]-rgb[:,:,1] > chroma)), minimum)

    def test_profile_eye_spacing_does_not_erase_inner_ears_or_color_the_tail(self):
        sys.path.insert(0,str(ROOT/'tools'))
        from pack_video_actions import snow_material
        from PIL import Image
        original=np.full((100,100,4),255,dtype=np.uint8)
        skin=np.array([235,165,152,255],dtype=np.uint8)
        original[15,35]=skin;original[75,4]=skin
        iris=np.zeros((100,100),np.uint8);iris[40:52,60:72]=1
        painted,proof=snow_material(Image.fromarray(original,'RGBA'),iris,strict_skin=True)
        result=np.asarray(painted)
        np.testing.assert_array_equal(result[15,35],skin)
        self.assertLessEqual(int(result[75,4,:3].max())-int(result[75,4,:3].min()),2)
        self.assertTrue(proof['alpha_exact'] and proof['skin_rgb_exact'])

    def test_snow_run_distinguishes_warm_fur_from_actual_pink_skin(self):
        sys.path.insert(0,str(ROOT/'tools'))
        from pack_video_actions import snow_material
        from PIL import Image
        original=np.array([[[225,190,178,255],[235,135,157,255],[15,10,5,0],[235,165,152,255]]],dtype=np.uint8)
        result,proof=snow_material(Image.fromarray(original,'RGBA'),np.zeros((1,4),np.uint8),strict_skin=True)
        painted=np.asarray(result)
        self.assertLessEqual(int(painted[0,0,:3].max())-int(painted[0,0,:3].min()),2)
        np.testing.assert_array_equal(painted[0,1],original[0,1])
        np.testing.assert_array_equal(painted[0,3],original[0,3])
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

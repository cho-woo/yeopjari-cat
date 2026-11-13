"""Keep every measured gallop drawing while avoiding stationary seam timing."""
from pathlib import Path
import json
import math
import unittest

D=Path(__file__).resolve().parents[1]/'assets/video-actions'
class RunResponseCalibrationTest(unittest.TestCase):
    def test_complete_source_boundaries_and_positive_distance_clock(self):
        identity=json.loads((D/'identity-v29.json').read_text())['clips']['run']
        manifest=json.loads((D/'manifest.json').read_text())
        run=next(e for e in manifest['clips'] if e['clip']=='run')
        self.assertEqual(identity['loop_frames'],106)
        self.assertEqual(identity['stride_pixels'],523)
        for coat in ('cream','tuxedo'):
            knots=identity['distance_knots'][coat]
            self.assertEqual(len(knots),107)
            self.assertEqual((knots[0],knots[-1]),(0,1))
            self.assertTrue(all(math.isfinite(x) for x in knots))
            self.assertTrue(all(a<b for a,b in zip(knots,knots[1:])))
            steps=[b-a for a,b in zip(knots,knots[1:])]
            ordinary=steps[8:98]
            self.assertLess(max(ordinary)-min(ordinary),1e-12)
            self.assertLess(min(steps),min(ordinary)*.5)
            self.assertGreaterEqual(min(steps),max(ordinary)*.099)
            self.assertEqual(identity['source_indices'][coat][:106],list(range(106)))
            for part in ('intro','outro'):
                section=run['sequence'][part]
                self.assertEqual(len(section),12)
                self.assertEqual(sum(s['ms'] for s in section),500)
                indices=[identity['source_indices'][coat][s['frame']] for s in section]
                self.assertEqual(indices,list(range(indices[0],indices[0]+12)))
                self.assertTrue(all('run-v36' in identity['source_files'][coat][s['frame']] for s in section))
        self.assertTrue((D/'selected/run-v36/generations.json').exists())
        self.assertTrue((D/'history/v36-before-run/identity-v29.json').exists())
if __name__=='__main__':unittest.main()

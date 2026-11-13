"""Props must never change the support coordinate of a stationary animal."""
import json
from pathlib import Path
import unittest

ROOT=Path(__file__).resolve().parents[1]
D=ROOT/'assets/video-actions'


class ContactFloorTest(unittest.TestCase):
    def test_separate_prop_never_moves_recorded_cat_floor(self):
        identity=json.loads((D/'identity-v29.json').read_text())['clips']
        registration=json.loads((D/'registration.json').read_text())
        prior=json.loads((D/'history/v37-before-continuity/registration.json').read_text())
        for clip in ('toy','eat','fish'):
            for coat in ('cream','tuxedo'):
                with self.subTest(clip=clip,coat=coat):
                    config=identity[clip]
                    current=registration[coat+'-'+clip]
                    old=prior[coat+'-'+clip]
                    self.assertEqual(config['ground_root_mode'],'fixed')
                    floor=config['fixed_ground_root'][coat]
                    self.assertEqual(floor,old['source_roots'][0][1])
                    self.assertEqual({point[1] for point in current['source_roots']},{floor})
                    self.assertEqual(current['selected_frame_indices'],old['selected_frame_indices'])
                    self.assertEqual(current['source_files'],old['source_files'])
                    self.assertEqual(current['scale'],old['scale'])
                    self.assertEqual(current['reference_height'],old['reference_height'])
                    self.assertEqual(len(current['source_roots']),158)


if __name__=='__main__':unittest.main()

"""The physical contact point must be real fur in the retained single-stroke video."""
import hashlib
import json
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from import_care_sprites import png_alpha
D=ROOT/'assets/video-actions'

class BallVideoRegistrationTest(unittest.TestCase):
    def test_complete_movie_fixed_root_and_visible_paw(self):
        identity=json.loads((D/'identity-v29.json').read_text())
        config=identity['clips']['ball']
        manifest=json.loads((D/'manifest.json').read_text())
        atlas=next(e for e in manifest['clips'] if e['clip']=='ball')
        registration=json.loads((D/'registration.json').read_text())
        self.assertEqual(config['frame_count'],97)
        self.assertEqual(sum(s['ms'] for s in atlas['sequence']['intro']),4042)
        self.assertEqual(config['ground_root_mode'],'fixed')
        for master in identity['master_sources']:
            self.assertEqual(hashlib.sha256((ROOT/master['path']).read_bytes()).hexdigest(),master['sha256'])
        for coat in ('cream','tuxedo'):
            entry=registration[coat+'-ball']
            self.assertEqual(entry['selected_frame_indices'],list(range(97)))
            self.assertEqual(set(map(tuple,entry['source_roots'])),{(480,config['fixed_ground_root'][coat])})
            self.assertEqual(entry['reference_height'],96)
            self.assertAlmostEqual(entry['scale'],96/(367*960/1024))
            self.assertEqual(len(entry['paw_points_logical']),97)
            source=D/entry['source']
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(),entry['sha256'])
            width,height,alpha=png_alpha((D/(coat+'-ball.png')).read_bytes())
            for f,(frame,paws) in enumerate(zip(atlas['frames'],entry['paw_points_logical'])):
                x,y,_,_=frame['rect'];ax,ay=frame['anchor'];px,py=paws[1]
                ix=round(x+ax+px*96/110);iy=round(y+ay-py*96/110)
                self.assertTrue(0<=ix<width and 0<=iy<height)
                self.assertGreater(alpha[iy*width+ix],180,(coat,f,ix,iy))
        generations=json.loads((D/'selected/ball-v38/generations.json').read_text())
        self.assertEqual(len(generations),2)
        for job in generations:
            options=job['args']['options']
            self.assertEqual(options['start_frame'],options['last_frame'])
            self.assertFalse(options['generate_audio'])

if __name__=='__main__':unittest.main()

import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
D = ROOT / 'assets/video-actions/selected/ball-v40'


class BallPounceRegistration(unittest.TestCase):
    def test_complete_two_coat_sources_and_three_registered_materials(self):
        registration = json.loads((D / 'registration.json').read_text())
        self.assertEqual(registration['canonical_reference_height'], 367)
        self.assertEqual(registration['fixed_source_root'], [240, 340])
        self.assertTrue(registration['full_source_cadence'])
        self.assertEqual({a['character'] for a in registration['assets']}, {0, 8, 10})
        for asset in registration['assets']:
            for name, digest in [('path', 'sha256'), ('source', 'source_sha256')]:
                self.assertEqual(hashlib.sha256((ROOT / asset[name]).read_bytes()).hexdigest(), asset[digest])
            self.assertEqual(asset['selected_frames'], list(range(124)))
            self.assertEqual(asset['source_fps'], 24)
            self.assertEqual(asset['duration_ms'], round(124 * 1000 / 24))
            self.assertEqual(asset['reference_height'], 367 * 288 / 1024)
        # Existing installed updaters retain a128MiB executable limit.
        self.assertLess(sum((ROOT / a['path']).stat().st_size for a in registration['assets']), 1_200_000)

    def test_generation_has_immutable_seated_start_and_end(self):
        jobs = json.loads((D / 'generations.json').read_text())
        identity = json.loads((ROOT / 'assets/video-actions/identity-v29.json').read_text())
        refs = {r['path']: r for r in identity['references']}
        self.assertEqual(len(jobs), 2)
        for job in jobs:
            options = job['args']['options']
            ref = refs['references/' + job['character'] + '-front-v29.png']
            self.assertEqual(options['start_frame'], ref['cdn_url'])
            self.assertEqual(options['last_frame'], ref['cdn_url'])
            self.assertEqual(job['generation']['status'], 'succeeded')
            self.assertEqual(hashlib.sha256((ROOT / 'assets/video-actions' / ref['path']).read_bytes()).hexdigest(), ref['sha256'])


if __name__ == '__main__':
    unittest.main()

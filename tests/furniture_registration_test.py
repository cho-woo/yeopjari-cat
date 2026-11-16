import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'assets/furniture-v39'


class FurnitureRegistration(unittest.TestCase):
    def test_complete_sources_and_fixed_registration(self):
        manifest = json.loads((ASSETS / 'manifest.json').read_text(encoding='utf-8'))
        assets = manifest['assets']
        self.assertEqual(len(assets), 9)
        self.assertEqual({(a['character'], a['kind_id']) for a in assets},
                         {(c, k) for c in (0, 8, 10) for k in (1, 2, 3)})
        for a in assets:
            for name, digest in (('source', 'source_sha256'), ('path', 'sha256')):
                self.assertEqual(hashlib.sha256((ROOT / a[name]).read_bytes()).hexdigest(), a[digest])
            self.assertEqual(a['selected_frames'], list(range(a['source_frames'])))
            self.assertEqual(a['source_frames'], 158 if a['kind'] == 'scratch' else 192)
            self.assertEqual(a['source_fps'], 24)
            self.assertEqual(a['anchor'], [84.375, 247.5])
            self.assertEqual(a['reference_height'], 367 * 288 / 1024)
            self.assertEqual(a['duration_ms'], round(a['source_frames'] * 1000 / 24))
        # Keep v2.0.38's 128 MiB updater compatible without a bootstrap update.
        self.assertLess(sum((ROOT / a['path']).stat().st_size for a in assets), 3_300_000)

    def test_immutable_references_and_identical_end_conditions(self):
        refs = json.loads((ASSETS / 'selected/reference-registration.json').read_text(encoding='utf-8'))
        jobs = json.loads((ASSETS / 'selected/video-generations.json').read_text(encoding='utf-8'))
        self.assertEqual(len(refs), 6)
        self.assertEqual(len(jobs), 6)
        for ref in refs:
            self.assertFalse(ref['master_design_changed'])
            self.assertEqual(ref['cat_root'], [300, 880])
            self.assertEqual(ref['common_reference_height'], 367)
            self.assertEqual(hashlib.sha256((ROOT / ref['canonical_source']).read_bytes()).hexdigest(),
                             ref['canonical_source_sha256'])
            self.assertEqual(hashlib.sha256((ROOT / ref['path']).read_bytes()).hexdigest(), ref['sha256'])
        for job in jobs:
            options = job['args']['options']
            self.assertEqual(options['start_frame'], options['last_frame'])


if __name__ == '__main__':
    unittest.main()

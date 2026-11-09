"""Registration/auxiliary-pose fixtures only; these do not create or approve art."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
spec = importlib.util.spec_from_file_location('mesh_walk_import', ROOT/'tools/import_mesh_walk_review.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class MeshWalkImportTests(unittest.TestCase):
    def fixture(self, folder, auxiliary=False):
        manifest = json.loads((ROOT/'assets/care-sprites/manifest.json').read_text())
        entry = copy.deepcopy(next(e for e in manifest['clips'] if e['clip']=='walk'))
        entry['source'] = str(ROOT/'assets/care-sprites/c0-walk.png')
        points = [{'frame': i, 'feet': {name: {'x': 100., 'y': 275., 'planted': True}
                                      for name in module.FEET},
                   'landmarks': {'head': [260., 135.], 'body': [150., 205.]}} for i in range(8)]
        # Scalar registration fixture, deliberately not a valid walking contact
        # cycle. The calibrator owns biomechanical acceptance independently.
        annotations = {'annotations': points, 'motion_contract': {'sample_phases': [i/8 for i in range(8)]}}
        if auxiliary:
            entry['frames'].append(copy.deepcopy(entry['frames'][0]))
            entry['frame_count'] = 9
            entry['standing_frame'] = 8
            standing = copy.deepcopy(points[0]); standing['frame'] = 8
            annotations['auxiliary_annotations'] = [standing]
        (folder/'manifest.json').write_text(json.dumps({'schema': 2, 'clips': [entry]}))
        (folder/'contacts.json').write_text(json.dumps(annotations))
        return entry, annotations

    def test_original_png_bytes_and_approved_manifest_are_preserved(self):
        png = (ROOT/'assets/care-sprites/c0-walk.png').read_bytes()
        before = hashlib.sha256((ROOT/'assets/care-sprites/manifest.json').read_bytes()).hexdigest()
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); self.fixture(folder)
            outputs, entry = module.registration(folder/'manifest.json', folder/'contacts.json')
            self.assertEqual(outputs[ROOT/'assets/review/cream-mesh-walk.png'], png)
            header = outputs[ROOT/'src/cream_mesh_walk.generated.hpp'].decode()
            self.assertIn('kResource=480', header)
            self.assertEqual(entry['resource'], 400)  # Same logical Walk registration.
            self.assertNotIn(ROOT/'assets/care-sprites/manifest.json', outputs)
        self.assertEqual(hashlib.sha256((ROOT/'assets/care-sprites/manifest.json').read_bytes()).hexdigest(), before)

    def test_neutral_standing_pose_has_contacts_but_is_not_looped(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); self.fixture(folder, auxiliary=True)
            outputs, _ = module.registration(folder/'manifest.json', folder/'contacts.json')
            canonical = json.loads(outputs[ROOT/'assets/review/cream-mesh-walk.manifest.json'])['clips'][0]
            self.assertEqual(canonical['frame_count'], 9)
            self.assertEqual(canonical['standing_frame'], 8)
            self.assertEqual({s['frame'] for s in canonical['sequence']['loop']}, set(range(8)))
            self.assertIn('std::array<Sample,9>', outputs[ROOT/'src/cream_mesh_walk.generated.hpp'].decode())

    def test_auxiliary_pose_cannot_silently_enter_loop(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); entry, _ = self.fixture(folder, auxiliary=True)
            entry['sequence']['loop'].append({'frame': 8, 'ms': 75})
            (folder/'manifest.json').write_text(json.dumps({'schema': 2, 'clips': [entry]}))
            with self.assertRaisesRegex(ValueError, 'Auxiliary'):
                module.registration(folder/'manifest.json', folder/'contacts.json')

    def test_standing_hold_cannot_freeze_a_raised_paw(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); _, points = self.fixture(folder, auxiliary=True)
            points['auxiliary_annotations'][0]['feet']['near_front']['planted'] = False
            (folder/'contacts.json').write_text(json.dumps(points))
            with self.assertRaisesRegex(ValueError, 'neutral pose'):
                module.registration(folder/'manifest.json', folder/'contacts.json')

    def test_bad_phase_or_missing_named_contact_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); _, points = self.fixture(folder)
            points['motion_contract']['sample_phases'][3] = points['motion_contract']['sample_phases'][2]
            (folder/'contacts.json').write_text(json.dumps(points))
            with self.assertRaisesRegex(ValueError, 'phases'):
                module.registration(folder/'manifest.json', folder/'contacts.json')
            _, points = self.fixture(folder)
            del points['annotations'][2]['feet']['far_rear']
            (folder/'contacts.json').write_text(json.dumps(points))
            with self.assertRaisesRegex(ValueError, 'Contact IDs'):
                module.registration(folder/'manifest.json', folder/'contacts.json')

    def test_canonical_review_roundtrip_and_check_rejects_stale_header(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); entry, contacts_document = self.fixture(folder, auxiliary=True)
            # The calibrator's source entry remains the8-loop-only geometry,
            # while the review manifest also registers a neutral ninth pose.
            contacts_document['entry'] = copy.deepcopy(entry)
            contacts_document['entry']['frames'] = entry['frames'][:8]
            contacts_document['entry']['frame_count'] = 8
            contacts_document['entry']['standing_frame'] = 0
            (folder/'contacts.json').write_text(json.dumps(contacts_document))
            generated_root = folder/'generated-root'
            with mock.patch.object(module, 'ROOT', generated_root):
                outputs, _ = module.registration(folder/'manifest.json', folder/'contacts.json')
                for path, data in outputs.items():
                    path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(data)
                canonical = generated_root/'assets/review/cream-mesh-walk.manifest.json'
                contacts = generated_root/'assets/review/cream-mesh-walk.contacts.json'
                canonical_contacts = json.loads(contacts.read_text())
                self.assertEqual(canonical_contacts['entry']['source'], 'cream-mesh-walk.png')
                self.assertEqual(canonical_contacts['entry']['frame_count'], 8)
                self.assertEqual(len(canonical_contacts['annotations']), 8)
                self.assertEqual(len(canonical_contacts['auxiliary_annotations']), 1)
                self.assertEqual(canonical_contacts['annotations'], contacts_document['annotations'])
                self.assertEqual(json.loads((folder/'contacts.json').read_text())['entry']['source'], entry['source'])
                argv = ['import_mesh_walk_review.py', '--manifest', str(canonical),
                        '--annotations', str(contacts), '--check']
                with mock.patch.object(sys, 'argv', argv), mock.patch('builtins.print'):
                    self.assertEqual(module.main(), 0)
                    header = generated_root/'src/cream_mesh_walk.generated.hpp'
                    header.write_bytes(header.read_bytes()+b'// stale\n')
                    self.assertEqual(module.main(), 1)


if __name__ == '__main__':
    unittest.main()

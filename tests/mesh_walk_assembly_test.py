"""Manifest/provenance fixtures only; no artwork or calibrated walk is created."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('mesh_assembly', ROOT/'tools/cream_mesh_rig/assemble_review.py')
assembly = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(assembly)


class ReviewAssemblyTests(unittest.TestCase):
    def fixture(self, folder, count=3):
        # Reuse unchanged approved PNG bytes for a scalar registration fixture.
        # This does not represent new poses or assert kinematic acceptance.
        png = (ROOT/'assets/care-sprites/c0-walk.png').read_bytes()
        (folder/'atlas.png').write_bytes(png)
        frames = [{'rect': [i*10, 0, 10, 10], 'anchor': [5, 9]} for i in range(count+1)]
        entry = {'clip': 'walk', 'character': 0, 'source': 'atlas.png', 'columns': count+1, 'rows': 1,
                 'reference_height': 9, 'frame_count': count, 'frames': frames[:count]}
        calibrated = copy.deepcopy(entry)
        calibrated.update(sha256=hashlib.sha256(png).hexdigest(), stride_pixels=7.25,
                          sequence={'intro': [], 'loop': [{'frame': i, 'ms': 10+i*3} for i in range(count)],
                                    'outro': [], 'duration_ms': 0, 'finish': 'complete', 'events': []})
        packed = copy.deepcopy(entry); packed.update(frame_count=count+1, frames=frames, standing_frame=count)
        packed['sequence'] = {'loop': [{'frame': i, 'ms': 1} for i in range(count)]}
        annotations = {'entry': entry, 'annotations': [{'frame': i} for i in range(count)],
                       'auxiliary_annotations': [{'frame': count, 'root_anchor': [5, 9],
                                                 'feet': {name: {'x': 5., 'y': 9., 'planted': True}
                                                          for name in assembly.FEET}}]}
        for name, document in [('manifest', {'schema': 2, 'clips': [packed]}),
                               ('calibrated-manifest', {'schema': 2, 'clips': [calibrated]}),
                               ('annotations', annotations)]:
            (folder/f'{name}.json').write_text(json.dumps(document))
        return calibrated, packed, annotations

    def test_dynamic_pose_count_preserves_calibration_and_immutable_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); calibrated, packed, _ = self.fixture(folder, count=5)
            before = {p.name: p.read_bytes() for p in folder.iterdir()}
            report = assembly.assemble(folder)
            result = json.loads((folder/'review-manifest.json').read_text())['clips'][0]
            self.assertEqual(result['sequence'], calibrated['sequence'])
            self.assertEqual(result['stride_pixels'], 7.25)
            self.assertEqual(result['frames'], packed['frames'])
            self.assertEqual((result['frame_count'], result['standing_frame']), (6, 5))
            self.assertFalse(report['production_approved'])
            self.assertEqual(before, {name: (folder/name).read_bytes() for name in before})

    def test_missing_neutral_rejects_without_overwriting_existing_review(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); self.fixture(folder)
            target = folder/'review-manifest.json'; target.write_text('previous review\n')
            document = json.loads((folder/'annotations.json').read_text()); document['auxiliary_annotations'] = []
            (folder/'annotations.json').write_text(json.dumps(document))
            with self.assertRaisesRegex(ValueError, 'separate neutral'):
                assembly.assemble(folder)
            self.assertEqual(target.read_text(), 'previous review\n')

    def test_changed_png_rejects_stale_calibration(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory); self.fixture(folder)
            with (folder/'atlas.png').open('ab') as handle:
                handle.write(b'changed source bytes')
            with self.assertRaisesRegex(ValueError, 'hash'):
                assembly.assemble(folder)
            self.assertFalse((folder/'review-manifest.json').exists())


if __name__ == '__main__':
    unittest.main()

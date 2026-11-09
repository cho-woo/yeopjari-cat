"""Physical scalar contact fixtures, not production artwork or new poses."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('walk_contacts', ROOT/'tools/calibrate_walk_contacts.py')
calibrator = importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(calibrator)


def contact_fixture(distances=None):
    """Four staggered stance windows span all edges, including the wrap edge."""
    distances = distances or [0.5]*8
    count = len(distances)
    assert count == 8
    cumulative = [0]
    for distance in distances:
        cumulative.append(cumulative[-1]+distance)
    stride = sum(distances)
    frames = [{'rect': [i*100, 0, 100, 110], 'anchor': [50, 100]} for i in range(count)]
    annotations = []
    for i in range(count):
        feet = {}
        for foot, start in zip(calibrator.FEET, (0, 2, 4, 6)):
            age = (i-start) % count
            traveled = (cumulative[i]-cumulative[start]) % stride
            feet[foot] = {'x': (70 if 'front' in foot else 30)-traveled,
                          'y': 100 if age <= 4 else 90, 'planted': age <= 4}
        annotations.append({'frame': i, 'feet': feet, 'landmarks': {'head': [72, 38], 'body': [50, 65]}})
    return {'schema': 1, 'entry': {'clip': 'walk', 'character': 0, 'source': 'missing-fixture.png',
                                  'columns': 8, 'rows': 1, 'reference_height': 110,
                                  'frame_count': count, 'frames': frames}, 'annotations': annotations}


class ContactCalibrationTests(unittest.TestCase):
    def test_consistent_closure_derives_stride_from_all_edges(self):
        result = calibrator.calibrate(contact_fixture())
        self.assertTrue(result['accepted'], result['errors'])
        self.assertAlmostEqual(result['entry']['stride_pixels'], 4)
        intervals = result['report']['intervals']
        self.assertEqual((intervals[-1]['from'], intervals[-1]['to']), (7, 0))
        self.assertAlmostEqual(intervals[-1]['root_travel_logical_px'], 0.5)
        self.assertTrue(all(len(interval['estimates']) >= 2 for interval in intervals))
        self.assertFalse(result['report']['foot_identities_inferred'])
        self.assertIn('geometry-only', result['report']['source_image_status'])

    def test_nonuniform_contacts_replace_any_old_timing(self):
        distances = [0.2, 0.4, 0.6, 0.8, 0.3, 0.7, 0.5, 0.9]
        document = contact_fixture(distances)
        document['entry'].update(stride_pixels=64, sequence={'loop': [{'frame': 0, 'ms': 999}]})
        result = calibrator.calibrate(document)
        self.assertTrue(result['accepted'], result['errors'])
        self.assertAlmostEqual(result['entry']['stride_pixels'], sum(distances))
        weights = [step['ms'] for step in result['entry']['sequence']['loop']]
        self.assertEqual(sum(weights), 60000)
        self.assertGreater(len(set(weights)), 1)
        self.assertTrue(result['report']['existing_sequence_or_stride_ignored'])
        for measured, expected in zip(result['report']['intervals'], distances):
            self.assertAlmostEqual(measured['root_travel_logical_px'], expected)
        self.assertLess(result['report']['max_weight_quantization_residual_logical_px'], 0.0001)

    def test_simultaneous_planted_feet_must_agree(self):
        document = contact_fixture()
        document['annotations'][1]['feet']['near_front']['x'] -= 0.3
        result = calibrator.calibrate(document)
        self.assertFalse(result['accepted'])
        self.assertTrue(any('feet disagree' in error for error in result['errors']))
        self.assertNotIn('entry', result)

    def test_forward_and_zero_planted_motion_are_rejected(self):
        for shift in (0.5, 1.0):
            document = contact_fixture()
            document['annotations'][1]['feet']['near_front']['x'] += shift
            result = calibrator.calibrate(document)
            self.assertFalse(result['accepted'])
            self.assertTrue(any('zero/forward planted motion' in error for error in result['errors']))

    def test_missing_named_track_and_unannotated_sheet_reject(self):
        document = contact_fixture()
        del document['annotations'][2]['feet']['far_rear']
        result = calibrator.calibrate(document)
        self.assertFalse(result['accepted'])
        self.assertIn('four named foot tracks', ' '.join(result['errors']))
        del document['annotations']
        self.assertFalse(calibrator.calibrate(document)['accepted'])

    def test_wrap_is_checked_not_omitted_from_stride(self):
        document = contact_fixture()
        document['annotations'][0]['feet']['near_rear']['x'] += 2
        result = calibrator.calibrate(document)
        self.assertFalse(result['accepted'])
        self.assertTrue(any('Interval 7->0' in error for error in result['errors']))

    def test_uncovered_transition_never_gets_interpolated_weights(self):
        document = contact_fixture()
        for foot in calibrator.FEET:
            document['annotations'][3]['feet'][foot]['planted'] = False
        result = calibrator.calibrate(document)
        self.assertFalse(result['accepted'])
        self.assertIn([2, 3], result['report']['uncovered_intervals'])
        self.assertIsNone(result['report']['measured_stride_logical_px'])
        self.assertNotIn('entry', result)

    def test_gap_limit_requires_more_actual_drawings(self):
        result = calibrator.calibrate(contact_fixture([1.25]*8))
        self.assertFalse(result['accepted'])
        self.assertAlmostEqual(result['report']['max_root_gap_logical_px'], 1.25)
        self.assertTrue(any('requires more inbetweens' in error for error in result['errors']))

    def test_floor_and_landmark_consistency(self):
        document = contact_fixture()
        document['annotations'][1]['feet']['near_front']['y'] -= 1
        self.assertIn('common floor', ' '.join(calibrator.calibrate(document)['errors']))
        document = contact_fixture()
        document['annotations'][4]['landmarks']['head'][0] += 1
        self.assertIn('head landmark jitter', ' '.join(calibrator.calibrate(document)['errors']))

    def test_source_local_anchors_are_used_as_common_root(self):
        original = contact_fixture()
        shifted = copy.deepcopy(original)
        for index in range(8):
            shift = index/10
            shifted['entry']['frames'][index]['anchor'][0] += shift
            for foot in shifted['annotations'][index]['feet'].values():
                foot['x'] += shift
            for point in shifted['annotations'][index]['landmarks'].values():
                point[0] += shift
        first, second = calibrator.calibrate(original), calibrator.calibrate(shifted)
        self.assertTrue(second['accepted'], second['errors'])
        self.assertAlmostEqual(first['entry']['stride_pixels'], second['entry']['stride_pixels'])

    def test_invalid_numbers_and_incomplete_registration_reject(self):
        document = contact_fixture(); document['annotations'][0]['feet']['near_front']['x'] = float('nan')
        self.assertFalse(calibrator.calibrate(document)['accepted'])
        document = contact_fixture(); del document['entry']['frames'][1]['anchor']
        self.assertFalse(calibrator.calibrate(document)['accepted'])
        for key, value in (('character', True), ('standing_frame', 8)):
            document = contact_fixture(); document['entry'][key] = value
            self.assertFalse(calibrator.calibrate(document)['accepted'])
        document = contact_fixture(); document['schema'] = True
        self.assertFalse(calibrator.calibrate(document)['accepted'])

    def test_failed_cli_does_not_replace_existing_export(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document = contact_fixture(); del document['annotations']
            source = root/'input.json'; source.write_text(json.dumps(document))
            output = root/'export.json'; output.write_text('existing reviewed registration\n')
            report = root/'report.json'
            process = subprocess.run([sys.executable, str(ROOT/'tools/calibrate_walk_contacts.py'), str(source),
                                      '--output', str(output), '--report', str(report)], capture_output=True, text=True)
            self.assertEqual(process.returncode, 1)
            self.assertEqual(output.read_text(), 'existing reviewed registration\n')
            self.assertFalse(json.loads(report.read_text())['accepted'])

    def test_present_png_requires_actual_alpha_at_annotated_contacts(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('Pillow alpha checks are optional review-tool dependencies')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            document = contact_fixture()
            image = Image.new('RGBA', (800, 110), (0, 0, 0, 0))
            image.save(root/'missing-fixture.png')
            rejected = calibrator.calibrate(document, root)
            self.assertFalse(rejected['accepted'])
            self.assertIn('no source alpha support', ' '.join(rejected['errors']))
            for annotation in document['annotations']:
                for foot in annotation['feet'].values():
                    if foot['planted']:
                        image.putpixel((annotation['frame']*100+round(foot['x']), round(foot['y'])-1), (250, 240, 220, 255))
            image.save(root/'missing-fixture.png')
            accepted = calibrator.calibrate(document, root)
            self.assertTrue(accepted['accepted'], accepted['errors'])
            self.assertEqual(len(accepted['entry']['sha256']), 64)


if __name__ == '__main__':
    unittest.main()

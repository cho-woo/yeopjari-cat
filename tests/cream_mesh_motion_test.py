"""Kinematic invariants of the editable rig curves; no Blender or artwork approval."""
import importlib.util
import math
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('cream_mesh_motion', ROOT/'tools/cream_mesh_rig/motion.py')
motion = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(motion)


class CreamMeshMotionTests(unittest.TestCase):
    def assertPointClose(self, first, second, tolerance=1e-8):
        self.assertLessEqual(math.dist(first, second), tolerance, (first, second))

    def world_sole(self, name, phase, stride=None):
        stride = motion.STRIDE_LOGICAL if stride is None else stride
        pose = motion.foot_pose(name, phase, stride=stride)
        return (phase*stride+(pose['position'][0]-motion.ANCHOR[0])*motion.SCALE,
                (pose['position'][1]-motion.ANCHOR[1])*motion.SCALE)

    def test_body_advances_while_each_stance_sole_stays_at_one_world_point(self):
        for name in motion.FOOT_ORDER:
            for cycle in (-1, 0, 1, 3):
                begin = cycle+motion.OFFSETS[name]
                samples = [begin+motion.DUTY*(i+.25)/64 for i in range(64)]
                soles = [self.world_sole(name, phase) for phase in samples]
                self.assertGreater((samples[-1]-samples[0])*motion.STRIDE_LOGICAL, 1)
                self.assertTrue(all(motion.foot_pose(name, phase)['planted'] for phase in samples))
                for sole in soles:
                    self.assertPointClose(sole, soles[0])
                    self.assertAlmostEqual(sole[1], 0)

    def test_successive_touchdowns_advance_by_the_chosen_stride(self):
        for name in motion.FOOT_ORDER:
            first = self.world_sole(name, motion.OFFSETS[name]+.01)
            next_cycle = self.world_sole(name, motion.OFFSETS[name]+1.01)
            self.assertAlmostEqual(next_cycle[0]-first[0], motion.STRIDE_LOGICAL)
            self.assertAlmostEqual(next_cycle[1], 0)

    def test_swing_leaves_floor_and_reaches_the_next_contact(self):
        for name in motion.FOOT_ORDER:
            start = motion.OFFSETS[name]+motion.DUTY
            duration = 1-motion.DUTY
            samples = [motion.foot_pose(name, start+duration*i/64) for i in range(1, 64)]
            self.assertTrue(all(not pose['planted'] for pose in samples))
            self.assertTrue(all(pose['position'][1] < motion.ANCHOR[1] for pose in samples))
            middle = motion.foot_pose(name, start+duration*.5)
            lift = (motion.ANCHOR[1]-middle['position'][1])*motion.SCALE
            self.assertAlmostEqual(lift, motion.LIFT_LOGICAL)
            before = self.world_sole(name, motion.OFFSETS[name]+1-1e-8)
            after = self.world_sole(name, motion.OFFSETS[name]+1+1e-8)
            self.assertPointClose(before, after, 1e-6)

    def test_liftoff_and_touchdown_have_continuous_position_and_velocity(self):
        epsilon = 1e-7
        for name in motion.FOOT_ORDER:
            for age in (motion.DUTY, 1):
                boundary = motion.OFFSETS[name]+age
                first = motion.foot_pose(name, boundary-epsilon)
                last = motion.foot_pose(name, boundary+epsilon)
                self.assertPointClose(first['position'], last['position'], 1e-4)
                self.assertPointClose(first['velocity'], last['velocity'], 1e-3)
                self.assertAlmostEqual(first['velocity'][0]*motion.SCALE, -motion.STRIDE_LOGICAL, places=5)
                self.assertAlmostEqual(last['velocity'][0]*motion.SCALE, -motion.STRIDE_LOGICAL, places=5)

    def test_reported_curve_velocities_agree_with_measured_position_derivatives(self):
        epsilon = 1e-6
        for name in motion.FOOT_ORDER:
            for phase in (.013, .177, .391, .719, .943):
                first = motion.foot_pose(name, phase-epsilon)['position']
                last = motion.foot_pose(name, phase+epsilon)['position']
                measured = [(last[i]-first[i])/(2*epsilon) for i in (0, 1)]
                self.assertPointClose(measured, motion.foot_pose(name, phase)['velocity'], 1e-4)
        for position, velocity in ((motion.head_shift, motion.head_velocity),
                                   (motion.body_shift, motion.body_velocity)):
            for phase in (.017, .3, .877):
                measured = [(position(phase+epsilon)[i]-position(phase-epsilon)[i])/(2*epsilon)
                            for i in (0, 1)]
                self.assertPointClose(measured, velocity(phase), 1e-6)

    def test_loop_position_and_velocity_close_for_all_parts(self):
        for name in motion.FOOT_ORDER:
            first, last = motion.foot_pose(name, 0), motion.foot_pose(name, 1)
            self.assertPointClose(first['position'], last['position'])
            self.assertPointClose(first['velocity'], last['velocity'])
            self.assertEqual(first['planted'], last['planted'])
            self.assertAlmostEqual(first['angle'], last['angle'])
        for value in (motion.head_shift, motion.head_velocity, motion.body_shift, motion.body_velocity):
            self.assertPointClose(value(0), value(1))

    def test_four_identified_feet_have_distinct_staggered_events_and_ground_support(self):
        self.assertEqual(set(motion.FOOT_ORDER), {'near_front', 'far_front', 'near_rear', 'far_rear'})
        self.assertEqual(set(motion.OFFSETS), set(motion.FOOT_ORDER))
        self.assertEqual(len(set(motion.OFFSETS.values())), 4)
        signatures = {name: tuple(motion.foot_pose(name, i/128)['planted'] for i in range(128))
                      for name in motion.FOOT_ORDER}
        self.assertEqual(len(set(signatures.values())), 4)
        for index in range(2048):
            phase = index/2048
            support = [motion.foot_pose(name, phase) for name in motion.FOOT_ORDER]
            self.assertGreaterEqual(sum(pose['planted'] for pose in support), 2)
            self.assertTrue(all(abs(pose['position'][1]-motion.ANCHOR[1]) < 1e-8
                                for pose in support if pose['planted']))

    def test_runtime_stride_changes_reach_controls_annotations_and_contract(self):
        chosen = motion.STRIDE_LOGICAL*.73
        with patch.object(motion, 'STRIDE_LOGICAL', chosen):
            for name in motion.FOOT_ORDER:
                phase = motion.OFFSETS[name]+.1
                pose = motion.foot_pose(name, phase)
                self.assertTrue(pose['planted'])
                self.assertAlmostEqual(pose['velocity'][0]*motion.SCALE, -chosen)
                first, last = self.world_sole(name, phase), self.world_sole(name, phase+.1)
                self.assertPointClose(first, last)
                annotation = motion.annotation(phase)['feet'][name]
                self.assertPointClose([annotation['x'], annotation['y']], pose['position'])
            contract = motion.motion_contract(128)
            self.assertEqual(contract['stride_logical_px'], chosen)
            for name in motion.FOOT_ORDER:
                if contract['loop_boundary']['start']['feet'][name]['planted']:
                    self.assertPointClose(contract['loop_boundary']['start']['feet'][name]['velocity'], [-chosen, 0])


if __name__ == '__main__':
    unittest.main()

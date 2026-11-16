import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from run_live_motion_audit import inspect_trace


class FurnitureSpaceAudit(unittest.TestCase):
    def inspect(self, second_left, instrumented=True):
        rows=[]
        for role,left in [(0,100),(1,second_left)]:
            row=dict(role=role,coat=8 if role else 0,x=left,lift=0,vx=0,vy=0,
                     wall_ms=1000,scene=1,pounce=0,clip=8,source=20,action=37,
                     repeat=0,timer_gap_ms=16)
            if instrumented:row.update(scene_left=left,scene_right=left+200)
            rows.append(row)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace.csv'
            with path.open('w',newline='') as stream:
                writer=csv.DictWriter(stream,fieldnames=rows[0]);writer.writeheader();writer.writerows(rows)
            return inspect_trace(path,0,8)

    def test_separate_movies(self):
        result=self.inspect(350)
        self.assertEqual(result['furniture_space_samples'],1)
        self.assertEqual(result['furniture_visual_overlaps'],0)

    def test_overlapping_movies_are_reported(self):
        result=self.inspect(200)
        self.assertEqual(result['furniture_visual_overlaps'],1)
        self.assertEqual(result['furniture_overlap_events'][0]['overlap_logical_pixels'],100)

    def test_legacy_trace_is_not_counted_as_spatial_measurement(self):
        self.assertEqual(self.inspect(200,False)['furniture_space_samples'],0)


if __name__=='__main__':unittest.main()

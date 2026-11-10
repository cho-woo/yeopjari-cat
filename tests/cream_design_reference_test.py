import importlib.util,json,tempfile,unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('cream_reference_check',ROOT/'tools/check_cream_reference.py')
checker=importlib.util.module_from_spec(spec);spec.loader.exec_module(checker)

class CreamDesignReferenceTest(unittest.TestCase):
    def test_original_eight_pose_design_remains_bound(self):
        result=checker.check(ROOT)
        self.assertEqual(result['frames'],8)
        self.assertEqual(result['status'],'passed')

    def test_pixel_file_change_requires_explicit_reference_update(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'assets').mkdir()
            (root/checker.MANIFEST).write_bytes((ROOT/checker.MANIFEST).read_bytes())
            raw=bytearray((ROOT/'assets/cat.png').read_bytes());raw[-20]^=1
            (root/'assets/cat.png').write_bytes(raw)
            with self.assertRaisesRegex(ValueError,'design changed'):checker.check(root)

if __name__=='__main__':unittest.main()

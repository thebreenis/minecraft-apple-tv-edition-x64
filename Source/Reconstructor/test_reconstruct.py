import hashlib
from pathlib import Path
import tempfile
import unittest
from reconstruct import apply_edits, safe_target

class ReconstructionTests(unittest.TestCase):
    def test_checked_edit_preserves_unchanged_bytes(self):
        before=b'unchanged: OLD tail';after=b'unchanged: NEW tail'
        patch=dict(path='test',before=hashlib.sha256(before).hexdigest(),after=hashlib.sha256(after).hexdigest(),edits=[[11,14,b'NEW'.hex()]])
        self.assertEqual(apply_edits(before,patch),after)
        with self.assertRaises(ValueError):apply_edits(before+b'x',patch)
        patch['after']='0'*64
        with self.assertRaises(ValueError):apply_edits(before,patch)

    def test_overlapping_edits_fail(self):
        patch=dict(path='test',before=hashlib.sha256(b'abc').hexdigest(),after='',edits=[[1,2,''],[0,1,'']])
        with self.assertRaises(ValueError):apply_edits(b'abc',patch)

    def test_archive_paths_cannot_escape(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d)
            for name in ['../escape','a/../../escape','/absolute','C:/absolute','a:stream','a\\..\\escape','a//b']:
                with self.subTest(name=name),self.assertRaises(ValueError):safe_target(root,name)
            self.assertEqual(safe_target(root,'data/ui.json'),root/'data/ui.json')

if __name__=='__main__':unittest.main()

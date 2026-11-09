#!/usr/bin/env python3
"""Repair v2.0.15 review media; the already validated EXE/ZIP/feed are immutable."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

REPO='cho-woo/yeopjari-cat'
TAG='v2.0.15'
EXE='YeopjariCat-2.0.15.exe'
EXE_SHA='7b5fd64493ab4241bb932d01c630558c0da242e30ef54b17a61b808147e6d266'
SOURCE='5740458c3a09e1cee3a627aee930861e27e2c667'

def gh(*args):return subprocess.check_output(['gh',*args,'--repo',REPO],text=True)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    assert os.environ.get('GITHUB_ACTIONS')=='true'
    assert os.environ.get('GITHUB_REPOSITORY')==REPO
    assert os.environ.get('GITHUB_REF')=='refs/heads/main'
    release=json.loads(gh('release','view',TAG,'--json','isDraft,isPrerelease,targetCommitish,assets'))
    assert not release['isDraft'] and not release['isPrerelease']
    with tempfile.TemporaryDirectory() as directory:
        folder=Path(directory);gh('release','download',TAG,'--dir',directory)
        assert sha(folder/EXE)==EXE_SHA
        provenance=json.loads((folder/'windows-validation.json').read_text())
        assert provenance['source_commit']==SOURCE and provenance['exe_sha256']==EXE_SHA
        assert (folder/'render-self-test.txt').read_text(encoding='utf-8-sig').startswith('exit=0 ')
        assert sha(folder/'two-cats-gdi.png')=='6ae1685a53891a4bd826a9ecdb9de1f36556fb5f888fee7b3ac6741a3b11b1b8'
        for line in (folder/'SHA256SUMS.txt').read_text().splitlines():
            digest,name=line.split('  ',1);assert sha(folder/name)==digest,name
        changed={'two-cats.gif','two-cats.mp4','review-SHA256SUMS.txt'}
        original={p.name:sha(p) for p in folder.iterdir() if p.name not in changed}
        assert len(original)==8
        for name in ('two-cats.gif','two-cats.mp4'):
            shutil.copyfile(Path('docs/media')/name,folder/name)
        paths=sorted(p for p in folder.iterdir() if p.name!='review-SHA256SUMS.txt')
        (folder/'review-SHA256SUMS.txt').write_text(''.join(sha(p)+'  '+p.name+'\n' for p in paths))
        gh('release','upload',TAG,'--clobber',*(str(folder/name) for name in sorted(changed)))
        expected={p.name:sha(p) for p in folder.iterdir()}
        with tempfile.TemporaryDirectory() as verified:
            gh('release','download',TAG,'--dir',verified)
            actual={p.name:sha(p) for p in Path(verified).iterdir()}
            assert actual==expected
            assert all(actual[name]==digest for name,digest in original.items())
        print('Verified preview replacement; all 8 application/native evidence assets unchanged.')

if __name__=='__main__':main()

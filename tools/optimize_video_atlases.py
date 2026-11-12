#!/usr/bin/env python3
"""Apply the packer's lossless PNG encoding to existing video atlases."""
import hashlib
import json
from pathlib import Path
from PIL import Image
from pack_video_actions import ROOT, DIRECTORY, RESOURCE, save_atlas, sha, write_json


def main():
    manifest=json.loads((DIRECTORY/'manifest.json').read_text(encoding='utf-8'))
    proof=[]
    for entry in manifest['clips']:
        resource=RESOURCE[entry['clip']]
        for coat in ('cream','tuxedo','snow'):
            path=ROOT/f'assets/styled-sprites/c10-r{resource}.png' if coat=='snow' else DIRECTORY/f'{coat}-{entry["clip"]}.png'
            original=path.read_bytes()
            with Image.open(path) as image:
                pixels=hashlib.sha256(image.tobytes()).hexdigest();save_atlas(image,path)
            with Image.open(path) as image:
                assert hashlib.sha256(image.tobytes()).hexdigest()==pixels
            if len(path.read_bytes())>len(original):path.write_bytes(original)
            proof.append(dict(path=path.relative_to(ROOT).as_posix(),pixel_sha256=pixels,
                              before_bytes=len(original),after_bytes=path.stat().st_size))
            if coat=='cream':entry['sha256']=sha(path)
            if coat=='tuxedo':(ROOT/f'assets/styled-sprites/c8-r{resource}.png').write_bytes(path.read_bytes())
    write_json(DIRECTORY/'manifest.json',manifest)
    styles=ROOT/'assets/styled-sprites/manifest.json'
    data=json.loads(styles.read_text(encoding='utf-8'))
    for entry in data['atlases']:
        if entry['base_resource_id'] in RESOURCE.values() and 'sha256' in entry:
            entry['sha256']=sha(styles.parent/entry['source'])
            if 'base_sha256' in entry:entry['base_sha256']=sha(ROOT/entry['base_source'])
    write_json(styles,data)
    policy=ROOT/'assets/styled-sprites/odd-eye-policy.json'
    data=json.loads(policy.read_text(encoding='utf-8'))
    for entry in data['atlases']:
        if entry['base_resource_id'] not in RESOURCE.values():continue
        entry['input_sha256']=sha(ROOT/entry['base_source'])
        entry['output_sha256']=sha(policy.parent/entry['source'])
    data['additional_authoring_tool_sha256']=sha(ROOT/'tools/pack_video_actions.py')
    write_json(policy,data)
    provenance=DIRECTORY/'provenance.json';data=json.loads(provenance.read_text(encoding='utf-8'))
    data['manifest_sha256']=sha(DIRECTORY/'manifest.json')
    for entry in data['files']:entry['sha256']=sha(DIRECTORY/entry['path'])
    write_json(provenance,data)
    write_json(ROOT/'build/motion-audit/lossless-encoding.json',proof)
    print('Lossless resource savings:',sum(p['before_bytes']-p['after_bytes'] for p in proof),'bytes')


if __name__=='__main__':main()

"""Losslessly encode registered video atlases; never change a decoded pixel."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
from PIL import Image

ROOT=Path(__file__).resolve().parents[1]
D=ROOT/'assets/video-actions'
S=ROOT/'assets/styled-sprites'

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
def pixels(path):
    with Image.open(path) as image:
        assert image.mode=='RGBA'
        return image.size,hashlib.sha256(image.tobytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--optimizer',type=Path,required=True)
    args=parser.parse_args();optimizer=args.optimizer.resolve();assert optimizer.is_file()
    identity=json.loads((D/'identity-v29.json').read_text())
    for master in identity['master_sources']:assert sha(ROOT/master['path'])==master['sha256']
    manifest=json.loads((D/'manifest.json').read_text())
    styles=json.loads((S/'manifest.json').read_text())
    already={'toy','eat','fish','jump','sleep','run','settle'}
    entries=[e for e in manifest['clips'] if e['clip'] not in already]
    pairs={entry['base_resource_id']:entry for entry in styles['atlases'] if entry['character']==8}
    from import_care_sprites import load_entries
    layouts={entry['clip']:entry['resource'] for entry in load_entries(D/'manifest.json')}
    records=[];saved=0
    for entry in sorted(entries,key=lambda e:(D/e['source']).stat().st_size,reverse=True):
        resource=layouts[entry['clip']]
        targets=[(D/entry['source'],True),(D/entry['source'].replace('cream-','tuxedo-'),False)]
        targets += [(S/style['source'],True) for style in styles['atlases'] if style['base_resource_id']==resource]
        for path,embedded in targets:
            path=path.resolve();assert path.is_relative_to(D) or path.is_relative_to(S)
            before=path.read_bytes();size,digest=pixels(path)
            subprocess.run([str(optimizer),'--nx','-o','3','--threads','2','--quiet',str(path)],check=True)
            if pixels(path)!=(size,digest) or path.read_bytes()[24:26]!=bytes([8,6]):
                path.write_bytes(before);raise ValueError('Optimizer changed decoded RGBA bytes: '+str(path))
            after=path.read_bytes();gain=len(before)-len(after);assert gain>=0
            records.append({'path':path.relative_to(ROOT).as_posix(),'size':size,'decoded_rgba_sha256':digest,
                'before_sha256':hashlib.sha256(before).hexdigest(),'after_sha256':hashlib.sha256(after).hexdigest(),
                'bytes_saved':gain,'embedded':embedded})
            if embedded:saved+=gain
        print(entry['clip'],'embedded bytes saved so far:',saved,flush=True)
    for entry in manifest['clips']:entry['sha256']=sha(D/entry['source'])
    for entry in styles['atlases']:
        entry['sha256']=sha(S/entry['source']);entry['base_sha256']=sha(ROOT/entry['base_source'])
    write(D/'manifest.json',manifest);write(S/'manifest.json',styles)
    policy=json.loads((S/'odd-eye-policy.json').read_text())
    for entry in policy['atlases']:
        entry['input_sha256']=sha(ROOT/entry['base_source']);entry['output_sha256']=sha(S/entry['source'])
    write(S/'odd-eye-policy.json',policy)
    write(D/'selected/continuity-v37/lossless-encoding.json',{'schema':1,'method':'oxipng --nx -o3, RGBA8 retained',
        'all_decoded_rgba_bytes_exact':True,'immutable_masters_unchanged':True,'embedded_bytes_saved':saved,'files':records})
    print('Total embedded bytes saved:',saved)

if __name__=='__main__':main()

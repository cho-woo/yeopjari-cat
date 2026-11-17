"""Losslessly repack existing Dropshot-derived poses for the Android renderer.

No new animation, retiming, pose fitting or per-coat registration is authored.
Small atlas pages keep decode memory bounded. Assets are reproducible build output.
"""
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'mobile/android/app/src/main/assets/motions'
ACTIONS = {'walk': 400, 'groom': 113, 'knead': 440, 'punch': 117, 'sleep': 114, 'settle': 439}


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    base = json.loads((ROOT / 'assets/video-actions/manifest.json').read_text())
    styles = json.loads((ROOT / 'assets/styled-sprites/manifest.json').read_text())
    identity = ROOT / 'assets/video-actions/identity-v29.json'
    entries = []
    for name, resource in ACTIONS.items():
        clip = next(c for c in base['clips'] if c['clip'] == name)
        for coat in (0, 8, 10):
            spec = clip if coat == 0 else next(c for c in styles['atlases'] if c['character'] == coat and c['base_resource_id'] == resource)
            source = ROOT / ('assets/video-actions' if coat == 0 else 'assets/styled-sprites') / spec['source']
            if sha(source) != spec['sha256']:
                raise ValueError(f'Source hash mismatch: {source}')
            records, pages = [], []
            width = max(f['rect'][2] for f in clip['frames']) + 4
            height = max(f['rect'][3] for f in clip['frames']) + 4
            with Image.open(source) as sheet:
                sheet = sheet.convert('RGBA')
                for start in range(0, len(clip['frames']), 32):
                    group = clip['frames'][start:start+32]
                    page = Image.new('RGBA', (width * 8, height * 4))
                    for offset, frame in enumerate(group):
                        x, y, w, h = frame['rect']
                        dest_x, dest_y = (offset % 8)*width+2, (offset//8)*height+2
                        pose = sheet.crop((x, y, x+w, y+h))
                        page.paste(pose, (dest_x, dest_y))
                        records.append({'page': len(pages), 'rect': [dest_x, dest_y, w, h], 'anchor': frame['anchor']})
                    filename = f'c{coat}-{name}-{len(pages)}.webp'
                    page.save(OUT / filename, lossless=True, exact=True, method=6)
                    with Image.open(OUT / filename) as check:
                        if check.convert('RGBA').tobytes() != page.tobytes():
                            raise ValueError('Lossless pixel equality failed')
                    pages.append({'file': filename, 'sha256': sha(OUT/filename), 'width': page.width, 'height': page.height})
                if name == 'settle':
                    x,y,w,h = clip['frames'][-1]['rect']
                    if coat == 0:
                        icon = Image.new('RGBA', (160,160))
                        pose = sheet.crop((x,y,x+w,y+h))
                        icon.paste(pose, ((160-w)//2,(160-h)//2))
                        icon_dir = ROOT/'mobile/android/app/src/main/res/drawable-nodpi'
                        icon_dir.mkdir(parents=True, exist_ok=True)
                        icon.save(icon_dir/'cat_icon.png')
            entries.append({'name':name,'coat':coat,'reference_height':clip['reference_height'],
                'stride_pixels':clip.get('stride_pixels',0),'sequence':clip['sequence'],
                'frames':records,'pages':pages,'source':source.relative_to(ROOT).as_posix(),'source_sha256':sha(source)})
    manifest = {'schema':1,'source_identity':identity.relative_to(ROOT).as_posix(),'source_identity_sha256':sha(identity),
        'method':'lossless_repack_existing_registered_RGBA_without_retiming','clips':entries}
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
    print(json.dumps({'clips':len(entries),'frames':sum(len(c['frames']) for c in entries),
        'pages':sum(len(c['pages']) for c in entries),'bytes':sum(p.stat().st_size for p in OUT.iterdir())}))


if __name__ == '__main__':
    main()

#!/usr/bin/env python3
"""Pair retained Dropshot frames with isolated shipping GDI+ drawing captures.

Run the executable's --render-source-audit and the normal interaction reviews
first. These pairs inspect drawing registration; the normal dispatcher and
physics reviews separately verify ownership, timing, travel and contacts.
The source row retains its green backdrop and original RGB pixels.
"""
import argparse
import json
from pathlib import Path

import cv2
from PIL import Image, ImageDraw
from pack_video_actions import foreground, head_root

ROOT = Path(__file__).resolve().parents[1]
IDS = dict(groom=0, fish=3, punch=4, eat=5, jump=6, walk=9, run=10,
           knead=11, reach=12, ball=13, toy=14, brake=15, slide=16,
           stumble=17, climb=18, fly=19, turn=20)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native', type=Path, default=ROOT / 'render-self-test/source-audit')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    directory = ROOT / 'assets/video-actions'
    registration = json.loads((directory / 'registration.json').read_text(encoding='utf-8'))
    native = json.loads((args.native / 'report.json').read_text(encoding='utf-8'))
    entries = {entry['clip']: entry for entry in json.loads((directory / 'manifest.json').read_text(encoding='utf-8'))['clips']}
    report = {'schema': 1, 'native_version': native['version'],
              'comparison': 'original RGB source at registered reference scale vs shipping GDI+ drawing',
              'normal_motion_states': native['motion_states'], 'coats': native['coats'], 'clips': []}
    # Include the original non-video sheets and Snow's actual painted material.
    names={value:key for key,value in IDS.items()};names.update({1:'sleep-original',2:'box-retired-original',7:'treat-original'})
    for clip_id,name in names.items():
        board=Image.new('RGB',(1920,660),'#f5f7f9');draw=ImageDraw.Draw(board)
        for row,character in enumerate((0,8,10)):
            for sample in range(8):
                with Image.open(args.native/f'clip-{clip_id}-c{character}-s{sample}.png') as actor:
                    board.paste(actor,(sample*240,row*220+20),actor)
                draw.text((sample*240+4,row*220+4),f'{name} / coat {character} / {sample}',fill='black')
        board.save(args.output/f'{name}-three-coats.png')
    motion_names='Idle Walk Sleep Eat Groom Stretch Watch Stalk Pounce Zoomies TailChase Punch Jump Treat EarFlick TailRaise BellyRoll Loaf Knead Peek Scratch Sneeze ToyPaw BoxSit Sniff LookDown Balance Startled Yawn SleepTalk Hang Pride'.split()
    for page in range(4):
        board=Image.new('RGB',(960,1440),'#f5f7f9');draw=ImageDraw.Draw(board)
        for row in range(8):
            motion=page*8+row
            draw.text((4,row*180+3),f'{motion_names[motion]} / samples 1 and 4 / Cream, Tuxedo, Snow',fill='black')
            for half,sample in enumerate((1,4)):
                for coat,character in enumerate((0,8,10)):
                    with Image.open(args.native/f'motion-{motion}-c{character}-s{sample}.png') as actor:
                        actor=actor.crop((40,35,200,190))
                        board.paste(actor,((half*3+coat)*160,row*180+22),actor)
        board.save(args.output/f'normal-states-{page+1}.png')
    for clip, clip_id in IDS.items():
        board = Image.new('RGB', (1920, 960), '#f5f7f9')
        draw = ImageDraw.Draw(board)
        for row, coat in enumerate(('cream', 'tuxedo')):
            proof = registration[coat + '-' + clip]
            indices = proof['selected_frame_indices']
            files = proof.get('selected_source_files', [proof['source']] * len(indices))
            caps = {}
            for sample in range(8):
                ordinal = (len(indices) - 1) * sample // 7
                filename, index = files[ordinal], indices[ordinal]
                if filename not in caps:
                    caps[filename] = cv2.VideoCapture(str(directory / filename))
                cap = caps[filename]
                cap.set(cv2.CAP_PROP_POS_FRAMES, index)
                ok, bgr = cap.read()
                if not ok:
                    raise ValueError(f'Cannot decode {filename}:{index}')
                raw = Image.fromarray(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB))
                if proof.get('source_roots'):
                    xroot, yroot = proof['source_roots'][ordinal]
                else:
                    actor = foreground(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB), remove_wall=clip == 'climb',
                                       keep_feather=clip in ('fish', 'eat', 'toy'), keep_fly=clip == 'fly')
                    xroot, yroot = proof['neutral_root'][0], actor.getbbox()[3]
                    if clip == 'climb':
                        xroot = actor.getbbox()[2] - 48 * 96 / 110 / proof['scale']
                    if clip == 'jump' and 18 <= ordinal < 39:
                        xroot = head_root(actor)[0]
                factor = 110 / (367 * raw.height / 1024)
                raw = raw.resize((round(raw.width * factor), round(raw.height * factor)), Image.Resampling.LANCZOS)
                cell = Image.new('RGB', (240, 200), '#00ff00')
                cell.paste(raw, (round(120 - xroot * factor), round(180 - yroot * factor)))
                board.paste(cell, (sample * 240, row * 480 + 30))
                app_path = args.native / f'clip-{clip_id}-c{0 if coat == "cream" else 8}-s{sample}.png'
                with Image.open(app_path) as app:
                    board.paste(app, (sample * 240, row * 480 + 240), app)
                draw.text((sample * 240 + 4, row * 480 + 8), f'{clip} / {coat} / source {index}', fill='black')
                draw.text((sample * 240 + 4, row * 480 + 440), f'app drawing {ordinal}', fill='black')
            for cap in caps.values():
                cap.release()
        board.save(args.output / f'{clip}-source-app.png')
        entry = entries[clip]
        report['clips'].append({'clip': clip, 'drawings': entry['frame_count'],
            'program_ms': sum(step['ms'] for part in ('intro', 'loop', 'outro') for step in entry['sequence'][part]),
            'comparison': f'{clip}-source-app.png', 'sources': [
                {'coat': coat, 'path': registration[coat + '-' + clip]['source'],
                 'sha256': registration[coat + '-' + clip]['sha256']} for coat in ('cream', 'tuxedo')]})
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()

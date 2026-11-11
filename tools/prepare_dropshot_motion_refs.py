#!/usr/bin/env python3
"""Export current registered artwork as reference inputs, without creating poses.

These are local authoring inputs, not unverified Dropshot API payloads. A model
ID, duration and upload schema must come from its authenticated MCP catalogue.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/dropshot/reference')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    original = json.loads((ROOT / 'assets/cream-design-reference.json').read_text())
    walk = next(c for c in json.loads((ROOT / 'assets/natural-gait/manifest.json').read_text())['clips']
                if c['clip'] == 'walk' and c['character'] == 0)
    front = original['frames'][4]
    side = walk['frames'][walk['standing_frame']]
    sources = {
        'front': (ROOT / original['source'], front, original['reference_height']),
        'side': (ROOT / 'assets/natural-gait' / walk['source'], side, walk['reference_height']),
    }
    refs = []
    for view, (path, registration, height) in sources.items():
        x, y, w, h = registration['rect']
        with Image.open(path) as source:
            image = source.convert('RGBA').crop((x, y, x+w, y+h))
        alpha = image.getchannel('A').point(lambda a: 0 if a <= 16 else a)
        image.putalpha(alpha)
        # Fixed reference height and common floor, never per-pose bbox fitting.
        scale = 400 / height
        image = image.resize((round(w*scale), round(h*scale)), Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', (1024, 768), (246, 246, 244, 255))
        anchor = registration['anchor']
        canvas.alpha_composite(image, (round(360-anchor[0]*scale), round(640-anchor[1]*scale)))
        destination = output / f'cream-{view}.png'
        canvas.convert('RGB').save(destination)
        refs.append({'view': view, 'file': destination.name, 'sha256': digest(destination),
                     'source': str(path.relative_to(ROOT)), 'source_sha256': digest(path),
                     'rect': registration['rect'], 'anchor': anchor, 'reference_height': height,
                     'floor_y': 640, 'draw_height': 400})
    identity = (
        'Use the supplied existing kitten artwork as the exact character reference. Preserve its face, '
        'large brown eyes, head/body proportions, warm cream and orange fur pattern, ear shape and tail. '
        'One kitten, exactly four legs. Fixed orthographic camera, entire body including ears, paws and tail '
        'always visible. Plain light grey background and flat floor; no cuts, zoom, camera orbit, text, props '
        'or human hands. Smooth real joint articulation with anticipation and follow-through. '
    )
    scenes = {
        'walking-jump': ('side', 'Walk towards screen right, make one small forward jump without stopping first, '
                         'hind legs push, paws tuck then forepaws reach, land with soft compression and continue walking. '
                         'The body follows one uninterrupted parabolic arc with constant forward momentum.'),
        'running-jump': ('side', 'Run towards screen right, leap forward without braking or turning, '
                        'extend then tuck the hind legs, balance with the tail, forepaws land first followed '
                        'by the hind paws, absorb the landing and continue running. No airborne pause.'),
        'wipe-mouth': ('front', 'From the same seated neutral pose, slowly lick one forepaw, gently wipe the mouth '
                      'and cheek twice, lower the paw and settle back into the exact starting pose.'),
        'both-paws': ('front', 'From the seated neutral pose, shift weight to the hind paws, rise onto tiptoes '
                     'and softly reach up with both front paws to catch an imaginary pointer. Briefly close '
                     'the paws, lower the body and return both forepaws to their original floor positions.'),
        'knead': ('front', 'Remain seated and gently knead the floor with alternating forepaws. Left presses '
                 'as right lifts, then right presses as left lifts. Natural shoulder and elbow motion, '
                 'tiny weight shifts, relaxed blinking; finish in the identical neutral pose.'),
        'paw-swat': ('front', 'From the seated neutral pose, focus on an imaginary nearby target, anticipate '
                    'with one forepaw, make one quick playful rounded swat, retract with a soft follow-through '
                    'and settle the paw back on the floor. The other forepaw and hind paws support the body.'),
    }
    plan = {'schema': 1, 'requested_model': 'Seedance 2.5', 'generation_status': 'not_generated',
            'generated_videos': 0, 'new_animation_drawings': 0, 'references': refs,
            'scenes': [{'id': name, 'reference': f'cream-{view}.png', 'requested_seconds': 6,
                        'prompt': identity+prompt} for name, (view, prompt) in scenes.items()],
            'integration': 'Use authenticated model options; retain source video and job ID. Strip authored '
                           'root travel before runtime playback: Box2D remains the only movement/height owner. '
                           'Transfer the shared motion to the existing Tuxedo and Odd-eye coat registrations.'}
    (output / 'authoring-plan.json').write_text(json.dumps(plan, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps({'output': str(output), 'reference_images': len(refs), 'scenes': len(scenes),
                      'generated_videos': 0, 'new_animation_drawings': 0}))


if __name__ == '__main__':
    main()

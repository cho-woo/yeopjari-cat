#!/usr/bin/env python3
"""Register an authored mesh bake as an OPTIONAL Win32 review, preserving PNG bytes.

This does not replace the approved care manifest. The normal application keeps
its eight Walk drawings; --mesh-walk-review selects resource480 at startup.
The usual bounded RGBA importer validates the new sheet and playlist. Contact
samples come from the rig's explicit exported annotations, never a bbox guess.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys

from import_care_sprites import generated, load_entries, ImportError as SpriteImportError

ROOT = Path(__file__).resolve().parents[1]
FEET = ('near_front', 'far_front', 'near_rear', 'far_rear')


def registration(manifest, annotations):
    entries = load_entries(manifest)
    if len(entries) != 1 or entries[0]['clip'] != 'walk' or entries[0]['character'] != 0:
        raise ValueError('Review manifest must contain exactly one Cream Walk clip')
    entry = entries[0]
    document = json.loads(annotations.read_text())
    loop_samples = document.get('annotations')
    auxiliary = document.get('auxiliary_annotations', [])
    if (not isinstance(loop_samples, list) or not loop_samples or not isinstance(auxiliary, list) or
            len(loop_samples)+len(auxiliary) != len(entry['frames'])):
        raise ValueError('Every loop or auxiliary drawing requires its rig-exported foot annotations')
    samples = loop_samples + auxiliary
    contract = document.get('motion_contract', {})
    phases = contract.get('sample_phases', [i/len(loop_samples) for i in range(len(loop_samples))])
    if (len(phases) != len(loop_samples) or any(type(p) not in (int,float) or not math.isfinite(p) or
            not 0 <= p < 1 for p in phases) or phases[0] != 0 or
            any(a >= b for a,b in zip(phases,phases[1:]))):
        raise ValueError('Rig phases must start at zero and increase within one cycle')
    loop_indices = {step['frame'] for step in entry['program']['canonical']['loop']}
    if loop_indices != set(range(len(loop_samples))) or any(sample.get('frame') in loop_indices for sample in auxiliary):
        raise ValueError('Auxiliary standing/recovery poses must never enter the calibrated loop')
    phases = phases + [0.0] * len(auxiliary)
    header, _ = generated(entries)
    header = header.decode().replace('tools/import_care_sprites.py', 'tools/import_mesh_walk_review.py')
    header = header.replace('namespace cat::sprite_assets {', 'namespace cat::mesh_walk {\nusing cat::sprite_assets::Descriptor;\nusing cat::sprite_assets::Frame;')
    header = header.replace('kCareSprites', 'kRegistrations')
    header = header.replace('assets/care-sprites/c0-walk.png', 'assets/review/cream-mesh-walk.png')
    header = header.rsplit('}', 1)[0]
    header += '\ninline constexpr const auto& kDescriptor=kRegistrations[0];\ninline constexpr int kResource=480;\n'
    header += f'inline constexpr unsigned kLoopFrameCount={len(loop_samples)};\n'
    header += 'struct Contact {double x,y;bool planted;};\nstruct Sample {double phase;std::array<Contact,4> feet;std::array<double,2> head,body;};\n'
    header += f'inline constexpr std::array<Sample,{len(samples)}> kSamples{{{{\n'
    for index, sample in enumerate(samples):
        if sample.get('frame') != index or set(sample.get('feet', {})) != set(FEET):
            raise ValueError('Contact IDs/order must match the rig export')
        contacts = []
        for foot in FEET:
            point = sample['feet'][foot]
            if type(point.get('planted')) is not bool or not all(type(point.get(axis)) in (int, float) and
                    math.isfinite(point[axis]) for axis in ('x', 'y')):
                raise ValueError('Incomplete numeric rig foot contact')
            contacts.append('Contact{' + repr(point['x']) + ',' + repr(point['y']) + ',' + str(point['planted']).lower() + '}')
        landmarks = sample['landmarks']
        if any(not isinstance(landmarks.get(name), list) or len(landmarks[name]) != 2 or
               any(type(value) not in (int,float) or not math.isfinite(value) for value in landmarks[name])
               for name in ('head', 'body')):
            raise ValueError('Rig head/body landmarks are required')
        pairs = ['{{'+','.join(map(repr, landmarks[name]))+'}}' for name in ('head', 'body')]
        header += '    Sample{' + repr(phases[index]) + ',{{' + ','.join(contacts) + '}},' + ','.join(pairs) + '},\n'
    if not all(point['planted'] for point in samples[entry['standing_frame']]['feet'].values()):
        raise ValueError('The standing hold must use a neutral pose with all four paws planted')
    header += '}};\nstatic_assert(cat::sprite_assets::Valid(kDescriptor),"Invalid mesh Walk review registration");\n}\n'
    canonical = {'schema': 2, 'clips': [{
        'clip': 'walk', 'character': 0, 'source': 'cream-mesh-walk.png',
        'columns': entry['columns'], 'rows': entry['rows'], 'frame_count': len(entry['frames']),
        'reference_height': entry['reference_height'], 'frames': entry['frames'],
        'stride_pixels': entry['stride_pixels'], 'standing_frame': entry['standing_frame'],
        'sha256': entry['sha256'], 'sequence': entry['program']['canonical'],
    }]}
    # Keep the rig's loop-only geometry entry separate from its auxiliary pose.
    # Only relocate its source path so re-running the strict calibrator on the
    # canonical contacts still reads the real copied PNG instead of silently
    # falling back to geometry-only validation after atlas.png is relocated.
    if isinstance(document.get('entry'), dict):
        document['entry']['source'] = 'cream-mesh-walk.png'
    canonical_contacts = (json.dumps(document, indent=2)+'\n').encode()
    return {
        ROOT/'src/cream_mesh_walk.generated.hpp': header.encode(),
        ROOT/'src/cream_mesh_walk.generated.rc': b'// Generated optional mesh Walk review; normal startup does not select it.\n480 RCDATA "../assets/review/cream-mesh-walk.png"\n',
        ROOT/'assets/review/cream-mesh-walk.png': entry['data'],
        ROOT/'assets/review/cream-mesh-walk.manifest.json': (json.dumps(canonical, indent=2)+'\n').encode(),
        ROOT/'assets/review/cream-mesh-walk.contacts.json': canonical_contacts,
    }, entry


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--annotations', type=Path, required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        outputs, entry = registration(args.manifest.resolve(), args.annotations.resolve())
        if args.check:
            for path, data in outputs.items():
                if not path.is_file() or path.read_bytes() != data:
                    raise ValueError(f'Generated review drift: {path.relative_to(ROOT)}')
        else:
            for path, data in outputs.items():
                path.parent.mkdir(parents=True, exist_ok=True)
                if not path.is_file() or path.read_bytes() != data:
                    path.write_bytes(data)
    except (OSError, ValueError, KeyError, SpriteImportError) as error:
        print(f'Mesh Walk review registration failed: {error}', file=sys.stderr)
        return 1
    print(json.dumps({'status': 'checked' if args.check else 'registered', 'frame_count': len(entry['frames']),
                      'stride_logical_px': entry['stride_pixels'], 'png_sha256': hashlib.sha256(entry['data']).hexdigest(),
                      'normal_approved_walk_changed': False, 'new_art_created_by_import': False}))
    return 0


if __name__ == '__main__':
    sys.exit(main())

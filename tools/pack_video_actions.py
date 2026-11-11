#!/usr/bin/env python3
"""Bake the retained Dropshot videos into registered, transparent app atlases.

Authoring requires NumPy, OpenCV and Pillow. Runtime import and verification use
the standard library. Each character/action has one scale. Only the jump's
recorded screen translation is removed because Box2D supplies world movement.
"""
from __future__ import annotations

import hashlib
import argparse
import json
from pathlib import Path

import cv2
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
DIRECTORY = ROOT / 'assets/video-actions'
CELL = 320
ROOT_POINT = (160, 240)
REFERENCE_HEIGHT = 96
RESOURCE = {'groom': 113, 'punch': 117, 'jump': 119, 'knead': 440, 'reach': 441}
RESOURCE.update({'ball':430,'toy':431,'brake':432,'slide':433,'stumble':434,'climb':435})
SOURCE_ACTION = {'groom': 'wipe', 'punch': 'punch', 'jump': 'jump', 'knead': 'knead', 'reach': 'reach'}
SOURCE_ACTION.update({name:name for name in ('ball','toy','brake','slide','stumble','climb')})
REACH_KEYS = {
    'cream': [(18, 300, 651, 369, 651), (36, 300, 651, 369, 651),
        (54, 243, 360, 378, 360), (72, 282, 228, 396, 306),
        (90, 228, 312, 360, 222), (108, 282, 249, 396, 330),
        (126, 252, 636, 348, 633), (141, 300, 651, 369, 651)],
    'tuxedo': [(18, 282, 693, 378, 693), (36, 282, 693, 378, 693),
        (54, 243, 366, 405, 303), (72, 282, 309, 402, 354),
        (90, 234, 357, 372, 309), (108, 285, 336, 408, 369),
        (126, 246, 660, 348, 678), (141, 282, 693, 378, 693)]}
BALL_KEYS = {
    'cream': [(24,326,713),(36,442,679),(48,487,575),(60,396,722),(72,461,710),(82,480,675)],
    'tuxedo': [(42,464,734),(54,498,731),(66,520,635),(78,559,676),(90,520,724),(100,520,724)]}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


def background_palette(rgb):
    border=np.concatenate((rgb[:4].reshape(-1,3),rgb[-4:].reshape(-1,3),
        rgb[:,:4].reshape(-1,3),rgb[:,-4:].reshape(-1,3))).astype(np.float32)
    red,green,blue=border.T
    known=(green>np.maximum(red,blue)*1.22+12)|(border.max(axis=1)<12)
    if known.mean()>.98:return None
    cv2.setRNGSeed(28)
    return cv2.kmeans(border[::4],12,None,(cv2.TERM_CRITERIA_EPS|cv2.TERM_CRITERIA_MAX_ITER,20,.5),1,cv2.KMEANS_PP_CENTERS)[2]


def foreground(rgb, palette=None, remove_ball=False, remove_wall=False):
    values = rgb.astype(np.float32)
    red, green, blue = values.transpose(2, 0, 1)
    backdrop = green > np.maximum(red, blue) * 1.22 + 12
    border=np.concatenate((rgb[0],rgb[-1],rgb[:,0],rgb[:,-1]))
    if (border.max(axis=1)<12).mean()>.5:
        backdrop |= values.max(axis=2)<4
    if remove_wall:
        # The reference's full-height monitor rim is scenery, never a cat part.
        rim=(rgb[:12].max(axis=2)-rgb[:12].min(axis=2)<24)&(rgb[:12].mean(axis=2)>60)
        columns=np.where(rim.mean(axis=0)>.85)[0]
        columns=columns[columns>rgb.shape[1]*.65]
        if len(columns):backdrop[:,max(0,columns.min()-3):]=True
    if palette is not None:
        nearest=np.full(rgb.shape[:2],np.inf,np.float32)
        for color in palette:nearest=np.minimum(nearest,np.sum((values-color)**2,axis=2))
        candidate=(backdrop|(nearest<22**2)).astype(np.uint8)
        _,regions=cv2.connectedComponents(candidate,8)
        border_ids=np.unique(np.concatenate((regions[0],regions[-1],regions[:,0],regions[:,-1])))
        backdrop=np.isin(regions,border_ids[border_ids!=0])
    count, labels, stats, _ = cv2.connectedComponentsWithStats((~backdrop).astype(np.uint8), 8)
    if count < 2:
        raise ValueError('No character foreground')
    mask = (labels == 1 + np.argmax(stats[1:, cv2.CC_STAT_AREA])).astype(np.uint8)
    if remove_ball:
        red_ball=(red>130)&(red>green*1.8)&(red>blue*1.8)
        count,labels,stats,_=cv2.connectedComponentsWithStats(red_ball.astype(np.uint8),8)
        for component in range(1,count):
            if stats[component,cv2.CC_STAT_AREA]>350:
                mask[cv2.dilate((labels==component).astype(np.uint8),np.ones((3,3),np.uint8))>0]=0
        count,labels,stats,_=cv2.connectedComponentsWithStats(mask,8)
        if count>1:mask=(labels==1+np.argmax(stats[1:,cv2.CC_STAT_AREA])).astype(np.uint8)
    # Preserve dark eyes, mouth and fur enclosed by the character outline.
    exterior = (1 - mask).copy()
    cv2.floodFill(exterior, None, (0, 0), 2)
    mask[exterior == 1] = 1
    alpha = cv2.GaussianBlur(mask.astype(np.float32), (0, 0), .36)
    alpha[cv2.distanceTransform(mask, cv2.DIST_L2, 5) > 1.2] = 1
    alpha = np.rint(alpha.clip(0, 1) * 255).astype(np.uint8)
    alpha[alpha < 12] = 0
    # Remove green spill only on the antialiased outer edge.
    edge = (alpha > 0) & (alpha < 245)
    corrected = rgb.copy()
    corrected[:, :, 1][edge] = np.minimum(green, np.maximum(red, blue))[edge].astype(np.uint8)
    corrected[alpha == 0] = 0
    return Image.fromarray(np.dstack((corrected, alpha)), 'RGBA')


def head_root(image):
    alpha = np.asarray(image.getchannel('A'))
    x0, y0, x1, y1 = image.getbbox()
    rows = alpha[y0:y0 + round((y1 - y0) * .56)] > 128
    yy, xx = np.where(rows)
    return float((xx.min() + xx.max()) / 2), float(y0), float(y1)


def eye_material(source, rect, clip):
    """Locate dark amber irises while excluding warm ear and forehead fur."""
    x, y, width, height = rect
    rgba = np.asarray(source.crop((x, y, x + width, y + height)))
    red, green, blue, alpha = rgba.transpose(2, 0, 1).astype(np.float32)
    yy, xx = np.mgrid[:height, :width]
    candidate = (alpha > 180) & (red > green * 1.15) & (green > blue * 1.15)
    candidate &= (red > 35) & (red < 160) & (green < 120) & (blue < 90)
    candidate &= (yy > height * .18) & (yy < height * .64)
    candidate &= xx > width * (.55 if clip in ('jump','ball','brake','slide','stumble','climb') else .12)
    count, labels, stats, centers = cv2.connectedComponentsWithStats(candidate.astype(np.uint8), 8)
    regions = []
    for index in range(1, count):
        left, top, w, h, area = stats[index]
        if clip=='climb':
            neighborhood=rgba[max(0,top-2):top+h+2,max(0,left-2):left+w+2]
            pupil=(neighborhood[:,:,:3].max(axis=2)<48)&(neighborhood[:,:,3]>180)
            if pupil.sum()<max(4,w*h*.08):continue
        if area >= 14 and w >= 6 and h >= 6 and .35 < w / h < 2.5 and area / (w * h) > .15:
            regions.append((area, left, top, w, h))
    regions = sorted(sorted(regions, reverse=True)[:2], key=lambda region: region[1])
    hsv = np.asarray(Image.fromarray(rgba).convert('RGB').convert('HSV'))
    amber = (hsv[:, :, 0] >= 5) & (hsv[:, :, 0] <= 43) & (hsv[:, :, 1] > 110) & (hsv[:, :, 2] > 35) & (hsv[:, :, 2] < 225)
    mask = np.zeros((height, width), np.uint8)
    eyes = []
    for ordinal, (_, left, top, w, h) in enumerate(regions):
        region = ((xx - (left + w / 2)) / (w / 2 + 1)) ** 2 + ((yy - (top + h / 2)) / (h / 2 + 1)) ** 2 <= 1
        pigment = ordinal + 1
        mask[region & amber] = pigment
        eyes.append({'rect': [x + int(left), y + int(top), int(w), int(h)],
            'anatomical_side': 'right' if ordinal == 0 else 'left',
            'material': 'gold' if ordinal == 0 else 'blue', 'pixels': int(np.count_nonzero(region & amber))})
    return eyes, mask


def snow_material(source, eyes):
    rgba = np.asarray(source)
    rgb = rgba[:, :, :3].astype(np.float32)
    red, green, blue = rgb.transpose(2, 0, 1)
    luma = rgb @ np.array([.2126, .7152, .0722])
    value = np.interp(luma, [0, 25, 60, 120, 180, 255], [0, 20, 70, 210, 235, 255])
    result = np.stack((value + 1, value, value - 1), axis=2).clip(0, 255)
    # Orange fur and pink skin overlap at low resolution. Require the pink
    # blue-channel component so forehead/tail fur cannot become pink islands.
    pink = (red > 100) & (red - green > 8) & (green - blue < 18) & (blue > red * .52)
    weight = np.clip((red - green - 8) / 20, 0, 1) * np.clip((18 - (green - blue)) / 12, 0, 1) * pink
    result = result * (1 - weight[:, :, None]) + rgb * weight[:, :, None]
    skin = (weight >= .999) & (eyes == 0)
    result[skin] = rgb[skin]
    hsv = np.asarray(source.convert('RGB').convert('HSV')).copy()
    for pigment, hue in ((1, 27), (2, 145)):
        selected = eyes == pigment
        hsv[:, :, 0][selected] = hue
        hsv[:, :, 1][selected] = np.minimum(180, hsv[:, :, 1][selected])
        hsv[:, :, 2][selected] = np.minimum(240, hsv[:, :, 2][selected].astype(float) * 1.12).astype(np.uint8)
    painted = np.asarray(Image.fromarray(hsv, 'HSV').convert('RGB'))
    result[eyes > 0] = painted[eyes > 0]
    output = np.clip(np.dstack((result, rgba[:, :, 3])), 0, 255).astype(np.uint8)
    output[rgba[:, :, 3] == 0, :3] = 0
    return Image.fromarray(output, 'RGBA'), {'alpha_exact': np.array_equal(output[:, :, 3], rgba[:, :, 3]),
        'skin_rgb_exact': np.array_equal(output[:, :, :3][skin], rgba[:, :, :3][skin])}


def selection(clip):
    extra={'ball':range(24,84,2),'toy':range(6,146,2),'brake':range(42,142,3),
        'slide':range(42,144,2),'stumble':range(45,150,2),'climb':range(0,158,2)}
    if clip in extra:return list(extra[clip])
    if clip == 'jump':
        return list(range(42, 104))
    if clip == 'groom':
        return list(range(12, 138))
    if clip == 'knead':
        return list(range(22, 142))
    if clip == 'reach':
        return list(range(18, 142, 3))
    return list(range(22, 142, 2))


def durations(clip, indices):
    if clip == 'jump':
        return [20 if index<81 else 40 for index in indices]
    total = {'groom':5250,'knead':5200,'reach':1550,'punch':2900,
        'ball':1050,'toy':3300,'brake':900,'slide':1600,'stumble':2600,'climb':6583}[clip]
    if clip == 'punch':
        points = [(22, 0), (40, 700), (68, 1100), (102, 1700), (126, 2100), (140, 2600), (142, total)]
        clock = np.interp(indices + [142], *np.asarray(points).T)
    else:
        clock = np.linspace(0, total, len(indices) + 1)
    return [int(round(clock[i + 1]) - round(clock[i])) for i in range(len(indices))]


def bake(clip, character, indices):
    if clip=='ball' and character=='tuxedo':indices=[i+18 for i in indices]
    path = DIRECTORY / 'sources' / f'{character}-{SOURCE_ACTION[clip]}.mp4'
    video = cv2.VideoCapture(str(path))
    if not video.isOpened():
        raise ValueError('Cannot decode ' + str(path))
    frames = []
    while True:
        valid, bgr = video.read()
        if not valid:
            break
        frames.append(cv2.cvtColor(bgr, cv2.COLOR_BGR2RGB))
    fps = video.get(cv2.CAP_PROP_FPS)
    video.release()
    if max(indices) >= len(frames) or abs(fps - 24) > .01:
        raise ValueError('Unexpected original video cadence or duration')
    palette=None if clip=='climb' else background_palette(frames[0])
    neutral = foreground(frames[0],palette,clip=='ball',clip=='climb')
    center, top, bottom = head_root(neutral)
    factor = REFERENCE_HEIGHT / (bottom - top)
    columns = 8
    rows = (len(indices) + columns - 1) // columns
    atlas = Image.new('RGBA', (columns * CELL, rows * CELL))
    records = []
    paws = []
    for ordinal, source_index in enumerate(indices):
        local_palette=background_palette(frames[source_index]) if clip=='stumble' and character=='cream' else palette
        image = foreground(frames[source_index],local_palette,clip=='ball',clip=='climb')
        anchor_x, anchor_y = center, float(image.getbbox()[3])
        if clip in ('ball','brake','slide','stumble','climb'):
            anchor_x,_,_=head_root(image)
        if clip=='climb':
            anchor_x=image.getbbox()[2]-48*REFERENCE_HEIGHT/110/factor
        if clip == 'jump' and 60 <= source_index <= 80:
            moving_x, _, _ = head_root(image)
            anchor_x += moving_x - center
        image = image.resize((round(image.width * factor), round(image.height * factor)), Image.Resampling.LANCZOS)
        shift = (round(ROOT_POINT[0] - anchor_x * factor), round(ROOT_POINT[1] - anchor_y * factor))
        bounds = image.getbbox()
        if bounds[0] + shift[0] < 0 or bounds[1] + shift[1] < 0 or bounds[2] + shift[0] > CELL or bounds[3] + shift[1] > CELL:
            raise ValueError(f'Clipped {character} {clip} frame {source_index}: {bounds}, {shift}')
        cell = Image.new('RGBA', (CELL, CELL))
        cell.paste(image, shift)
        x, y = ordinal % columns * CELL, ordinal // columns * CELL
        atlas.paste(cell, (x, y))
        records.append({'rect': [x, y, CELL, CELL], 'anchor': list(ROOT_POINT)})
        if clip == 'reach':
            keys = np.asarray(REACH_KEYS[character])
            coordinates = [np.interp(source_index, keys[:, 0], keys[:, component]) for component in range(1, 5)]
            paws.append([[(coordinates[i] - anchor_x) * factor * 110 / REFERENCE_HEIGHT,
                (anchor_y - coordinates[i + 1]) * factor * 110 / REFERENCE_HEIGHT] for i in (0, 2)])
        if clip=='ball':
            keys=np.asarray(BALL_KEYS[character]);px,py=[np.interp(source_index,keys[:,0],keys[:,i]) for i in (1,2)]
            point=[(px-anchor_x)*factor*110/REFERENCE_HEIGHT,(anchor_y-py)*factor*110/REFERENCE_HEIGHT]
            paws.append([point,point])
    output = DIRECTORY / f'{character}-{clip}.png'
    atlas.save(output)
    return output, records, columns, rows, {'source': path.relative_to(DIRECTORY).as_posix(),
        'sha256': sha(path), 'source_fps': fps, 'source_frame_count': len(frames),
        'selected_frame_indices': indices, 'scale': factor, 'neutral_root': [center, bottom],
        'reference_height': REFERENCE_HEIGHT, 'paw_points_logical': paws}


def main(only=None):
    selected={clip:resource for clip,resource in RESOURCE.items() if not only or clip in only}
    entries = [entry for entry in json.loads((DIRECTORY/'manifest.json').read_text(encoding='utf-8'))['clips']
        if entry['clip'] not in selected] if only else []
    evidence = json.loads((DIRECTORY/'registration.json').read_text(encoding='utf-8')) if only else {}
    styles_path = ROOT / 'assets/styled-sprites/manifest.json'
    styles = json.loads(styles_path.read_text(encoding='utf-8'))
    replaced = set(selected.values())
    styles['atlases'] = [entry for entry in styles['atlases'] if entry['base_resource_id'] not in replaced]
    odd_policy_path = ROOT / 'assets/styled-sprites/odd-eye-policy.json'
    odd_policy = json.loads(odd_policy_path.read_text(encoding='utf-8'))
    odd_policy['atlases'] = [entry for entry in odd_policy['atlases'] if entry['base_resource_id'] not in replaced]
    iris_path = ROOT / 'assets/styled-sprites/odd-eye-iris-regions.json'
    iris = json.loads(iris_path.read_text(encoding='utf-8'))
    iris['atlases'] = [entry for entry in iris['atlases'] if entry['base_resource_id'] not in replaced]
    for clip, resource in selected.items():
        indices = selection(clip)
        exposure = durations(clip, indices)
        steps = [{'frame': index, 'ms': milliseconds} for index, milliseconds in enumerate(exposure)]
        events = []
        if clip == 'jump':
            intro = indices.index(60)
            landing = indices.index(81)
            sequence = {'intro': steps[:intro], 'loop': steps[intro:landing], 'outro': steps[landing:],
                'finish': 'complete', 'duration_ms': 0, 'events': []}
        else:
            if clip == 'punch':
                events = [{'step': indices.index(index), 'offset_ms': 0, 'type': 'contact'} for index in (40, 102)]
            sequence = {'intro': steps, 'loop': [], 'outro': [], 'finish': 'complete', 'duration_ms': 0, 'events': events}
        cream, records, columns, rows, registration = bake(clip, 'cream', indices)
        evidence['cream-' + clip] = registration
        entries.append({'clip': clip, 'character': 0, 'source': cream.name, 'columns': columns, 'rows': rows,
            'reference_height': REFERENCE_HEIGHT, 'frames': records, 'frame_count': len(records),
            'sha256': sha(cream), 'sequence': sequence})
        tuxedo, _, _, _, registration = bake(clip, 'tuxedo', indices)
        evidence['tuxedo-' + clip] = registration
        # Share one rectangle per pose while retaining both complete coats.
        # Tight source rectangles save decoded memory without fitting the
        # characters to independent bounding boxes or changing their scale.
        with Image.open(cream) as base, Image.open(tuxedo) as styled:
            for ordinal, record in enumerate(records):
                x, y, _, _ = record['rect']
                boxes = [image.crop((x, y, x + CELL, y + CELL)).getbbox() for image in (base, styled)]
                left = max(0, min(box[0] for box in boxes) - 2)
                top = max(0, min(box[1] for box in boxes) - 2)
                right = min(CELL, max(max(box[2] for box in boxes) + 2, ROOT_POINT[0] + 1))
                bottom = min(CELL, max(max(box[3] for box in boxes) + 2, ROOT_POINT[1] + 1))
                left, top = min(left, ROOT_POINT[0]), min(top, ROOT_POINT[1])
                record['rect'] = [x + left, y + top, right - left, bottom - top]
                record['anchor'] = [ROOT_POINT[0] - left, ROOT_POINT[1] - top]
        style_output = ROOT / 'assets/styled-sprites' / f'c8-r{resource}.png'
        style_output.write_bytes(tuxedo.read_bytes())
        styles['atlases'].append({'character': 8, 'base_resource_id': resource, 'resource_id': 700 + resource,
            'source': style_output.name, 'anchors': [record['anchor'] for record in records],
            'reference_height': REFERENCE_HEIGHT})
        with Image.open(cream) as source:
            snow = Image.new('RGBA', source.size)
            mask = Image.new('L', source.size)
            annotations = []
            for index, record in enumerate(records):
                x, y, width, height = record['rect']
                eyes, local_mask = eye_material(source, record['rect'], clip)
                local = source.crop((x, y, x + width, y + height))
                painted, checks = snow_material(local, local_mask)
                if not checks['alpha_exact'] or not checks['skin_rgb_exact']:
                    raise ValueError(f'OddEye {clip} frame {index} changed geometry or skin: {checks}')
                snow.paste(painted, (x, y))
                mask.paste(Image.fromarray(local_mask), (x, y))
                annotations.append({'frame': index, 'eyes': eyes})
            snow_path = ROOT / 'assets/styled-sprites' / f'c10-r{resource}.png'
            mask_path = snow_path.parent / 'odd-eye-masks' / snow_path.name
            snow.save(snow_path)
            mask.save(mask_path)
            styles['atlases'].append({'character': 10, 'base_resource_id': resource,
                'resource_id': 900 + resource, 'source': snow_path.name})
            odd_policy['atlases'].append({'base_resource_id': resource,
                'base_source': cream.relative_to(ROOT).as_posix(), 'source': snow_path.name,
                'input_sha256': sha(cream), 'output_sha256': sha(snow_path),
                'source_alpha_sha256': hashlib.sha256(source.getchannel('A').tobytes()).hexdigest(),
                'iris_mask': 'odd-eye-masks/' + snow_path.name, 'iris_mask_sha256': sha(mask_path),
                'alpha_exact': True, 'skin_rgb_exact': True})
            iris['atlases'].append({'base_resource_id': resource, 'source': snow_path.name, 'frames': annotations})
    manifest = DIRECTORY / 'manifest.json'
    write_json(manifest, {'schema': 2, 'clips': entries})
    write_json(styles_path, styles)
    odd_policy['additional_authoring_tool'] = 'tools/pack_video_actions.py'
    odd_policy['additional_authoring_tool_sha256'] = sha(Path(__file__))
    write_json(odd_policy_path, odd_policy)
    write_json(iris_path, iris)
    write_json(DIRECTORY / 'registration.json', evidence)
    header = ['// Generated by tools/pack_video_actions.py from annotated source-video paws.',
        '#pragma once', '#include "cat_deformation.hpp"', '#include <array>',
        'namespace cat::video_actions {', 'using Paws = std::array<cat::deform::Point,2>;']
    for action in ('reach','ball'):
        for character in ('cream', 'tuxedo'):
            points = evidence[character + '-'+action]['paw_points_logical']
            header.append(f'inline constexpr std::array<Paws,{len(points)}> k{character.title()}{action.title()}{{{{')
            for pair in points:
                header.append('    Paws{{' + ','.join('cat::deform::Point{' + ','.join(f'{value:.5f}' for value in point) + '}' for point in pair) + '}},')
            header.append('}};')
        header.extend([f'inline Paws {action.title()}Paws(unsigned character,unsigned frame) {{',
            f'    const auto& points=character==8?kTuxedo{action.title()}:kCream{action.title()};',
            '    return points[std::min<unsigned>(frame,points.size()-1)];', '}'])
    header.append('}')
    (ROOT / 'src/video_action_contacts.generated.hpp').write_text('\n'.join(header) + '\n', encoding='utf-8', newline='\n')
    files = [p for p in sorted(DIRECTORY.rglob('*')) if p.is_file() and p.name not in ('manifest.json', 'provenance.json')]
    write_json(DIRECTORY / 'provenance.json', {'schema': 1, 'method': 'dropshot_video_extraction',
        'manifest_sha256': sha(manifest), 'precomputed_only': True,
        'files': [{'path': p.relative_to(DIRECTORY).as_posix(), 'sha256': sha(p)} for p in files]})
    print(f'Baked {len(RESOURCE)} action families for Cream and Tuxedo')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--only',nargs='+',choices=list(RESOURCE))
    main(parser.parse_args().only)

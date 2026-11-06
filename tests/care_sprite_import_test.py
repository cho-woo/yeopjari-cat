#!/usr/bin/env python3
"""Original PNG preservation, fixed registration and fail-closed importer checks."""
from __future__ import annotations
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('care_sprite_import',ROOT/'tools/import_care_sprites.py')
importer=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(importer)


def chunk(kind,data):
    return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)


def png(width,height,pixels,filter_kind=0,color=6,depth=8):
    """Encode a complete original fixture; production importer never re-encodes."""
    scan=[];previous=bytes(width*4)
    for y in range(height):
        row=bytes(pixels[y*width*4:(y+1)*width*4]);filtered=bytearray(row)
        for x in range(len(row)):
            left=row[x-4] if x>=4 else 0;up=previous[x];upper_left=previous[x-4] if x>=4 else 0
            if filter_kind==1:predict=left
            elif filter_kind==2:predict=up
            elif filter_kind==3:predict=(left+up)//2
            elif filter_kind==4:
                estimate=left+up-upper_left;a,b,c=abs(estimate-left),abs(estimate-up),abs(estimate-upper_left)
                predict=left if a<=b and a<=c else up if b<=c else upper_left
            else:predict=0
            filtered[x]=(row[x]-predict)&255
        scan.append(bytes([filter_kind])+filtered);previous=row
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,depth,color,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(scan)))+chunk(b'IEND',b'')


def sheet(columns=4,rows=4,opaque=False,captions=False,filter_kind=0):
    width,height=columns*16,rows*16;pixels=bytearray(width*height*4)
    for row in range(rows):
        for col in range(columns):
            # Different visible pose heights must not change frame dimensions.
            for y in range(row*16+3,row*16+8+(col%3)):
                for x in range(col*16+4,col*16+12):pixels[(y*width+x)*4:(y*width+x+1)*4]=bytes((255,245,230,255))
            if captions:
                for x in range(col*16+2,col*16+14):pixels[((row*16+15)*width+x)*4:((row*16+15)*width+x+1)*4]=bytes((0,0,0,255))
    if opaque:
        for i in range(3,len(pixels),4):pixels[i]=255
    return png(width,height,pixels,filter_kind),pixels


class CareSpriteImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='yeopjari-care-import-');self.base=Path(self.temporary.name)
        self.root=self.base/'project';self.root.mkdir();(self.root/'src').mkdir()
        for name in ('cat_sprite_assets.hpp','cat_sprite_sequence.hpp'):shutil.copyfile(ROOT/'src'/name,self.root/'src'/name)
        self.inputs=self.base/'original PNGs';self.inputs.mkdir();self.manifest=self.inputs/'manifest.json'
        importer.prepare_import(self.root)

    def tearDown(self):self.temporary.cleanup()

    def entry(self,name='groom',character=0,**overrides):
        _,_,columns,rows=importer.LAYOUT[name];data,_=sheet(columns,rows)
        filename=f'{character}-{name}.png';(self.inputs/filename).write_bytes(data)
        result={'clip':name,'character':character,'source':filename,'columns':columns,'rows':rows,'reference_height':40,'anchor':[8,16]}
        result.update(overrides);return result

    def write_manifest(self,entries):
        self.manifest.write_text(json.dumps({'schema':1,'clips':entries})+'\n');return self.manifest

    def apply(self,entries,check=False):
        return importer.prepare_import(self.root,self.write_manifest(entries),check)

    def test_empty_default_keeps_legacy_artwork(self):
        result=importer.prepare_import(self.root,check=True)
        self.assertEqual(result['clips'],0)
        self.assertFalse((self.root/'assets').exists())
        self.assertIn('std::array<Descriptor,0>',(self.root/importer.HEADER).read_text())

    def test_default_grid_preserves_original_bytes_and_fixed_registration(self):
        entry=self.entry();original=(self.inputs/entry['source']).read_bytes();result=self.apply([entry])
        copied=self.root/'assets/care-sprites/c0-groom.png';self.assertEqual(copied.read_bytes(),original)
        self.assertEqual(result['resource_ids'],[113])
        canonical=json.loads((self.root/importer.CANONICAL).read_text())['clips'][0]
        self.assertEqual(canonical['reference_height'],40)
        self.assertEqual({tuple(frame['rect'][2:]) for frame in canonical['frames']},{(16,16)})
        self.assertEqual({tuple(frame['anchor']) for frame in canonical['frames']},{(8,16)})
        self.assertEqual(canonical['sha256'],hashlib.sha256(original).hexdigest())
        self.assertEqual(importer.prepare_import(self.root,check=True)['clips'],1)
        self.assertEqual((self.inputs/entry['source']).read_bytes(),original)

    def test_explicit_caption_roi_and_per_frame_anchor_are_not_recentered(self):
        entry=self.entry();data,_=sheet(captions=True);(self.inputs/entry['source']).write_bytes(data)
        entry.pop('anchor');entry['frames']=[{'rect':[(n%4)*16,(n//4)*16,16,14],'anchor':[7.5,13]} for n in range(16)]
        entry['frames'][1]={'rect':[18,0,12,14],'anchor':[5.25,12.5]}
        self.apply([entry]);canonical=json.loads((self.root/importer.CANONICAL).read_text())['clips'][0]
        self.assertEqual(canonical['frames'],entry['frames'])
        self.assertEqual((self.root/'assets/care-sprites/c0-groom.png').read_bytes(),data)
        self.assertIn('Frame{{18,0,12,14},{5.25,12.5}}',(self.root/importer.HEADER).read_text())

    def test_eight_frame_sleep_story_preserves_all_uploaded_poses(self):
        entry=self.entry('sleep');entry['rows']=2
        data,_=sheet(4,2);(self.inputs/entry['source']).write_bytes(data)
        self.apply([entry]);canonical=json.loads((self.root/importer.CANONICAL).read_text())['clips'][0]
        self.assertEqual(len(canonical['frames']),8)
        self.assertEqual(canonical['reference_height'],40)
        self.assertEqual((self.root/'assets/care-sprites/c0-sleep.png').read_bytes(),data)
        self.assertEqual(importer.prepare_import(self.root,check=True)['resource_ids'],[114])

    def test_all_eight_layouts_and_stable_resource_ids(self):
        entries=[self.entry(name) for name,_,_,_ in importer.CLIPS]+[self.entry('treat',9)]
        result=self.apply(entries)
        self.assertEqual(result['resource_ids'],list(range(113,121))+[192])
        self.assertNotIn(108,result['resource_ids'])
        rc=(self.root/importer.RC).read_text();self.assertIn('192 RCDATA "../assets/care-sprites/c9-treat.png"',rc)
        self.assertEqual(importer.prepare_import(self.root,check=True)['clips'],9)

    @unittest.skipUnless(shutil.which('g++'),'C++ compiler unavailable')
    def test_generated_metadata_compiles_and_find_is_typed(self):
        self.apply([self.entry('sleep',1),self.entry('treat',9)])
        source=self.base/'check.cpp';source.write_text('''#include "care_sprites.generated.hpp"
using namespace cat::sprite_assets;
static_assert(kCareSprites.size()==2);
static_assert(Valid(kCareSprites[0])&&Valid(kCareSprites[1]));
static_assert(ResourceId(cat::sprite_sequence::Clip::Sleep,1)==122);
static_assert(ResourceId(cat::sprite_sequence::Clip::Treat,9)==192);
static_assert(Find(cat::sprite_sequence::Clip::Sleep,1,kCareSprites)->frames[0].anchor.y==16);
static_assert(Find(cat::sprite_sequence::Clip::Sleep,0,kCareSprites)==nullptr);
int main(){return 0;}
''')
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(self.root/'src'),str(source),'-o',str(self.base/'check')],check=True,capture_output=True)

    def test_invalid_manifest_is_rejected_before_any_output_changes(self):
        good=self.entry();baseline={name:(self.root/name).read_bytes() for name in (importer.HEADER,importer.RC)}
        bad_entries=[dict(good,columns=3),dict(good,rows=5),dict(good,character=10),dict(good,character=True),
                     dict(good,reference_height=0),dict(good,reference_height=float('nan')),dict(good,reference_height=True),
                     dict(good,reference_height=10**400),dict(good,source='missing.png'),dict(good,clip='walk'),
                     dict(good,frames=[]),dict(good,anchor=[17,16]),dict(good,anchor=[-1,16]),dict(good,anchor=[0,float('inf')])]
        for entry in bad_entries:
            with self.subTest(entry=str(entry)[:100]),self.assertRaises(importer.ImportError):self.apply([good,entry] if entry['clip']!=good['clip'] else [entry])
            for name,data in baseline.items():self.assertEqual((self.root/name).read_bytes(),data)
            self.assertFalse((self.root/'assets').exists())
        with self.assertRaisesRegex(importer.ImportError,'duplicate'):self.apply([good,good])

    def test_frame_bounds_and_missing_anchor_fail_closed(self):
        for bad in ({'rect':[0,0,65,16],'anchor':[8,16]}, {'rect':[60,0,16,16],'anchor':[8,16]},
                    {'rect':[0,64,16,16],'anchor':[8,16]}, {'rect':[0,0,16,0],'anchor':[8,0]},
                    {'rect':[0,0,16,16],'anchor':[8,17]}, {'rect':[0,0,16,16]}):
            entry=self.entry();entry.pop('anchor');entry['frames']=[{'rect':[(n%4)*16,(n//4)*16,16,16],'anchor':[8,16]} for n in range(16)];entry['frames'][0]=bad
            with self.subTest(bad=bad),self.assertRaises(importer.ImportError):self.apply([entry])

    def test_opaque_checker_or_white_background_is_not_guessed_away(self):
        entry=self.entry();data,_=sheet(opaque=True);(self.inputs/entry['source']).write_bytes(data)
        with self.assertRaisesRegex(importer.ImportError,'transparent'):self.apply([entry])
        self.assertFalse((self.root/'assets').exists())

    def test_png_filters_reconstruct_alpha_without_editing_rgb(self):
        for kind in range(5):
            data,pixels=sheet(filter_kind=kind);w,h,alpha=importer.png_alpha(data)
            self.assertEqual(alpha,bytes(pixels[3::4]));self.assertEqual((w,h),(64,64))

    def test_png_crc_truncation_wrong_format_and_corrupt_data_reject(self):
        data,pixels=sheet();bad_crc=bytearray(data);bad_crc[36]^=1
        unsupported=png(64,64,pixels,color=2)
        malformed=[bytes(bad_crc),data[:-4],data+b'trailing',unsupported,png(64,64,pixels,filter_kind=5),
                   b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',64,64,8,6,0,0,0))+chunk(b'IDAT',b'bad')+chunk(b'IEND',b'')]
        for value in malformed:
            with self.subTest(size=len(value)),self.assertRaises(importer.ImportError):importer.png_alpha(value)

    def test_empty_or_opaque_individual_frame_rejects(self):
        entry=self.entry();data,pixels=sheet()
        for y in range(16):
            for x in range(16):pixels[(y*64+x)*4+3]=0
        (self.inputs/entry['source']).write_bytes(png(64,64,pixels))
        with self.assertRaisesRegex(importer.ImportError,r'frames\[0\]'):self.apply([entry])

    def test_check_detects_source_or_generated_drift_and_does_not_write(self):
        self.apply([self.entry()]);header=self.root/importer.HEADER;rc=self.root/importer.RC
        before=header.read_bytes();header.write_bytes(before+b'// edited\n')
        with self.assertRaisesRegex(importer.ImportError,'drift'):importer.prepare_import(self.root,check=True)
        self.assertTrue(header.read_bytes().endswith(b'// edited\n'));header.write_bytes(before)
        rc.write_text(rc.read_text()+'999 RCDATA "unexpected.png"\n')
        with self.assertRaises(importer.ImportError):importer.prepare_import(self.root,check=True)
        self.apply([self.entry()]);path=self.root/'assets/care-sprites/c0-groom.png';changed,_=sheet(filter_kind=1);path.write_bytes(changed)
        with self.assertRaisesRegex(importer.ImportError,'checksum'):importer.prepare_import(self.root,check=True)

    def test_determinism_sorted_manifest_and_explicit_missing_manifest(self):
        entries=[self.entry('treat',9),self.entry('sleep',0)];self.apply(entries)
        before={str(p.relative_to(self.root)):p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        self.apply(list(reversed(entries)));after={str(p.relative_to(self.root)):p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        self.assertEqual(before,after)
        with self.assertRaisesRegex(importer.ImportError,'Missing manifest'):importer.prepare_import(self.root,self.inputs/'missing.json')
        self.manifest.write_text('{"schema":1,"schema":1,"clips":[]}')
        with self.assertRaises(importer.ImportError):importer.prepare_import(self.root,self.manifest)

if __name__=='__main__':unittest.main()

#!/usr/bin/env python3
"""Authored appearance bytes, shared geometry and complete-character coverage."""
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
SPEC=importlib.util.spec_from_file_location('styled_importer',ROOT/'tools/import_styled_sprites.py')
importer=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(importer)


def png(width,height,red):
    def chunk(kind,data):
        return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    rows=[]
    for y in range(height):
        rows.append(bytes([0])+b''.join(bytes((red,90,80,255 if x==y else 0)) for x in range(width)))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(b''.join(rows)))+chunk(b'IEND',b'')


class StyledSpriteImportTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(); self.directory=Path(self.temporary.name)
        self.root=self.directory/'project'; self.care=self.root/'assets/care-sprites';self.care.mkdir(parents=True)
        (self.root/'src').mkdir()
        (self.care/'c0-groom.png').write_bytes(png(4,4,250))
        (self.care/'manifest.json').write_text(json.dumps({'schema':2,'clips':[
            {'clip':'groom','character':0,'source':'c0-groom.png','columns':1,'rows':1,
             'reference_height':4,'frame_count':1,'frames':[{'rect':[0,0,4,4],'anchor':[2,4]}],
             'sequence':{'loop':[{'frame':0,'ms':100}]}}]}))
        self.source=self.directory/'new-groom.png'; self.source.write_bytes(png(4,4,30))
        self.manifest=self.directory/'manifest.json'
        self.entry={'character':8,'base_resource_id':113,'resource_id':500,'source':'new-groom.png'}

    def tearDown(self):
        self.temporary.cleanup()

    def apply(self,entries=None,**options):
        self.manifest.write_text(json.dumps({'schema':1,'design':'new authored kitten','atlases':entries or [self.entry]}))
        return importer.prepare_import(self.root,self.manifest,**options)

    def test_original_bytes_and_registration_are_preserved(self):
        original=(self.care/'c0-groom.png').read_bytes();data=self.source.read_bytes()
        report=self.apply()
        self.assertEqual((self.root/'assets/styled-sprites/c8-r113.png').read_bytes(),data)
        self.assertEqual((self.care/'c0-groom.png').read_bytes(),original)
        self.assertEqual(report['complete_characters'],[])
        self.assertEqual(report['sources'][0]['image_width'],4)
        self.assertNotEqual(report['sources'][0]['sha256'],report['sources'][0]['base_sha256'])
        self.assertEqual(importer.prepare_import(self.root,check=True)['sources'],report['sources'])
        self.assertFalse((self.root/'src/care_sprites.generated.hpp').exists())

    def test_authored_anchor_offsets_are_explicit_and_bounded(self):
        manifest=json.loads((self.care/'manifest.json').read_text())
        manifest['clips'][0]['frames']=[{'rect':[0,0,4,4],'anchor':[2,4]}]
        (self.care/'manifest.json').write_text(json.dumps(manifest))
        entry=dict(self.entry,anchors=[[1.5,3.5]],reference_height=4)
        report=self.apply([entry])
        self.assertEqual(report['sources'][0]['anchors'],[[1.5,3.5]])
        self.assertIn('kStyledAnchors8_113.data()', (self.root/importer.HEADER).read_text())
        self.assertEqual(importer.prepare_import(self.root,check=True)['sources'],report['sources'])
        for invalid in (dict(entry,anchors=[]),dict(entry,anchors=[[5,2]]),dict(entry,reference_height=0)):
            with self.assertRaises(importer.ImportError):self.apply([invalid])

    def test_dimension_drift_or_original_copy_is_rejected(self):
        for data in (png(5,4,30),(self.care/'c0-groom.png').read_bytes()):
            self.source.write_bytes(data)
            with self.assertRaises(importer.ImportError):self.apply()
        self.assertFalse((self.root/importer.HEADER).exists())

    def test_derived_source_and_64_anchor_registration_replace_original_layout(self):
        directory=self.root/'assets/derived-motion';directory.mkdir()
        (directory/'c0-locomotion-flow.png').write_bytes(png(8,8,250))
        self.source.write_bytes(png(8,8,30))
        derived={'clip':'walk','character':0,'resource':400,'filename':'c0-walk.png',
                 'asset_path':'assets/derived-motion/c0-locomotion-flow.png',
                 'frames':[{'rect':[0,0,8,8],'anchor':[4,8]} for _ in range(64)]}
        entry=dict(self.entry,base_resource_id=400,resource_id=508,anchors=[[4,8] for _ in range(64)])
        with mock.patch.object(importer,'runtime_locomotion',return_value=([derived],2)):
            report=self.apply([entry]);atlas=report['sources'][0]
            self.assertEqual(atlas['base_source'],'assets/derived-motion/c0-locomotion-flow.png')
            self.assertEqual((atlas['image_width'],atlas['image_height']),(8,8))
            self.assertEqual(len(atlas['anchors']),64)
            with self.assertRaises(importer.ImportError):self.apply([dict(entry,anchors=[[4,8]]*8)])

    def test_missing_fallbacks_duplicate_resources_and_hash_drift_fail(self):
        with self.assertRaises(importer.ImportError):self.apply(require_character=8)
        second=dict(self.entry,character=2)
        with self.assertRaises(importer.ImportError):self.apply([self.entry,second])
        with self.assertRaises(importer.ImportError):self.apply([dict(self.entry,sha256='wrong')])
        self.apply();(self.root/'assets/styled-sprites/c8-r113.png').write_bytes(png(4,4,40))
        with self.assertRaises(importer.ImportError):importer.prepare_import(self.root,check=True)

    def test_new_video_walk_and_retained_run_have_separate_appearance_resources(self):
        natural=self.root/'assets/natural-gait';natural.mkdir()
        legacy=self.root/'assets/derived-motion';legacy.mkdir()
        (natural/'cream.png').write_bytes(png(8,8,250))
        (legacy/'cream.png').write_bytes(png(4,4,250))
        self.source.write_bytes(png(8,8,30))
        old_source=self.directory/'old-run.png';old_source.write_bytes(png(4,4,25))
        entries=[dict(self.entry,base_resource_id=400,resource_id=508),
                 dict(self.entry,base_resource_id=401,resource_id=516,source=old_source.name)]
        runtime=[{'clip':'walk','character':0,'resource':400,'filename':'c0-walk.png','asset_path':'assets/natural-gait/cream.png',
                  'frames':[{'rect':[0,0,8,8],'anchor':[4,8]} for _ in range(20)]},
                 {'clip':'run','character':0,'resource':401,'filename':'c0-run.png','asset_path':'assets/derived-motion/cream.png',
                  'frames':[{'rect':[0,0,4,4],'anchor':[2,4]} for _ in range(64)]}]
        with mock.patch.object(importer,'runtime_locomotion',return_value=(runtime,1)):
            report=self.apply(entries)
        self.assertEqual([a['base_resource_id'] for a in report['sources']],[400,401])
        self.assertEqual([a['resource_id'] for a in report['sources']],[508,516])
        self.assertEqual([a['image_width'] for a in report['sources']],[8,4])
        self.assertEqual(report['sources'][1]['base_source'],'assets/derived-motion/cream.png')

    @unittest.skipUnless(shutil.which('g++'),'C++ compiler unavailable')
    def test_generated_interface_selects_pixels_without_animation_metadata(self):
        self.apply()
        for name in ('cat_styled_sprite_assets.hpp','cat_sprite_assets.hpp','cat_sprite_sequence.hpp'):
            shutil.copyfile(ROOT/'src'/name,self.root/'src'/name)
        source=self.directory/'check.cpp';source.write_text('''#include "styled_sprites.generated.hpp"
using namespace cat::styled_assets;
static_assert(kStyledAtlases.size()==1 && ValidSet(kStyledAtlases));
static_assert(Find(8,113,kStyledAtlases)->resourceId==500);
static_assert(Find(0,113,kStyledAtlases)==nullptr);
static_assert(Find(8,400,kStyledAtlases)==nullptr);
int main(){return 0;}
''')
        subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(self.root/'src'),str(source),'-o',str(self.directory/'check')],check=True,capture_output=True)

if __name__=='__main__':
    unittest.main()

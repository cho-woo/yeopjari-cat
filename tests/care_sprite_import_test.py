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
from unittest import mock
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

    def write_manifest(self,entries,schema=1):
        self.manifest.write_text(json.dumps({'schema':schema,'clips':entries})+'\n');return self.manifest

    def apply(self,entries,check=False,schema=1):
        return importer.prepare_import(self.root,self.write_manifest(entries,schema),check)

    def authored(self,count,columns=16,clip='groom',**overrides):
        rows=(count+columns-1)//columns
        entry=self.entry(clip,columns=columns,rows=rows,frame_count=count)
        _,pixels=sheet(columns,rows)
        # Fixture poses are individually identifiable, not duplicate images
        # presented as newly authored production artwork.
        width=columns*16
        for frame in range(count):
            x=(frame%columns)*16+7;y=(frame//columns)*16+5
            pixels[(y*width+x)*4:(y*width+x+1)*4]=bytes((frame,255-frame,frame//2,255))
        (self.inputs/entry['source']).write_bytes(png(width,rows*16,pixels))
        entry['sequence']={'loop':[{'frame':index,'ms':25+(index%3)} for index in range(count)],'finish':'complete'}
        if clip in ('walk','run'):entry['stride_pixels']=72 if clip=='walk' else 150
        entry.update(overrides);return entry

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

    def derived_fixture(self):
        walk=self.authored(16,columns=4,clip='walk')
        frames=[{'rect':[(i%4)*16,(i//4)*16,16,16],'anchor':[8,16]} for i in range(16)]
        walk.update(frame_count=8,frames=frames[:8],sequence={'loop':[{'frame':i,'ms':125} for i in range(8)]})
        run=dict(walk,clip='run',frames=frames[8:],stride_pixels=150)
        self.apply([self.entry('groom'),walk,run],schema=2)
        canonical=self.root/importer.CANONICAL
        original=json.loads(canonical.read_text())
        directory=self.root/'assets/derived-motion';directory.mkdir()
        derived=self.authored(128,columns=8,clip='walk')
        data=(self.inputs/derived['source']).read_bytes()
        atlas=directory/'c0-locomotion-flow.png';atlas.write_bytes(data)
        registrations=[{'rect':[(i%8)*16,(i//8)*16,16,16],'anchor':[8,16]} for i in range(128)]
        clips=[]
        for name,offset,stride in [('walk',0,72),('run',64,150)]:
            clips.append(dict(derived,clip=name,source=atlas.name,frame_count=64,
                frames=registrations[offset:offset+64],stride_pixels=stride,
                sequence={'loop':[{'frame':i,'ms':125} for i in range(64)]}))
        manifest=directory/'manifest.json';manifest.write_text(json.dumps({'schema':2,'clips':clips}))
        source=directory/'source-registration.json'
        source.write_text(json.dumps({'schema':2,'clips':[c for c in original['clips'] if c['clip'] in ('walk','run')]}))
        generator=self.root/'tools/derive_locomotion_flow.py';generator.parent.mkdir()
        generator.write_bytes((ROOT/'tools/derive_locomotion_flow.py').read_bytes())
        original_png=self.root/'assets/care-sprites/c0-walk.png'
        sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
        report={'schema':1,'precomputed_only':True,'whole_sprite_alpha_blend':False,
            'generator':'tools/derive_locomotion_flow.py','generator_sha256':sha(generator),
            'source_registration':'assets/derived-motion/source-registration.json','source_registration_sha256':sha(source),
            'original_source':'assets/care-sprites/c0-walk.png','original_source_sha256':sha(original_png),
            'derived_manifest_sha256':sha(manifest),'atlas':{'source':atlas.name,'sha256':sha(atlas)},
            'clips':[{'clip':name,'source_frame_count':8,'original_drawings':8,'derived_frame_count':64}
                     for name in ('walk','run')]}
        (directory/'provenance.json').write_text(json.dumps(report))
        return canonical,original_png,atlas

    def test_default_derived_motion_overrides_runtime_without_replacing_originals(self):
        canonical,original,atlas=self.derived_fixture()
        before={path:path.read_bytes() for path in (canonical,original)}
        result=importer.prepare_import(self.root)
        self.assertEqual(result['derived_locomotion_clips'],2)
        self.assertEqual(result['actual_frames'],16+64+64)
        self.assertEqual({path:path.read_bytes() for path in before},before)
        header=(self.root/importer.HEADER).read_text();rc=(self.root/importer.RC).read_text()
        self.assertEqual(header.count('assets/derived-motion/c0-locomotion-flow.png'),2)
        self.assertEqual(rc.count('400 RCDATA "../assets/derived-motion/c0-locomotion-flow.png"'),1)
        self.assertNotIn('401 RCDATA',rc)
        self.assertEqual(importer.prepare_import(self.root,check=True)['derived_locomotion_clips'],2)

    def test_derived_atlas_drift_fails_before_generated_outputs_change(self):
        _,_,atlas=self.derived_fixture()
        before={name:(self.root/name).read_bytes() for name in (importer.HEADER,importer.RC)}
        atlas.write_bytes(atlas.read_bytes()+b'drift')
        with self.assertRaisesRegex(importer.ImportError,'atlas drift'):
            importer.prepare_import(self.root)
        self.assertEqual({name:(self.root/name).read_bytes() for name in before},before)

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

    @unittest.skipUnless(shutil.which('g++'),'C++ compiler unavailable')
    def test_64_128_256_authored_frames_generate_valid_compilable_programs(self):
        for count in (64,128,256):
            with self.subTest(frames=count):
                entry=self.authored(count);original=(self.inputs/entry['source']).read_bytes()
                result=self.apply([entry],schema=2)
                self.assertEqual(result['actual_frames'],count)
                self.assertEqual(result['unique_sheets'],1)
                self.assertEqual(result['decoded_frame_bytes'],count*16*16*4)
                self.assertEqual((self.root/'assets/care-sprites/c0-groom.png').read_bytes(),original)
                self.assertEqual(importer.prepare_import(self.root,check=True)['actual_frames'],count)
                source=self.base/'authored.cpp';source.write_text('''#include "care_sprites.generated.hpp"
using namespace cat::sprite_assets;
constexpr auto& asset=kCareSprites[0];
static_assert(Valid(asset));
static_assert(asset.frameCount==COUNT);
static_assert(asset.program.stepCount==COUNT);
static_assert(asset.program.loop.first==0&&asset.program.loop.count==COUNT);
static_assert(asset.program.steps[COUNT-1].frame==COUNT-1);
static_assert(asset.frames[COUNT-1].rect.width==16);
int main(){
    std::uint64_t elapsed=0;
    cat::sprite_sequence::Options options;options.sheetFrames=COUNT;
    for(unsigned i=0;i<COUNT;++i){
        if(asset.program.steps[i].frame!=i||asset.program.steps[i].durationMs!=25+i%3)return 1;
        const auto pose=cat::sprite_sequence::SampleProgram(asset.clip,asset.program,elapsed+12,options);
        if(pose.frame!=i||pose.finished)return 2;
        elapsed+=asset.program.steps[i].durationMs;
    }
    const auto done=cat::sprite_sequence::SampleProgram(asset.clip,asset.program,elapsed,options);
    if(!done.finished||done.frame!=COUNT-1)return 3;
    return 0;
}
'''.replace('COUNT',str(count)))
                result=subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(self.root/'src'),str(source),'-o',str(self.base/'authored')],capture_output=True,text=True)
                self.assertEqual(result.returncode,0,result.stderr[:6000])
                subprocess.run([str(self.base/'authored')],check=True,capture_output=True)

    def test_timed_entrance_loop_exit_events_and_partial_grid_are_preserved(self):
        entry=self.authored(65,sequence={'intro':[{'frame':0,'ms':400}],
            'loop':[{'frame':32,'ms':125},{'frame':63,'ms':175}],
            'outro':[{'frame':64,'ms':800}],'finish':'hold','duration_ms':5000,
            'events':[{'step':1,'offset_ms':90,'type':'contact'},{'step':3,'offset_ms':0,'type':'landing'}]})
        self.apply([entry],schema=2)
        canonical=json.loads((self.root/importer.CANONICAL).read_text())
        self.assertEqual(canonical['schema'],2)
        self.assertEqual(canonical['clips'][0]['frame_count'],65)
        self.assertEqual(canonical['clips'][0]['sequence'],entry['sequence'])
        header=(self.root/importer.HEADER).read_text()
        for fragment in ('Range{0,1}','Range{1,2}','Range{3,1}','Finish::Hold,5000,2', 'Cue{1,90,1}','Cue{3,0,4}'):
            self.assertIn(fragment,header)

    def test_walk_run_shared_atlas_is_decoded_once_and_embedded_once(self):
        walk=self.authored(16,columns=4,clip='walk');run=dict(walk,clip='run',stride_pixels=150)
        walk['frame_count']=run['frame_count']=8
        frames=[{'rect':[(i%4)*16,(i//4)*16,16,16],'anchor':[8,16]} for i in range(16)]
        walk['frames']=frames[:8];run['frames']=frames[8:]
        walk['sequence']=run['sequence']={'loop':[{'frame':i,'ms':100} for i in range(8)]}
        with mock.patch.object(importer,'png_alpha',wraps=importer.png_alpha) as decode:
            result=self.apply([run,walk],schema=2)
            self.assertEqual(decode.call_count,1)
        self.assertEqual(result['resource_ids'],[400,400])
        self.assertEqual(result['unique_sheets'],1)
        self.assertEqual(result['actual_frames'],16)
        self.assertEqual((self.root/importer.RC).read_text().count(' RCDATA '),1)
        self.assertFalse((self.root/'assets/care-sprites/c0-run.png').exists())
        canonical=json.loads((self.root/importer.CANONICAL).read_text())['clips']
        self.assertEqual([clip['source'] for clip in canonical],['c0-walk.png','c0-walk.png'])
        self.assertEqual(importer.prepare_import(self.root,check=True)['resource_ids'],[400,400])
        run=self.authored(8,columns=4,clip='run')
        self.assertEqual(self.apply([walk,run],schema=2)['resource_ids'],[400,401])
        all_characters=[dict(entry,character=character) for character in range(10) for entry in (walk,run)]
        ids=self.apply(all_characters,schema=2)['resource_ids']
        self.assertEqual(ids,list(range(400,420)))
        self.assertTrue(set(ids).isdisjoint(range(301,315)))

    def test_authored_metadata_bounds_fail_before_output_mutation(self):
        baseline={name:(self.root/name).read_bytes() for name in (importer.HEADER,importer.RC)}
        entry=self.authored(64)
        malformed=[dict(entry,frame_count=257),dict(entry,frame_count=0),dict(entry,columns=17,rows=16),
                   dict(entry,sequence={'loop':[{'frame':64,'ms':10}]}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':0}]}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':60001}]}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':True}]}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':10}]*513}),
                   dict(entry,sequence={'loop':[]}),dict(entry,sequence={'finish':'unknown'}),
                   dict(entry,sequence={'intro':[{'frame':0,'ms':500}],'duration_ms':499}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':10}],'duration_ms':86400001}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':10}],'events':[{'step':0,'offset_ms':10,'type':'contact'}]}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':10}],'events':[{'step':1,'offset_ms':0,'type':'contact'}]}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':10}],'events':[{'step':0,'offset_ms':0,'type':'unknown'}]}),
                   dict(entry,sequence={'loop':[{'frame':0,'ms':10}],'events':[{'step':0,'offset_ms':0,'type':'contact'}]*65}),
                   dict(entry,stride_pixels=float('nan')),dict(entry,stride_pixels=-1),
                   dict(entry,standing_frame=256),dict(entry,standing_frame=-1),dict(entry,standing_frame=True)]
        no_sequence=dict(entry);no_sequence.pop('sequence');malformed.append(no_sequence)
        for value in malformed:
            with self.subTest(value=str(value)[-200:]),self.assertRaises(importer.ImportError):self.apply([value],schema=2)
            for name,data in baseline.items():self.assertEqual((self.root/name).read_bytes(),data)
            self.assertFalse((self.root/'assets').exists())

    def test_oversize_png_and_resident_frame_budget_reject(self):
        for width,height in ((16385,1),(16384,2049),(1,0)):
            # Hostile dimensions must fail before attempting zlib decode.
            value=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,6,0,0,0))+chunk(b'IDAT',b'')+chunk(b'IEND',b'')
            with self.subTest(size=(width,height)),self.assertRaisesRegex(importer.ImportError,'dimensions'):importer.png_alpha(value)
        entry=self.authored(64)
        with mock.patch.object(importer,'MAX_BYTES',10),self.assertRaisesRegex(importer.ImportError,'32MiB'):
            self.apply([entry],schema=2)
        with mock.patch.object(importer,'MAX_RESIDENT_PIXELS',64*16*16-1),self.assertRaisesRegex(importer.ImportError,'resident budget'):
            self.apply([entry],schema=2)
        self.assertFalse((self.root/'assets').exists())

if __name__=='__main__':unittest.main()

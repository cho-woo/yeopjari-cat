"""Offline provenance and PCM quality regressions for all current action sounds."""
from array import array
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_reference_audio as reference

REJECTED_FIRST_BURST_HASHES = {
    "1fcafc4b169dc05db9d204bf5e2ec65a11fde15abf23706844604a6ee49b9e62", # former meal
    "2733c9ecc05f57b7c31f565cd19aff598522c68424b89d2a96b95eab2dc0f747", # former pet
    "fc145ffa2da066ea1d010b48f979462c63ed3c1534e7eda2ff62262d2541c3e6", # former meal-nogari
}


class ReferenceAudioTests(unittest.TestCase):
    def test_all_fourteen_slots_match_reviewed_output_pins(self):
        with mock.patch.object(reference.subprocess, 'check_output', side_effect=AssertionError('Offline verification must not run FFmpeg')):
            self.assertEqual(reference.verify()['verified_cues'], 14)
        manifest = json.loads((reference.OUTPUT / 'manifest.json').read_text())
        self.assertEqual(manifest['version'], reference.SOUND_SET_VERSION)
        self.assertIsNone(manifest['license'])
        self.assertFalse(manifest['pitch_changed'])
        self.assertFalse(manifest['speed_changed'])
        self.assertEqual([c['resource_id'] for c in manifest['cues']], list(range(301,315)))
        # These are changes from the archived v1.9 sound set, not stale CC0 cues.
        baseline = json.loads((ROOT / 'docs/v1.9.0-files.json').read_text())
        for name, _, _ in reference.CUES:
            with self.subTest(cue=name):
                payload = (reference.OUTPUT / (name+'.wav')).read_bytes()
                self.assertNotEqual(hashlib.sha256(payload).hexdigest(), baseline['assets/audio/'+name+'.wav'])

    def test_rejected_burst_is_excluded_from_every_slot_and_generation(self):
        self.assertEqual(set(reference.SEGMENTS), {'b','c','d'})
        manifest = json.loads((reference.OUTPUT/'manifest.json').read_text())
        self.assertEqual(manifest['excluded_segments'], [
            {'segment':'a','start_seconds':.18,'end_seconds':.91,'reason':'user_rejected'}])
        self.assertEqual(set(manifest['excerpt_reuse']), {'b','c','d'})
        reused = [cue for names in manifest['excerpt_reuse'].values() for cue in names]
        self.assertEqual(len(reused),14)
        self.assertEqual(set(reused), {name for name,_,_ in reference.CUES})
        for cue in manifest['cues']:
            with self.subTest(cue=cue['cue']):
                self.assertIn(cue['segment'], {'b','c','d'})
                self.assertTrue(cue['end_seconds'] <= .18 or cue['start_seconds'] >= .91)
                self.assertIn(cue['cue'], manifest['excerpt_reuse'][cue['segment']])
                payload=(reference.OUTPUT/(cue['cue']+'.wav')).read_bytes()
                self.assertNotIn(hashlib.sha256(payload).hexdigest(),REJECTED_FIRST_BURST_HASHES)
        # Reject the named burst and a renamed approved label pointing at the
        # same rejected source range, before accessing any source samples.
        for segment in ['a','renamed-first-burst']:
            with self.assertRaisesRegex(ValueError,'Forbidden'):
                reference.prepare([],segment,.05)
        with mock.patch.dict(reference.SEGMENTS, {'b':(.18,.91)}):
            with self.assertRaisesRegex(ValueError,'Forbidden'):
                reference.prepare([],'b',.05)

    def test_output_only_verification_rejects_forbidden_aliases_and_wrong_deduplication(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'audio';shutil.copytree(reference.OUTPUT,output)
            missing=Path(tmp)/'absent.mp4';canonical=Path(tmp)/'absent.wav'
            path=output/'manifest.json';original=path.read_text()
            for index in range(14):
                manifest=json.loads(original)
                cue=manifest['cues'][index]
                cue.update(segment='a',start_seconds=.18,end_seconds=.91,
                           start_frame=3969,end_frame=20066)
                path.write_text(json.dumps(manifest))
                with self.subTest(cue=cue['cue']),self.assertRaisesRegex(ValueError,'Forbidden'):
                    reference.verify(output,missing,canonical)
            path.write_text(original)
            manifest=json.loads(original);manifest['excluded_segments']=[]
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'exclusion'):
                reference.verify(output,missing,canonical)
            manifest=json.loads(original);manifest['excerpt_reuse']['a']=manifest['excerpt_reuse'].pop('b')
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'reuse'):
                reference.verify(output,missing,canonical)
            # Both inputs are valid clips from allowed ranges, but assigning a
            # shared clip to the wrong cue must not pass by rewriting its hash.
            manifest=json.loads(original)
            replacement=(output/'punch.wav').read_bytes()
            (output/'pet.wav').write_bytes(replacement)
            for cue in manifest['cues']:
                if cue['cue']=='pet':
                    cue['sha256']=hashlib.sha256(replacement).hexdigest()
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'pinned finished'):
                reference.verify(output,missing,canonical)

    @unittest.skipUnless(reference.SOURCE.is_file() and reference.CANONICAL.is_file(),
                         "Private reference media is not distributed in the public checkout")
    def test_rejected_audio_cannot_be_replayed_under_an_allowed_label(self):
        source=reference.sources()
        # Build a regression input representing the rejected old Pet sound,
        # deliberately bypassing the new generation guard only in this test.
        with mock.patch.object(reference,'segment_bounds',return_value=(.18,.91)):
            payload,metadata=reference.prepare(source,'b',.05)
        self.assertIn(hashlib.sha256(payload).hexdigest(),REJECTED_FIRST_BURST_HASHES)
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'audio';shutil.copytree(reference.OUTPUT,output)
            (output/'pet.wav').write_bytes(payload)
            path=output/'manifest.json';manifest=json.loads(path.read_text())
            for cue in manifest['cues']:
                if cue['cue']=='pet':cue.update(metadata)
            path.write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError,'pinned finished'):
                reference.verify(output,Path(tmp)/'absent.mp4',Path(tmp)/'absent.wav')

    def test_levels_duration_format_and_click_free_boundaries(self):
        for name, _, _ in reference.CUES:
            with self.subTest(cue=name):
                samples = reference.read_pcm((reference.OUTPUT / (name+'.wav')).read_bytes())
                self.assertGreaterEqual(len(samples)/reference.SAMPLE_RATE,.25)
                self.assertLessEqual(len(samples)/reference.SAMPLE_RATE,4)
                self.assertEqual(samples[0],0)
                self.assertEqual(samples[-1],0)
                peak = max(map(abs,samples))
                rms = math.sqrt(sum(x*x for x in samples)/len(samples))/32768
                self.assertLessEqual(peak/32768,reference.PEAK_LIMIT)
                self.assertGreater(rms,.01)
                self.assertLess(rms,.10)
                # Bound the first/last two milliseconds rather than treating
                # a zero endpoint alone as evidence against a hard cut.
                edge = round(reference.SAMPLE_RATE * .002)
                self.assertLess(max(map(abs,samples[:edge])), peak*.08)
                self.assertLess(max(map(abs,samples[-edge:])), peak*.08)
                window = round(reference.SAMPLE_RATE*.05)
                energies = [math.sqrt(sum(x*x for x in samples[i:i+window])/len(samples[i:i+window]))/32768
                            for i in range(0,len(samples),window)]
                self.assertGreater(sum(e>.002 for e in energies),len(energies)*.7)

    @unittest.skipUnless(reference.SOURCE.is_file() and reference.CANONICAL.is_file(),
                         "Private reference media is not distributed in the public checkout")
    def test_private_reconstruction_is_deterministic_without_ffmpeg(self):
        source = reference.sources()
        for name, segment, rms in reference.CUES:
            with self.subTest(cue=name):
                first,meta = reference.prepare(source,segment,rms)
                second,again = reference.prepare(source,segment,rms)
                self.assertEqual(first,second)
                self.assertEqual(meta,again)
                self.assertEqual(first,(reference.OUTPUT/(name+'.wav')).read_bytes())

    def test_tampered_wav_metadata_and_source_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp)/'audio'
            shutil.copytree(reference.OUTPUT,output)
            p=output/'pet.wav';original=p.read_bytes();changed=bytearray(original);changed[-100]^=1;p.write_bytes(changed)
            with self.assertRaisesRegex(ValueError,'differs'):
                reference.verify(output)
            p.write_bytes(original)
            manifest_path=output/'manifest.json';original_manifest=manifest_path.read_text();m=json.loads(original_manifest)
            m['cues'][0]['start_seconds']+=.01;manifest_path.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'metadata'):
                reference.verify(output)
            manifest_path.write_text(original_manifest)
            m=json.loads(original_manifest);m['version']='0.0.0';manifest_path.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'version'):
                reference.verify(output)
            manifest_path.write_text(original_manifest)
            m=json.loads(original_manifest);m['license']='CC0 1.0';manifest_path.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'license'):
                reference.verify(output)
            manifest_path.write_text(original_manifest)
            m=json.loads(original_manifest);m['cues'][1]=m['cues'][0];manifest_path.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'duplicate'):
                reference.verify(output)
            manifest_path.write_text(original_manifest)
            bad_source=Path(tmp)/'bad.mp4';bad_source.write_bytes(b'not the provided source')
            with self.assertRaisesRegex(ValueError,'video checksum'):
                reference.verify(output,bad_source)

    def test_public_checkout_verifies_outputs_without_claiming_source_reconstruction(self):
        with tempfile.TemporaryDirectory() as tmp:
            missing = Path(tmp)/'private-source.mp4'
            canonical = Path(tmp)/'private-decode.wav'
            with mock.patch.object(reference, 'sources', side_effect=AssertionError('Private source must not be read')):
                result = reference.verify(source=missing, canonical=canonical)
                self.assertEqual(result['verification_mode'],'outputs_only')
                self.assertEqual(result['verified_cues'],14)
                self.assertFalse(result['source_video_verified'])
                self.assertFalse(result['canonical_decode_verified'])
                self.assertFalse(result['sample_reconstruction_verified'])
            with self.assertRaisesRegex(ValueError,'Private source'):
                reference.verify(source=missing, canonical=canonical,require_source=True)
            output=Path(tmp)/'audio';shutil.copytree(reference.OUTPUT,output)
            wav=output/'pet.wav';changed=bytearray(wav.read_bytes());changed[-100]^=1;wav.write_bytes(changed)
            manifest=output/'manifest.json';m=json.loads(manifest.read_text())
            for cue in m['cues']:
                if cue['cue']=='pet':cue['sha256']=hashlib.sha256(changed).hexdigest()
            manifest.write_text(json.dumps(m))
            with self.assertRaisesRegex(ValueError,'pinned finished'):
                reference.verify(output,missing,canonical)

    def test_generation_requires_original_private_video(self):
        with tempfile.TemporaryDirectory() as tmp:
            absent=Path(tmp)/'unavailable.mp4'
            with mock.patch.object(reference.subprocess,'check_output',side_effect=AssertionError('No source means no decode')):
                with self.assertRaisesRegex(ValueError,'private original video'):
                    reference.generate(Path(tmp)/'output',absent,Path(tmp)/'absent.wav')
            self.assertFalse((Path(tmp)/'output').exists())

    def test_historical_verifiers_dispatch_current_provenance(self):
        import prepare_audio
        import prepare_user_audio
        self.assertEqual(prepare_audio.verify(reference.OUTPUT)['verified_cues'],14)
        self.assertEqual(prepare_user_audio.verify(reference.OUTPUT)['verified_cues'],14)


if __name__ == '__main__':
    unittest.main()

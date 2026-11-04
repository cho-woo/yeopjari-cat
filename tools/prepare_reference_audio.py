#!/usr/bin/env python3
"""Prepare all 14 cues from the user-provided video, without changing pitch/time.

Generation requires FFmpeg and the private original video. Verification uses
only Python's standard library: a public checkout verifies pinned finished clips
and metadata without requiring or claiming access to the original recording.
When both private inputs are present, it additionally reconstructs every sample.
The executable embeds only the finished WAV files, never the reference media.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import io
import json
import math
from pathlib import Path
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "build-inputs/audio-reference/reference.mp4"
CANONICAL = ROOT / "build-inputs/audio-reference/reference-mono-22050.wav"
OUTPUT = ROOT / "assets/audio"
# Introduction version of this exact pinned sound set, independent of later app
# releases that reuse these clips. Change only when reviewing a new sound set.
SOUND_SET_VERSION = "1.10.3"
SOURCE_SHA256 = "4e5981866b19199ba23c1e8388c16fc16a8b913bec1f12611b8580beba824e14"
CANONICAL_SHA256 = "c696a270b57352c1278d11fa9550897a042796718134e36a5bb71dbb4368e877"
PCM_SHA256 = "984b7ce5fedc87f7ba33f6a9bf127c1740b0d5088c1cf57b30d90ca6e90ea9a1"
SAMPLE_RATE = 22050
SOURCE_FRAMES = 109368
PEAK_LIMIT = .60
FADE_IN_MS = 12
FADE_OUT_MS = 45

# Quiet margins surround intact waveform bursts. The final burst is cut off by
# the uploaded video's end, so it is deliberately not used. These are waveform
# labels, not claims that different animal vocalisations were identified by ear.
SEGMENTS = {
    "a": (.18, .91),
    "b": (2.12, 2.63),
    "c": (2.68, 3.22),
    "d": (4.00, 4.74),
}
# Stable resource order (301..314), source segment and whole-clip target RMS.
# Short source length means clips are reused; no extra calls are synthesised.
CUES = [
    ("hungry", "c", .075), ("meal", "a", .065),
    ("treat", "d", .070), ("refuse", "b", .055),
    ("pet", "a", .050), ("jump", "b", .065),
    ("punch", "c", .075), ("wake", "b", .045),
    ("food-request", "d", .075), ("meal-fish", "c", .060),
    ("meal-nogari", "a", .060), ("punch-second", "b", .070),
    ("gesture", "b", .050), ("greeting", "d", .075),
]


# Reviewed finished-clip pins remain independently verifiable without private media.
OUTPUT_PINS = {
    "hungry": {
        "sha256": "81fd6ca3797b396313e4c70d8b78ef8eabf639d0a3e8dd01fe9d34e6d782cdc6",
        "gain": 3.764597251
    },
    "meal": {
        "sha256": "1fcafc4b169dc05db9d204bf5e2ec65a11fde15abf23706844604a6ee49b9e62",
        "gain": 3.749479145052905
    },
    "treat": {
        "sha256": "815e98e7eee674e6b9aa8f9d55d52c8808a0f923edc253c6b476226545c88860",
        "gain": 3.1880811355411307
    },
    "refuse": {
        "sha256": "8579710801387d834d1464b7f3adb50a14e37e5455380fb328394e18731315f9",
        "gain": 5.349002728595451
    },
    "pet": {
        "sha256": "2733c9ecc05f57b7c31f565cd19aff598522c68424b89d2a96b95eab2dc0f747",
        "gain": 2.884214726963773
    },
    "jump": {
        "sha256": "26a68f2ed0f977637aeac5cfa847876a251a1af6d48c1ed06a3016c2e272bb21",
        "gain": 6.32154867924917
    },
    "punch": {
        "sha256": "81fd6ca3797b396313e4c70d8b78ef8eabf639d0a3e8dd01fe9d34e6d782cdc6",
        "gain": 3.764597251
    },
    "wake": {
        "sha256": "23d2feea87b73e4086d1eb21752a00a2969664074f4c6d5d74afa3b014d9667b",
        "gain": 4.3764567779417325
    },
    "food-request": {
        "sha256": "b387d94bfe8b138dfa45e5ab3dd6b932068217408a37b44ba55ae6eddc24a2a8",
        "gain": 3.4158012166512113
    },
    "meal-fish": {
        "sha256": "5ccb91c5b97f5cd75f5b3aa753c49ddc8d4e7add7cac3492846e62c325303baa",
        "gain": 3.0116778008000002
    },
    "meal-nogari": {
        "sha256": "fc145ffa2da066ea1d010b48f979462c63ed3c1534e7eda2ff62262d2541c3e6",
        "gain": 3.461057672356527
    },
    "punch-second": {
        "sha256": "75d5cf5f0c1311dbdae76a6b50110a2c7fe92ac64218e8b4dc6f258b5d7bb8e6",
        "gain": 6.807821654576029
    },
    "gesture": {
        "sha256": "9faf79e5ed535b6c56c946c13e187f3c01e3d0697f25c4679a15b96ea4dcd9e8",
        "gain": 4.862729753268592
    },
    "greeting": {
        "sha256": "b387d94bfe8b138dfa45e5ab3dd6b932068217408a37b44ba55ae6eddc24a2a8",
        "gain": 3.4158012166512113
    }
}

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_pcm(payload: bytes) -> array:
    with wave.open(io.BytesIO(payload), "rb") as sound:
        if (sound.getnchannels(), sound.getsampwidth(), sound.getframerate(), sound.getcomptype()) != (1, 2, SAMPLE_RATE, "NONE"):
            raise ValueError("Expected mono PCM16/22050Hz")
        pcm = array("h", sound.readframes(sound.getnframes()))
    if sys.byteorder != "little":
        pcm.byteswap()
    return pcm


def wav_bytes(samples) -> bytes:
    pcm = array("h", samples)
    if sys.byteorder != "little":
        pcm.byteswap()
    stream = io.BytesIO()
    with wave.open(stream, "wb") as sound:
        sound.setnchannels(1)
        sound.setsampwidth(2)
        sound.setframerate(SAMPLE_RATE)
        sound.writeframes(pcm.tobytes())
    return stream.getvalue()


def sources(source: Path = SOURCE, canonical: Path = CANONICAL) -> array:
    if digest(source.read_bytes()) != SOURCE_SHA256:
        raise ValueError("Reference video checksum mismatch")
    payload = canonical.read_bytes()
    if digest(payload) != CANONICAL_SHA256:
        raise ValueError("Canonical source decode checksum mismatch")
    pcm = read_pcm(payload)
    if len(pcm) != SOURCE_FRAMES:
        raise ValueError("Unexpected canonical source duration")
    return pcm


def prepare(pcm, segment: str, wanted_rms: float) -> tuple[bytes, dict]:
    if segment not in SEGMENTS or not math.isfinite(wanted_rms) or not .01 <= wanted_rms <= .10:
        raise ValueError("Invalid cue parameters")
    start, end = SEGMENTS[segment]
    first, last = round(start * SAMPLE_RATE), round(end * SAMPLE_RATE)
    values = [value / 32768.0 for value in pcm[first:last]]
    if len(values) != last - first:
        raise ValueError("Reference segment exceeds decoded audio")
    # A constant DC removal, amplitude adjustment and raised-cosine boundary
    # fades preserve the source timing and pitch. No loops, resynthesis, EQ or
    # stochastic processing are used.
    mean = sum(values) / len(values)
    values = [value - mean for value in values]
    fade_in = round(FADE_IN_MS * SAMPLE_RATE / 1000)
    fade_out = round(FADE_OUT_MS * SAMPLE_RATE / 1000)
    for i in range(fade_in):
        values[i] *= .5 - .5 * math.cos(math.pi * i / (fade_in - 1))
    for i in range(fade_out):
        values[-1-i] *= .5 - .5 * math.cos(math.pi * i / (fade_out - 1))
    peak = max(map(abs, values))
    rms = math.sqrt(sum(x*x for x in values) / len(values))
    if rms < .0001:
        raise ValueError("Reference segment is silent")
    gain = min(wanted_rms / rms, PEAK_LIMIT / peak)
    finished = [round(x * gain * 32767) for x in values]
    payload = wav_bytes(finished)
    metadata = {
        "segment": segment, "start_seconds": start, "end_seconds": end,
        "start_frame": first, "end_frame": last,
        "fade_in_ms": FADE_IN_MS, "fade_out_ms": FADE_OUT_MS,
        "target_rms": wanted_rms, "gain": gain,
        "duration_seconds": len(finished) / SAMPLE_RATE,
        "peak": max(map(abs, finished)) / 32768.0,
        "rms": math.sqrt(sum(x*x for x in finished) / len(finished)) / 32768.0,
        "bytes": len(payload), "sha256": digest(payload),
    }
    return payload, metadata


def verify(output: Path = OUTPUT, source: Path = SOURCE, canonical: Path = CANONICAL,
           require_source: bool = False) -> dict:
    source_present, canonical_present = source.is_file(), canonical.is_file()
    if require_source and not (source_present and canonical_present):
        raise ValueError("Private source video and canonical decode are required for reconstruction")
    # Present inputs are always checked; an altered private file must never be
    # ignored by silently falling back to public-output verification.
    if source_present and digest(source.read_bytes()) != SOURCE_SHA256:
        raise ValueError("Reference video checksum mismatch")
    if canonical_present and digest(canonical.read_bytes()) != CANONICAL_SHA256:
        raise ValueError("Canonical source decode checksum mismatch")
    pcm = sources(source, canonical) if source_present and canonical_present else None
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") != 3 or manifest.get("source_kind") != "user_provided_video":
        raise ValueError("Expected user-provided video audio manifest")
    if manifest.get("version") != SOUND_SET_VERSION:
        raise ValueError("Audio manifest version differs from the pinned sound-set version")
    if (manifest.get("source_sha256") != SOURCE_SHA256 or manifest.get("canonical_sha256") != CANONICAL_SHA256 or
            manifest.get("source") != "build-inputs/audio-reference/reference.mp4" or
            manifest.get("canonical") != "build-inputs/audio-reference/reference-mono-22050.wav" or
            manifest.get("decoded_pcm_sha256") != PCM_SHA256):
        raise ValueError("Incorrect reference provenance")
    if manifest.get("license") is not None or manifest.get("pitch_changed") is not False or manifest.get("speed_changed") is not False:
        raise ValueError("Incorrect license or audio-processing declaration")
    entries = manifest.get("cues", [])
    names = {name for name, _, _ in CUES}
    if len(entries) != len(CUES) or {entry.get("cue") for entry in entries} != names:
        raise ValueError("Missing, duplicate or unexpected reference audio cue")
    if {p.stem for p in output.glob("*.wav")} != names:
        raise ValueError("Unexpected WAV files in current audio set")
    by_name = {entry["cue"]: entry for entry in entries}
    for index, (name, segment, target_rms) in enumerate(CUES):
        entry = by_name[name]
        payload = (output / (name + ".wav")).read_bytes()
        actual_hash = digest(payload)
        if actual_hash != OUTPUT_PINS[name]["sha256"] or entry.get("sha256") != actual_hash:
            raise ValueError("Cue differs from the pinned finished clip: " + name)
        samples = read_pcm(payload)
        first, last = (round(t * SAMPLE_RATE) for t in SEGMENTS[segment])
        if len(samples) != last - first or not .25 <= len(samples) / SAMPLE_RATE <= 4:
            raise ValueError("Invalid cue duration: " + name)
        peak = max(map(abs, samples)) / 32768.0
        rms = math.sqrt(sum(x*x for x in samples) / len(samples)) / 32768.0
        metadata = {
            "segment": segment, "start_seconds": SEGMENTS[segment][0], "end_seconds": SEGMENTS[segment][1],
            "start_frame": first, "end_frame": last,
            "fade_in_ms": FADE_IN_MS, "fade_out_ms": FADE_OUT_MS,
            "target_rms": target_rms, "gain": OUTPUT_PINS[name]["gain"],
            "duration_seconds": len(samples) / SAMPLE_RATE,
            "peak": peak, "rms": rms, "bytes": len(payload), "sha256": actual_hash,
        }
        for key, value in metadata.items():
            if entry.get(key) != value:
                raise ValueError("Incorrect cue edit metadata: " + name + "/" + key)
        if entry.get("resource_id") != 301 + index:
            raise ValueError("Changed cue resource identity: " + name)
        if samples[0] or samples[-1] or max(map(abs, samples)) >= 32767:
            raise ValueError("Unfaded or clipped cue: " + name)
        if rms < .01 or peak > PEAK_LIMIT:
            raise ValueError("Silent or overly loud cue: " + name)
        edge = round(SAMPLE_RATE * .002)
        if max(map(abs, samples[:edge])) > peak * 32768 * .08 or max(map(abs, samples[-edge:])) > peak * 32768 * .08:
            raise ValueError("Abrupt cue boundary: " + name)
        if pcm is not None:
            expected, _ = prepare(pcm, segment, target_rms)
            if payload != expected:
                raise ValueError("Cue differs from the private reference segment: " + name)
    return {"verified_cues": len(CUES), "source_seconds": SOURCE_FRAMES / SAMPLE_RATE,
            "segments": len(SEGMENTS), "source_sha256": SOURCE_SHA256,
            "verification_mode": "source_and_outputs" if pcm is not None else "outputs_only",
            "source_video_verified": source_present, "canonical_decode_verified": canonical_present,
            "sample_reconstruction_verified": pcm is not None}


def generate(output: Path = OUTPUT, source: Path = SOURCE, canonical: Path = CANONICAL) -> dict:
    if not source.is_file():
        raise ValueError("Generation requires the private original video via --source")
    if digest(source.read_bytes()) != SOURCE_SHA256:
        raise ValueError("Reference video checksum mismatch")
    if canonical.is_file() and digest(canonical.read_bytes()) != CANONICAL_SHA256:
        raise ValueError("Canonical source decode checksum mismatch")
    decoded = subprocess.check_output([
        "ffmpeg", "-v", "error", "-i", str(source), "-map", "0:a:0", "-vn",
        "-ac", "1", "-ar", str(SAMPLE_RATE), "-f", "s16le", "-acodec", "pcm_s16le", "pipe:1",
    ])
    if digest(decoded) != PCM_SHA256:
        raise ValueError("FFmpeg decode differs from the pinned canonical audio; do not overwrite it")
    pcm = array("h", decoded)
    if sys.byteorder != "little":
        pcm.byteswap()
    if len(pcm) != SOURCE_FRAMES:
        raise ValueError("Unexpected decoded source duration")
    output.mkdir(parents=True, exist_ok=True)
    cues = []
    for index, (name, segment, rms) in enumerate(CUES):
        payload, metadata = prepare(pcm, segment, rms)
        (output / (name + ".wav")).write_bytes(payload)
        cues.append({"cue": name, "resource_id": 301 + index, **metadata})
    manifest = {
        "schema": 3, "version": SOUND_SET_VERSION, "source_kind": "user_provided_video",
        "source": "build-inputs/audio-reference/reference.mp4", "source_sha256": SOURCE_SHA256,
        "canonical": "build-inputs/audio-reference/reference-mono-22050.wav",
        "canonical_sha256": CANONICAL_SHA256, "decoded_pcm_sha256": PCM_SHA256,
        "source_audio_seconds": SOURCE_FRAMES / SAMPLE_RATE,
        "license": None,
        "license_note": "User-provided reference supplied for this requested replacement; no CC0, authorship, or other license is asserted.",
        "pitch_changed": False, "speed_changed": False,
        "edits": "Waveform-selected bounded bursts with quiet margins; mono downmix and resample, constant DC removal, level adjustment and 12ms/45ms raised-cosine fades. No looping, synthetic calls, pitch shift or time stretch. Four source excerpts are reused across fourteen cue slots. The truncated final source burst is excluded.",
        "cues": cues,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    result = verify(output, source, canonical)
    result.update(verification_mode="generated_from_source", source_video_verified=True,
                  canonical_decode_verified=True, sample_reconstruction_verified=True)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify", action="store_true", help="Verify finished clips offline; reconstruct only when both private inputs exist")
    parser.add_argument("--require-source", action="store_true", help="With --verify, require both private inputs and sample reconstruction")
    parser.add_argument("--output-dir", type=Path, default=OUTPUT)
    parser.add_argument("--source", type=Path, default=SOURCE)
    parser.add_argument("--canonical", type=Path, default=CANONICAL)
    args = parser.parse_args()
    result = verify(args.output_dir, args.source, args.canonical, args.require_source) if args.verify else generate(args.output_dir, args.source, args.canonical)
    print(json.dumps(result, indent=2))

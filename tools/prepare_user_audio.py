#!/usr/bin/env python3
"""Prepare the user's v1.7 cat cues; verify without FFmpeg or network access.

Generation requires Python 3.10+ and FFmpeg on PATH. Input MP3s are pinned by
SHA256, decoded to floating point (not clipped to PCM16 first), and never
uploaded. The Windows program embeds the final WAVs and does not run this tool.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import io
import json
import math
from pathlib import Path
import shutil
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/audio"
SAMPLE_RATE = 22050
PEAK_LIMIT = .68
SOURCE_HASHES = {
    "jump": ("점프 소리.mp3", "26719dc3605379ccd59b249d484c88dd5922823f7a97c639497fb540122a3813"),
    "greeting": ("먼저 말걸때 소리.mp3", "7bb0f9a8f709c173a8138d1fc2f27a93eb587454b5c9585d80233418d7f3b2f4"),
    "hungry": ("배고플때소리.mp3", "b30eeef7a785a06fe1ace5f958ae6b7f9aefd0d5f361ecd8e324e7c45bed2219"),
    "meal": ("밥 사료 먹는 소리.mp3", "c0f71e6204f7d316e51b003b56204d92cfa273f1045030204559a2d0c939e157"),
    "meal-fish": ("생선 먹는 소리.mp3", "f467709861fad114652f0f6fca599f87fc3e0e3f9e83492823f1b817082422e7"),
    "treat": ("츄르먹는 소리.mp3", "2d57faf2373d2d806455c5b2ad94c578ed337f8999f0895d9f758e3c59568529"),
    "meal-nogari": ("노가리 먹는 소리.mp3", "c073b6082f94cc36685ffe647d54a4d9bfdf18e19225e49d15278f88b12597b3"),
    "food-request": ("밥달라는 울음 소리.mp3", "617718cb61801369d44dc7941a8576b50d57d7677d1a8a43c092a745f87e6777"),
    "punch-second": ("냥냥펀치 소리 2.mp3", "d76cc094a1e4ec0ec134cde84c7e19179ba040470a203054df9afb7033723215"),
    "gesture": ("귀세우거나 꼬리세울떄 소리.mp3", "a79aa786100e0b2269ab9b02f623b50f86b651d6227ba90224eeabfb9757a921"),
    "punch": ("냥냥펀치 소리 1.mp3", "02ef149dd6b5bfca9353e368c403a05ad86f6e97dc1f444893326df51d8da521"),
}
# Cue; decoded-source start/end seconds; high-pass Hz; target RMS; cosine fades.
# Long eating recordings are excerpts; calls use a complete short event with
# a small lead-in and tail. No change of pitch/speed and no synthetic overlay.
CUES = [
    ("hungry", .55, 1.60, 65, .090, 12, 80),
    ("food-request", .24, 1.32, 65, .105, 12, 90),
    ("meal", 2.80, 5.90, 55, .055, 35, 110),
    ("meal-fish", 1.20, 4.30, 55, .055, 35, 110),
    ("meal-nogari", 10.50, 13.60, 55, .055, 35, 110),
    ("treat", 11.50, 14.80, 50, .050, 35, 130),
    ("jump", .90, 1.67, 65, .090, 12, 70),
    ("punch", .06, 1.02, 60, .090, 12, 70),
    ("punch-second", .08, 3.12, 40, .090, 18, 110),
    ("gesture", .53, 1.43, 65, .080, 12, 80),
    ("greeting", .81, 1.65, 65, .090, 12, 90),
]
RETAINED = ("refuse", "pet", "wake")
RETAINED_HASHES = {
    "refuse": "75b8fbf72189698f6f48f70ea6317e47a10eb92e1540edbb5acb65184d2069e3",
    "pet": "ab5c77ee70df1a6fea9700a7b3d6a9dbe002ac6aec24c745a1a550a1a1657160",
    "wake": "c9d65898143b3bad0088d338523df7d2f18360729ff332b5109eeea34e64c745",
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def source_bytes(source_dir: Path) -> dict[str, bytes]:
    sources = {}
    for cue, (_, expected) in SOURCE_HASHES.items():
        path = source_dir / (cue + ".mp3")
        if not 0 < path.stat().st_size <= 10 * 1024 * 1024:
            raise ValueError("Unexpected MP3 size: " + path.name)
        data = path.read_bytes()
        if digest(data) != expected:
            raise ValueError("The user source differs from its pinned SHA256: " + path.name)
        sources[cue] = data
    return sources


def decode(path: Path, ffmpeg: str) -> tuple[list[float], str]:
    command = [ffmpeg, "-nostdin", "-hide_banner", "-loglevel", "error", "-i", str(path),
               "-map", "0:a:0", "-vn", "-t", "61", "-ac", "1", "-ar", str(SAMPLE_RATE),
               "-f", "f32le", "pipe:1"]
    result = subprocess.run(command, capture_output=True, check=True, timeout=30)
    raw = result.stdout
    if not raw or len(raw) % 4 or len(raw) > SAMPLE_RATE * 60 * 4:
        raise ValueError("Decoded MP3 must contain at most 60 seconds of float32 audio")
    pcm = array("f", raw)
    if sys.byteorder != "little":
        pcm.byteswap()
    samples = list(pcm)
    if not all(math.isfinite(x) for x in samples):
        raise ValueError("Decoded source contains invalid floating-point samples")
    return samples, digest(raw)


def prepare(samples: list[float], start: float, end: float, highpass: float,
            wanted_rms: float, fade_in_ms: int, fade_out_ms: int) -> tuple[list[int], float]:
    low, high = round(start * SAMPLE_RATE), round(end * SAMPLE_RATE)
    if not 0 <= low < high <= len(samples) or high-low > SAMPLE_RATE * 4:
        raise ValueError("Invalid cue crop")
    # Source MP3 synthesis can exceed +/-1.0; retain float headroom until gain
    # reduction and final quantization so no extra PCM16 clipping is introduced.
    a = math.exp(-2 * math.pi * highpass / SAMPLE_RATE)
    previous_x = previous_y = 0.0
    processed = []
    for sample in samples[low:high]:
        previous_y = a * (previous_y + sample - previous_x)
        previous_x = sample
        processed.append(previous_y)
    for reverse, milliseconds in ((False, fade_in_ms), (True, fade_out_ms)):
        length = min(len(processed), max(2, round(milliseconds * SAMPLE_RATE / 1000)))
        for index in range(length):
            weight = .5 - .5 * math.cos(math.pi * index / (length-1))
            processed[-1-index if reverse else index] *= weight
    peak = max(map(abs, processed))
    rms = math.sqrt(sum(x*x for x in processed) / len(processed))
    if peak < 1e-7 or rms < 1e-8:
        raise ValueError("Cue excerpt is effectively silent")
    gain = min(wanted_rms / rms, PEAK_LIMIT / peak)
    # A bounded uniform gain keeps the original timing and transient shape.
    output = [round(x * gain * 32767) for x in processed]
    if max(map(abs, output)) > round(PEAK_LIMIT * 32767):
        raise ValueError("Peak bound failed")
    return output, gain


def wav_bytes(samples: list[int]) -> bytes:
    pcm = array("h", samples)
    if sys.byteorder != "little":
        pcm.byteswap()
    stream = io.BytesIO()
    with wave.open(stream, "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm.tobytes())
    return stream.getvalue()


def verify(output: Path, source_dir: Path | None = None) -> dict:
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") == 3:
        from prepare_reference_audio import verify as verify_reference_audio
        return verify_reference_audio(output)
    expected = set(SOURCE_HASHES) | set(RETAINED)
    cues = manifest.get("cues", [])
    if manifest.get("schema") != 2 or manifest.get("version") != "1.7.0":
        raise ValueError("Expected the v1.7 mixed-provenance audio manifest")
    if len(cues) != len(expected) or {cue.get("cue") for cue in cues} != expected:
        raise ValueError("Unexpected, missing or duplicate audio cues")
    sources = source_bytes(source_dir or output / "source-user")
    total = 0
    longest = 0.0
    for cue in cues:
        name = cue["cue"]
        payload = (output / (name + ".wav")).read_bytes()
        if len(payload) != cue["bytes"] or digest(payload) != cue["sha256"]:
            raise ValueError("Changed WAV: " + name)
        if name in SOURCE_HASHES:
            if (cue.get("source_kind"), cue.get("source_sha256"), cue.get("source")) != (
                "user_provided", SOURCE_HASHES[name][1], "source-user/" + name + ".mp3"
            ):
                raise ValueError("Incorrect user-source provenance: " + name)
        elif cue.get("source_kind") != "cc0_legacy" or digest(payload) != RETAINED_HASHES[name]:
            raise ValueError("Changed retained CC0 cue: " + name)
        with wave.open(io.BytesIO(payload), "rb") as audio:
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) != (
                1, 2, SAMPLE_RATE, "NONE"
            ):
                raise ValueError("Unexpected PCM format: " + name)
            pcm = array("h", audio.readframes(audio.getnframes()))
            if sys.byteorder != "little":
                pcm.byteswap()
            seconds = len(pcm) / SAMPLE_RATE
            if not .25 <= seconds <= 4 or abs(seconds-cue["duration_seconds"]) > 1e-8:
                raise ValueError("Invalid cue duration: " + name)
            if pcm[0] != 0 or pcm[-1] != 0 or max(map(abs, pcm)) > round(PEAK_LIMIT * 32767):
                raise ValueError("Unfaded or clipped cue: " + name)
            if not any(pcm):
                raise ValueError("Silent cue: " + name)
            longest = max(longest, seconds)
        total += len(payload)
    return {"cues": len(cues), "user_cues": len(sources), "retained_cc0_cues": len(RETAINED),
            "wav_bytes": total, "source_mp3_bytes": sum(map(len, sources.values())),
            "longest_seconds": longest, "pcm": "mono PCM16 little-endian / 22050 Hz",
            "source_hashes_checked": True, "verified": True}


def generate(source_dir: Path, output: Path) -> dict:
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise ValueError("Install FFmpeg for generation; --verify requires only Python")
    sources = source_bytes(source_dir)
    version = subprocess.run([ffmpeg, "-version"], capture_output=True, text=True,
                             check=True, timeout=10).stdout.splitlines()[0]
    legacy_path = OUTPUT / "manifest-legacy-v15.json"
    legacy_data = legacy_path.read_bytes()
    legacy = json.loads(legacy_data)
    legacy_by_name = {row["cue"]: row for row in legacy["cues"]}
    payloads, entries = {}, []
    for name in RETAINED:
        payload = (OUTPUT / (name + ".wav")).read_bytes()
        if digest(payload) != RETAINED_HASHES[name]:
            raise ValueError("Retained WAV differs from v1.5: " + name)
        payloads[name] = payload
        entry = dict(legacy_by_name[name])
        entry.update(source_kind="cc0_legacy", license="CC0 1.0", creator="Kerzoven",
                     license_url=legacy["license_url"], source_page=legacy["source_page"])
        entries.append(entry)
    for name, start, end, cutoff, wanted_rms, fade_in, fade_out in CUES:
        samples, decoded_hash = decode(source_dir / (name + ".mp3"), ffmpeg)
        result, gain = prepare(samples, start, end, cutoff, wanted_rms, fade_in, fade_out)
        payload = wav_bytes(result)
        payloads[name] = payload
        entries.append({"cue": name, "source_kind": "user_provided",
                        "source": "source-user/" + name + ".mp3", "uploaded_name": SOURCE_HASHES[name][0],
                        "source_sha256": SOURCE_HASHES[name][1], "source_bytes": len(sources[name]),
                        "decoded_source_seconds": len(samples) / SAMPLE_RATE,
                        "decoded_mono_float32_sha256": decoded_hash, "crop_seconds": [start, end],
                        "highpass_hz": cutoff, "fade_ms": [fade_in, fade_out], "gain": gain,
                        "target_rms": wanted_rms, "duration_seconds": len(result) / SAMPLE_RATE,
                        "peak": max(map(abs, result)) / 32768,
                        "rms": math.sqrt(sum(x*x for x in result)/len(result))/32768,
                        "bytes": len(payload), "sha256": digest(payload)})
    manifest = {"schema": 2, "version": "1.7.0", "sample_rate": SAMPLE_RATE,
                "channels": 1, "pcm_bits": 16, "peak_limit": PEAK_LIMIT,
                "source_groups": {
                    "user_provided": {"count": 11, "provided_date": "2026-11-03",
                                      "license": None, "license_note": "User-supplied audio for this requested game update; no CC0 or other new license is asserted."},
                    "cc0_legacy": {"count": 3, "creator": "Kerzoven", "license": "CC0 1.0",
                                   "source_page": legacy["source_page"], "license_url": legacy["license_url"]}},
                "decoder": version,
                "processing": "MP3 decoded directly to mono float32/22050Hz; crop; one-pole high-pass; cosine fades; bounded uniform RMS/peak gain; PCM16 quantization. No pitch or speed change, loop or synthetic overlay.",
                "inspection": "File labels plus waveform/energy analysis; real speaker listening and native Windows playback were not performed in this environment.",
                "legacy_manifest": "manifest-legacy-v15.json", "cues": entries}
    # All source and crop validation completes before writing any finished cue.
    output.mkdir(parents=True, exist_ok=True)
    (output / "source-user").mkdir(exist_ok=True)
    for name, payload in sources.items():
        destination = output / "source-user" / (name + ".mp3")
        if not destination.exists() or destination.read_bytes() != payload:
            destination.write_bytes(payload)
    (output / "manifest-legacy-v15.json").write_bytes(legacy_data)
    for name, payload in payloads.items():
        destination = output / (name + ".wav")
        if not destination.exists() or destination.read_bytes() != payload:
            destination.write_bytes(payload)
    (output / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return verify(output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=OUTPUT / "source-user")
    parser.add_argument("--output-dir", type=Path, default=OUTPUT)
    parser.add_argument("--verify", action="store_true", help="Verify all 14 WAVs and original MP3 hashes; no FFmpeg or network")
    args = parser.parse_args()
    current = args.output_dir / "manifest.json"
    if not args.verify and current.exists() and json.loads(current.read_text(encoding="utf-8")).get("schema") == 3:
        raise ValueError("Current cues use the reference video; use prepare_reference_audio.py or a separate historical output directory")
    result = verify(args.output_dir, args.source_dir) if args.verify else generate(args.source_dir, args.output_dir)
    print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

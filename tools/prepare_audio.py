#!/usr/bin/env python3
"""Prepare short real-cat PCM cues from pinned CC0 recordings.

Python 3.10+ standard library only. The shipped app embeds the finished WAVs;
this development helper and its optional network downloads are not run by it.
"""
from __future__ import annotations

import argparse
from array import array
import hashlib
import json
import math
from pathlib import Path
import sys
import urllib.request
import wave

ROOT = Path(__file__).resolve().parents[1]
SOURCE_PAGE = "https://opengameart.org/content/cat-purr-meow"
LICENSE_URL = "https://creativecommons.org/publicdomain/zero/1.0/"
DOWNLOAD_BASE = "https://opengameart.org/sites/default/files/"
SOURCE_HASHES = {
    "cat_mewfood.wav": "3550f080882b52e236c8810a353756b2e0395fb7dadf0aeface0a06893cd181f",
    "cat_mewpurr.wav": "a58487959ee00c812f9f02ca822a668173bcf04a98c6dd98a902514a1d3994ea",
    "cat_mewpurr2.wav": "918a896111936f3f6316d1cc7fd64aa1ef0a31a4162dfcbe02c058758b0f2458",
    "cat_softmew.wav": "7de9fed4401b677ccbf072781182857d875093a8e9b5727fc21b38028cfd5a90",
    "cat_purrsleepy_loop.wav": "7f06368e25e6a7aec9b106c3e2915f236b3fa10b41781733ecc8a1866fc749b9",
    "cat_purractive_loop.wav": "5b41fbabf4a43e526face6922a74a1fa2d1930d0901e0df590989bf7d68cc97a",
}

# cue, original file, start/end seconds, high-pass Hz, desired RMS, fades ms
# Selected from the recording's waveform and harmonic content. No speech or
# synthetic imitation is mixed in, and no percussive/glass-crash effect is used.
CUES = [
    ("hungry", "cat_mewfood.wav", .04, 1.04, 80, .100, 12, 60),
    ("meal", "cat_mewpurr.wav", .02, .96, 70, .115, 12, 70),
    ("treat", "cat_mewpurr2.wav", .30, 1.57, 70, .120, 18, 90),
    ("refuse", "cat_softmew.wav", 1.30, 1.86, 80, .080, 15, 60),
    ("pet", "cat_purractive_loop.wav", .20, 1.60, 18, .105, 55, 110),
    ("jump", "cat_mewfood.wav", .53, .95, 85, .090, 12, 70),
    ("punch", "cat_softmew.wav", 1.32, 1.73, 85, .090, 12, 65),
    ("wake", "cat_purrsleepy_loop.wav", .32, 1.42, 18, .075, 60, 120),
]
SAMPLE_RATE = 22050
PEAK_LIMIT = .68


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def pcm_read(path: Path) -> tuple[int, list[float]]:
    with wave.open(str(path), "rb") as source:
        if (source.getnchannels(), source.getsampwidth(), source.getframerate(), source.getcomptype()) != (1, 2, 44100, "NONE"):
            raise ValueError("Unexpected source PCM format: " + path.name)
        if not 0 < source.getnframes() <= 500000:
            raise ValueError("Unexpected source duration: " + path.name)
        samples = array("h", source.readframes(source.getnframes()))
    if sys.byteorder != "little":
        samples.byteswap()
    return 44100, [sample / 32768.0 for sample in samples]


def prepare(samples: list[float], rate: int, start: float, end: float,
            highpass: float, wanted_rms: float, fade_in_ms: int, fade_out_ms: int) -> list[int]:
    cropped = samples[round(start * rate):round(end * rate)]
    if not cropped:
        raise ValueError("Empty cue crop")
    # A one-pole high-pass removes DC/handling rumble. Purr cues keep their
    # characteristic low pulse rate with a substantially lower cutoff.
    a = math.exp(-2 * math.pi * highpass / rate)
    previous_x = previous_y = 0.0
    filtered = []
    for sample in cropped:
        previous_y = a * (previous_y + sample - previous_x)
        previous_x = sample
        filtered.append(previous_y)
    # Windowed sinc anti-alias filter, compensated to avoid a shifted onset.
    radius = 24
    cutoff = 8500.0 / rate
    taps = []
    for offset in range(-radius, radius + 1):
        sinc = 2 * cutoff if offset == 0 else math.sin(2 * math.pi * cutoff * offset) / (math.pi * offset)
        window = .5 + .5 * math.cos(math.pi * offset / radius)
        taps.append(sinc * window)
    total = sum(taps)
    taps = [tap / total for tap in taps]
    padded = [filtered[0]] * radius + filtered + [filtered[-1]] * radius
    result = [sum(padded[index + tap] * weight for tap, weight in enumerate(taps))
              for index in range(0, len(filtered), 2)]
    fade_in = max(2, round(fade_in_ms * SAMPLE_RATE / 1000))
    fade_out = max(2, round(fade_out_ms * SAMPLE_RATE / 1000))
    for index in range(min(fade_in, len(result))):
        result[index] *= .5 - .5 * math.cos(math.pi * index / (fade_in - 1))
    for index in range(min(fade_out, len(result))):
        result[-1-index] *= .5 - .5 * math.cos(math.pi * index / (fade_out - 1))
    peak = max(map(abs, result))
    rms = math.sqrt(sum(sample * sample for sample in result) / len(result))
    gain = min(wanted_rms / max(rms, 1e-9), PEAK_LIMIT / max(peak, 1e-9))
    return [round(max(-PEAK_LIMIT, min(PEAK_LIMIT, sample * gain)) * 32767) for sample in result]


def write_pcm(path: Path, samples: list[int]) -> bytes:
    pcm = array("h", samples)
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        output.writeframes(pcm.tobytes())
    return path.read_bytes()


def verify(output: Path) -> dict:
    manifest = json.loads((output / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("schema") == 3:
        from prepare_reference_audio import verify as verify_reference_audio
        return verify_reference_audio(output)
    if manifest.get("schema") == 2:
        # v1.7 combines user-provided clips and three retained CC0 clips.
        # Do not apply the historical all-CC0 provenance assertion to that set.
        from prepare_user_audio import verify as verify_user_audio
        return verify_user_audio(output)
    if manifest.get("source_page") != SOURCE_PAGE or manifest.get("license") != "CC0 1.0":
        raise ValueError("Unexpected audio provenance")
    if {item["cue"] for item in manifest["cues"]} != {cue[0] for cue in CUES}:
        raise ValueError("Unexpected cue inventory")
    total_bytes = 0
    for cue in manifest["cues"]:
        path = output / (cue["cue"] + ".wav")
        payload = path.read_bytes()
        if len(payload) != cue["bytes"] or digest(payload) != cue["sha256"]:
            raise ValueError("Changed cue: " + path.name)
        with wave.open(str(path), "rb") as audio:
            if (audio.getnchannels(), audio.getsampwidth(), audio.getframerate(), audio.getcomptype()) != (1, 2, SAMPLE_RATE, "NONE"):
                raise ValueError("Unexpected output PCM format")
            pcm = array("h", audio.readframes(audio.getnframes()))
            if sys.byteorder != "little":
                pcm.byteswap()
            if pcm[0] != 0 or pcm[-1] != 0 or max(map(abs, pcm)) > round(PEAK_LIMIT * 32767):
                raise ValueError("Unfaded or clipped cue: " + path.name)
            if not .25 <= len(pcm) / SAMPLE_RATE <= 1.6:
                raise ValueError("Cue duration outside the quiet-desktop range")
        total_bytes += len(payload)
    if total_bytes >= 500000:
        raise ValueError("Audio bundle is unexpectedly large")
    return {"cues": len(manifest["cues"]), "wav_bytes": total_bytes,
            "pcm": "mono, signed16-bit little-endian,22050Hz", "verified": True}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=ROOT / "assets/audio_candidates")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "assets/audio")
    parser.add_argument("--download", action="store_true", help="Download any missing pinned CC0 source WAVs")
    parser.add_argument("--verify", action="store_true", help="Verify already prepared WAVs; never downloads")
    args = parser.parse_args()
    if args.verify:
        print(json.dumps(verify(args.output_dir), indent=2))
        return 0
    existing_manifest = args.output_dir / "manifest.json"
    if existing_manifest.exists() and json.loads(existing_manifest.read_text(encoding="utf-8")).get("schema") in (2, 3):
        raise ValueError("This folder contains user-provided audio. Use its matching preparation tool, "
                         "or choose a separate --output-dir to reproduce the historical CC0 set.")
    args.source_dir.mkdir(parents=True, exist_ok=True)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    sources = {}
    for name, expected in SOURCE_HASHES.items():
        source = args.source_dir / name
        if not source.exists() and args.download:
            with urllib.request.urlopen(DOWNLOAD_BASE + name, timeout=25) as response:
                data = response.read(2 * 1024 * 1024)
            if digest(data) != expected:
                raise ValueError("Downloaded source does not match its pinned SHA256: " + name)
            source.write_bytes(data)
        if not source.exists():
            raise ValueError("Missing source " + name + "; provide --source-dir or use --download")
        if digest(source.read_bytes()) != expected:
            raise ValueError("Source SHA256 mismatch: " + name)
        sources[name] = pcm_read(source)
    entries = []
    for cue, name, start, end, highpass, rms, fade_in, fade_out in CUES:
        rate, samples = sources[name]
        result = prepare(samples, rate, start, end, highpass, rms, fade_in, fade_out)
        payload = write_pcm(args.output_dir / (cue + ".wav"), result)
        entries.append({"cue": cue, "source": name, "source_sha256": SOURCE_HASHES[name],
                        "source_url": DOWNLOAD_BASE + name, "crop_seconds": [start, end],
                        "highpass_hz": highpass, "fade_ms": [fade_in, fade_out],
                        "duration_seconds": len(result) / SAMPLE_RATE,
                        "peak": max(map(abs, result)) / 32768,
                        "rms": math.sqrt(sum(x*x for x in result) / len(result)) / 32768,
                        "bytes": len(payload), "sha256": digest(payload)})
    manifest = {"schema": 1, "source_page": SOURCE_PAGE, "creator": "Kerzoven",
                "license": "CC0 1.0", "license_url": LICENSE_URL,
                "source_publication_date": "2022-04-16", "sample_rate": SAMPLE_RATE,
                "channels": 1, "pcm_bits": 16, "peak_limit": PEAK_LIMIT,
                "processing": "Trim; DC/rumble high-pass; sinc anti-alias downsample2:1; cosine fades; bounded RMS gain. No pitch shift or synthetic sound added.",
                "cues": entries}
    (args.output_dir / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(verify(args.output_dir), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

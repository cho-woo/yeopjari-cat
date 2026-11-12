#!/usr/bin/env python3
"""Encode actual GDI+ jump fixture PNGs without changing the actor's size.

The left side uses the old16 source drawings on the *current* fixture's physical
path. It is an artwork comparison, not footage of a prior app release. The right
side is the current production connected-jump renderer. Neither is a desktop
screen recording. Native inputs are emitted by NativeManualMotionRenderTest.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile

from PIL import Image, ImageDraw, ImageFont


def encode(source: Path, output: Path) -> None:
    report = json.loads((source / "report.json").read_text(encoding="utf-8"))
    if report.get("test") != "jump-continuity-native" or report.get("status") != "passed":
        raise ValueError("A passed real Windows jump fixture report is required")
    if report.get("comparison_baseline") is False:
        raise ValueError("This authored-video review has no historical comparison baseline; do not label its current frames as old artwork.")
    trace = json.loads((source / "trace.json").read_text(encoding="utf-8"))
    if trace.get("fps") != 60 or report.get("frames_per_case") != 420:
        raise ValueError("Unexpected native clock/frame contract")
    if (report.get("schema") != 1 or report.get("render_source") != "Windows GDI+ / Box2D" or
            report.get("desktop_recording") is not False or len(trace.get("frames", [])) != 3360):
        raise ValueError("Actual native renderer provenance and all eight physical cases are required")
    for row in trace["frames"]:
        if row.get("jump_rig") and (row["sprite_resource"], row["sprite_frame"]) != (400, 29):
            raise ValueError("Trace does not identify the actual connected painted source")
    output.parent.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256()
    font_path = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
    font = ImageFont.truetype(str(font_path), 14) if font_path.exists() else ImageFont.load_default()
    with tempfile.TemporaryDirectory(prefix="yeopjari-jump-") as temporary:
        frame_dir = Path(temporary)
        for number, native_frame in enumerate(range(0, 420, 2)):
            canvas = Image.new("RGB", (840, 740), (243, 241, 237))
            draw = ImageDraw.Draw(canvas)
            draw.text((14, 10), "Old16 drawings / current physical path", fill=(60, 70, 80), font=font)
            draw.text((434, 10), "Connected same painting / current renderer", fill=(60, 70, 80), font=font)
            for case, top, label in [(0, 40, "Cream"), (4, 370, "Tuxedo")]:
                for column, prefix in [(0, "before-"), (1, "")]:
                    path = source / f"{prefix}case-{case}-{native_frame}.png"
                    data = path.read_bytes()
                    digest.update(path.name.encode("ascii") + b"\0" + hashlib.sha256(data).digest())
                    image = Image.open(path).convert("RGBA")
                    if image.size != (420, 320):
                        raise ValueError(f"Unexpected actual display canvas: {path}")
                    canvas.paste(image, (column * 420, top), image)
                    draw.line((column * 420, top + 280, column * 420 + 419, top + 280), fill=(175, 181, 185))
                draw.text((14, top + 12), label, fill=(60, 70, 80), font=font)
            draw.text((14, 700), "Windows GDI+ / Box2D diagnostic render; not a desktop recording",
                      fill=(60, 70, 80), font=font)
            draw.text((14, 720), "110 logical px; video30FPS; Box2D60Hz; same current physical scenario",
                      fill=(60, 70, 80), font=font)
            canvas.save(frame_dir / f"{number:04}.png")
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-fflags", "+bitexact", "-framerate", "30", "-i",
                        str(frame_dir / "%04d.png"), "-frames:v", "210", "-an", "-map_metadata", "-1", "-map_chapters", "-1",
                        "-c:v", "libx264", "-threads:v", "1", "-flags:v", "+bitexact", "-crf", "18", "-pix_fmt", "yuv420p",
                        "-movflags", "+faststart", str(output)], check=True)
    subprocess.run(["ffmpeg", "-v", "error", "-i", str(output), "-f", "null", "-"], check=True)
    probe = json.loads(subprocess.check_output(["ffprobe", "-v", "error", "-select_streams", "v:0",
        "-show_entries", "stream=width,height,avg_frame_rate,nb_frames:format=duration", "-of", "json", str(output)], text=True))
    stream = probe["streams"][0]
    numerator, denominator = map(int, stream["avg_frame_rate"].split("/"))
    if ((stream["width"], stream["height"]) != (840, 740) or int(stream["nb_frames"]) != 210 or
            numerator != 30*denominator or abs(float(probe["format"]["duration"])-7) >= .002):
        raise ValueError("Jump review encoder changed the native size, sampling cadence or duration")
    # Stage-only and promotion run in different temporary directories. Paths,
    # wall clocks and workflow invocation IDs must never alter immutable assets.
    summary = {"schema": 1, "native_source": "jump-continuity", "output": output.name, "duration_seconds": 7,
               "fps": 30, "frame_count": 210, "canvas": [840, 720], "physical_hz": 60,
               "actor_logical_height": 110, "desktop_recording": False,
               "comparison": "old16 source artwork versus connected renderer on the same current native physical path",
               "rig_source": [400, 29], "new_painted_frames": 0, "interpolated_frames": 0, "retimed": False,
               "native_samples_sha256": digest.hexdigest(),
               "native_report_sha256": hashlib.sha256((source / "report.json").read_bytes()).hexdigest(),
               "native_trace_sha256": hashlib.sha256((source / "trace.json").read_bytes()).hexdigest(),
               "video_sha256": hashlib.sha256(output.read_bytes()).hexdigest()}
    output.with_suffix(".json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--frames", required=True, type=Path, help="Native jump-continuity artifact directory")
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    encode(args.frames, args.output)

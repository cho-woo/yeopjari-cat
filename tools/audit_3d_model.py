"""Audit a generated 3D preview asset through the real C++ runtime.

Uses only Python's standard library plus a C++17 compiler. The sidecar supplies
bone names and decorative vertex ranges; they are validated against the asset
by the C++ auditor. It does not change model bytes or pet preferences.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-dir", type=Path, default=ROOT / "build/3d-preview-model")
    parser.add_argument("--output", type=Path, default=ROOT / "build/3d-preview-runtime-audit.json")
    parser.add_argument("--auditor", type=Path, help="Use an already built audit_3d_model executable")
    args = parser.parse_args()
    model_dir, output = args.model_dir.resolve(), args.output.resolve()
    model = model_dir / "cat3d.bin"
    metadata_file = model_dir / "model-report.json"
    if model.stat().st_size > 128 * 1024 * 1024 or metadata_file.stat().st_size > 1024 * 1024:
        raise ValueError("Model or sidecar exceeds the audit size limit")
    metadata_bytes = metadata_file.read_bytes()
    metadata = json.loads(metadata_bytes.decode("utf-8"))
    model_hash = hashlib.sha256(model.read_bytes()).hexdigest()
    metadata_hash = hashlib.sha256(metadata_bytes).hexdigest()
    names = metadata["bone_names"]
    objects = metadata["objects"]
    if len(names) != metadata["bones"] or len(names) != len(set(names)):
        raise ValueError("Model sidecar bone names are invalid")
    if metadata["bytes"] != model.stat().st_size:
        raise ValueError("Model sidecar byte count differs from actual model")
    previous_end = 0
    for obj in objects:
        if obj["first_vertex"] != previous_end or obj["vertex_count"] < 1:
            raise ValueError("Model sidecar object ranges must partition vertices in order")
        if not isinstance(obj["structural_contact"], bool):
            raise ValueError("Model sidecar contact flags must be boolean")
        previous_end += obj["vertex_count"]
    if previous_end != metadata["vertices"]:
        raise ValueError("Model sidecar object ranges do not cover the model")
    if not any(obj["structural_contact"] for obj in objects):
        raise ValueError("Model sidecar excludes every structural object")
    output.parent.mkdir(parents=True, exist_ok=True)
    auditor = args.auditor.resolve() if args.auditor else ROOT / "build/audit_3d_model"
    if not args.auditor:
        auditor.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tools/audit_3d_model.cpp"), "-o", str(auditor)], check=True)
    command = [str(auditor), str(model), str(output), "--head-bone", str(names.index("head")),
               "--require-belly-head-contact", "--require-belly-body-contact"]
    for obj in objects:
        if not obj["structural_contact"]:
            command.extend(["--skip", str(obj["first_vertex"]), str(obj["vertex_count"])])
    groups = {"torso": ["pelvis", "spine", "chest"], "head": ["head"],
              "tail": ["tail.0", "tail.1", "tail.2", "tail.3"],
              "front_left": ["front_paw.L"], "front_right": ["front_paw.R"],
              "back_left": ["back_paw.L"], "back_right": ["back_paw.R"]}
    for group, bones in groups.items():
        command.extend(["--group", group, ",".join(str(names.index(bone)) for bone in bones)])
    # Remove an old report so a failed loader cannot leave stale passing proof.
    output.unlink(missing_ok=True)
    result = subprocess.run(command, check=False)
    if output.exists():
        report = json.loads(output.read_text(encoding="utf-8"))
        if (report["vertices"], report["bones"]) != (metadata["vertices"], metadata["bones"]):
            raise ValueError("Loaded asset counts disagree with sidecar")
        report["asset_sha256"] = model_hash
        report["sidecar_sha256"] = metadata_hash
        report["asset_bytes"] = model.stat().st_size
        changed = (hashlib.sha256(model.read_bytes()).hexdigest() != model_hash or
                   hashlib.sha256(metadata_file.read_bytes()).hexdigest() != metadata_hash)
        report["input_changed_during_audit"] = changed
        if changed:
            report["passed"] = False
        output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({"report": str(output), "passed": report["passed"],
                          "asset_sha256": report["asset_sha256"]}))
        if changed:
            raise RuntimeError("Asset or sidecar changed while the runtime audit was running")
    return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())

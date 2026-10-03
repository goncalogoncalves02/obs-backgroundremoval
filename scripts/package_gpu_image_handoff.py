# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
"""Render one owner ZIP after the original plugin has passed its origin verifier.

The installer pins plugin bytes and generic measurement bytes. The outer ZIP and
installer hashes belong to the final external download receipt: embedding them
in their own content would be circular. No network/provider acquisition.
"""
import argparse
import base64
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import stat
import subprocess
import zipfile

def digest(data):
    return hashlib.sha256(data).hexdigest()

def safe_file(path):
    path = Path(path)
    if any(parent.is_symlink() for parent in (path, *path.parents)):
        raise ValueError("Symlink input is prohibited")
    if not path.is_file() or not path.stat().st_size:
        raise ValueError("Required nonempty input file missing")
    return path

def safe_relative(name):
    parts = name.split("/")
    if not name or "\\" in name or ":" in name or name.startswith("/") or any(part in ("", ".", "..") or part.endswith((" ", ".")) for part in parts):
        raise ValueError("Unsafe ZIP path")

def inventory(plugin_zip):
    files = []
    seen = set()
    with zipfile.ZipFile(safe_file(plugin_zip)) as archive:
        for entry in archive.infolist():
            name = entry.filename.rstrip("/") if entry.is_dir() else entry.filename
            safe_relative(name)
            if name != "obs-backgroundremoval" and not name.startswith("obs-backgroundremoval/"):
                raise ValueError("Entry outside plugin root")
            key = name.casefold()
            if key in seen:
                raise ValueError("Duplicate or case-conflicting ZIP entry")
            seen.add(key)
            mode = (entry.external_attr >> 16) & 0o170000
            if mode == stat.S_IFLNK or entry.external_attr & 0x400:
                raise ValueError("Link/reparse ZIP entry")
            if entry.is_dir():
                continue
            if not entry.file_size:
                raise ValueError("Empty package file")
            data = archive.read(entry)
            files.append({"Path": name.removeprefix("obs-backgroundremoval/"), "Sha256": digest(data), "Length":len(data)})
    required = {
        "bin/64bit/obs-backgroundremoval.dll", "bin/64bit/onnxruntime.dll",
        "bin/64bit/Microsoft.Windows.AI.MachineLearning.dll", "bin/64bit/DirectML.dll",
        "data/effects/input_downscale.effect", "data/effects/gpu_mask_processing.effect",
        "data/models/mediapipe.onnx",
    }
    actual = {file["Path"] for file in files}
    if not required.issubset(actual) or sum(PurePosixPath(path).name.casefold() == "onnxruntime.dll" for path in actual) != 1:
        raise ValueError("Required plugin/effect/model/runtime inventory missing or duplicate ORT")
    return sorted(files, key=lambda file: file["Path"])

def package(plugin_zip, template, measurement, source_sha, run_id, artifact_name, destination):
    plugin_zip = safe_file(plugin_zip); template = safe_file(template); measurement = safe_file(measurement)
    destination = Path(destination)
    if destination.exists():
        raise ValueError("Destination exists; owner bytes are not overwritten")
    if not re.fullmatch("[0-9a-f]{40}",source_sha) or not re.fullmatch("[0-9]+",run_id) or not artifact_name:
        raise ValueError("Immutable source SHA, actual run ID and artifact name required")
    before = {path: digest(path.read_bytes()) for path in (plugin_zip,template,measurement)}
    files = inventory(plugin_zip)
    manifest = {"SchemaVersion":1,"SourceSha":source_sha,"RunId":run_id,"ArtifactName":artifact_name,
                "WindowsMlVersion":"2.2.12","ZipSha256":before[plugin_zip],"MeasurementSha256":before[measurement],"Files":files}
    encoded = base64.b64encode(json.dumps(manifest,sort_keys=True,separators=(",",":")).encode("utf-8")).decode("ascii")
    code = template.read_text(encoding="utf-8-sig")
    if code.count("@@MANIFEST_BASE64@@") != 1:
        raise ValueError("Exactly one template manifest marker required")
    rendered = code.replace("@@MANIFEST_BASE64@@",encoded).encode("utf-8-sig")
    measurement_bytes = measurement.read_bytes()
    if any(byte >= 128 for byte in measurement_bytes) and not measurement_bytes.startswith(b"\xef\xbb\xbf"):
        raise ValueError("PowerShell 5.1 needs UTF-8 BOM for non-ASCII measurement code")
    destination.parent.mkdir(parents=True,exist_ok=True)
    try:
        with zipfile.ZipFile(destination,"x",compression=zipfile.ZIP_STORED) as archive:
            archive.write(plugin_zip,plugin_zip.name)
            archive.writestr("instalar-processamento-gpu.ps1",rendered)
            archive.writestr("testar-processamento-gpu.ps1",measurement_bytes)
        with zipfile.ZipFile(destination) as archive:
            if digest(archive.read(plugin_zip.name)) != before[plugin_zip]:
                raise ValueError("Inner plugin ZIP changed while packaging")
            if archive.read("testar-processamento-gpu.ps1") != measurement_bytes:
                raise ValueError("Measurement changed while packaging")
        if any(digest(path.read_bytes()) != hash_value for path,hash_value in before.items()):
            raise ValueError("Source files changed while packaging")
    except Exception:
        destination.unlink(missing_ok=True)
        raise
    return manifest

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plugin-zip",type=Path,required=True)
    parser.add_argument("--template",type=Path,required=True)
    parser.add_argument("--measurement",type=Path,required=True)
    parser.add_argument("--source-sha",required=True)
    parser.add_argument("--run-id",required=True)
    parser.add_argument("--artifact-name",required=True)
    parser.add_argument("--destination",type=Path,required=True)
    parser.add_argument("--source-root",type=Path,required=True)
    args=parser.parse_args()
    actual=subprocess.check_output(["git","-C",str(args.source_root),"rev-parse","HEAD"],text=True).strip()
    if actual != args.source_sha:
        raise ValueError("Checkout differs from immutable SOURCE_COMMIT")
    for path in (args.template,args.measurement,Path(__file__)):
        relative=path.resolve().relative_to(args.source_root.resolve()).as_posix()
        blob=subprocess.check_output(["git","-C",str(args.source_root),"show",args.source_sha+":"+relative])
        if blob != path.read_bytes():
            raise ValueError("Tracked script differs from immutable source")
    package(args.plugin_zip,args.template,args.measurement,args.source_sha,args.run_id,args.artifact_name,args.destination)
    print("source_commit="+args.source_sha)
    print("handoff_sha256="+digest(args.destination.read_bytes()))
    print("status=ok")

if __name__ == "__main__":
    main()

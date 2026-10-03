# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: GPL-3.0-or-later
"""Actual ZIP fixture; generic generator provenance is supplied by CI, not invented."""
import base64
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "scripts/package_gpu_image_handoff.py"
PLUGIN = Path(os.environ["GPU_FIXTURE_ZIP"])

class HandoffTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not GENERATOR.is_file():
            raise AssertionError("Task6 expected RED: generic handoff generator absent")
        spec = importlib.util.spec_from_file_location("handoff", GENERATOR)
        cls.module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.module)

    def test_real_zip_byte_identity_and_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            dest = Path(temporary) / "handoff.zip"
            self.module.package(PLUGIN, ROOT / "scripts/instalar-processamento-gpu.ps1.in",
                                ROOT / "scripts/testar-processamento-gpu.ps1", "a"*40, "123", "fixture-plugin", dest)
            with zipfile.ZipFile(dest) as archive:
                self.assertEqual(sorted(archive.namelist()), sorted([PLUGIN.name, "instalar-processamento-gpu.ps1", "testar-processamento-gpu.ps1"]))
                self.assertEqual(hashlib.sha256(archive.read(PLUGIN.name)).digest(), hashlib.sha256(PLUGIN.read_bytes()).digest())
                installer = archive.read("instalar-processamento-gpu.ps1").decode("utf-8-sig")
                line = next(line for line in installer.splitlines() if line.startswith("# HANDOFF_MANIFEST:"))
                manifest = json.loads(base64.b64decode(line.split(":",1)[1]))
                self.assertEqual(manifest["SourceSha"], "a"*40)
                self.assertEqual(manifest["MeasurementSha256"], hashlib.sha256(archive.read("testar-processamento-gpu.ps1")).hexdigest())
                self.assertEqual(manifest["ZipSha256"], hashlib.sha256(PLUGIN.read_bytes()).hexdigest())
                self.assertGreater(len(manifest["Files"]), 40)
                self.assertNotIn("HandoffSha256", manifest)
                self.assertNotIn("@@MANIFEST_BASE64@@", installer)

    def test_destination_not_overwritten(self):
        with tempfile.TemporaryDirectory() as temporary:
            dest = Path(temporary)/"existing.zip"; dest.write_bytes(b"owner bytes")
            with self.assertRaises(ValueError):
                self.module.package(PLUGIN, ROOT / "scripts/instalar-processamento-gpu.ps1.in",
                                    ROOT / "scripts/testar-processamento-gpu.ps1", "a"*40, "123", "fixture", dest)
            self.assertEqual(dest.read_bytes(),b"owner bytes")

    def test_source_id_must_be_full_immutable_sha(self):
        with tempfile.TemporaryDirectory() as temporary:
            with self.assertRaises(ValueError):
                self.module.package(PLUGIN, ROOT / "scripts/instalar-processamento-gpu.ps1.in",
                                    ROOT / "scripts/testar-processamento-gpu.ps1", "main", "123", "fixture", Path(temporary)/"new.zip")

    def test_reject_archive_traversal_duplicate_and_symlink(self):
        for names in (["obs-backgroundremoval/../bad"], ["obs-backgroundremoval/a","obs-backgroundremoval/A"], ["/absolute"]):
            with tempfile.TemporaryDirectory() as temporary:
                source=Path(temporary)/"bad.zip"
                with zipfile.ZipFile(source,"w") as archive:
                    for name in names: archive.writestr(name,b"bad")
                with self.assertRaises(ValueError): self.module.inventory(source)
        with tempfile.TemporaryDirectory() as temporary:
            source=Path(temporary)/"link.zip"
            with zipfile.ZipFile(source,"w") as archive:
                info=zipfile.ZipInfo("obs-backgroundremoval/link"); info.external_attr=0o120777 << 16
                archive.writestr(info,b"target")
            with self.assertRaises(ValueError): self.module.inventory(source)

if __name__ == "__main__":
    unittest.main(verbosity=2)

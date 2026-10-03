# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DLLS = ("Microsoft.Windows.AI.MachineLearning.dll", "onnxruntime.dll", "DirectML.dll")


@unittest.skipUnless(sys.platform == "win32", "PowerShell artifact fixture execution requires Windows")
class SessionArtifactTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pwsh = shutil.which("pwsh")
        if not cls.pwsh:
            raise AssertionError("pwsh is required on Windows; packaging checks cannot be skipped")

    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.build = self.root / "build"
        self.package = self.root / "package"
        self.build.mkdir()
        native = self.package / "runtimes/win-x64/native"
        native.mkdir(parents=True)
        for name in DLLS:
            (native / name).write_bytes(f"fixture package {name}".encode())
            shutil.copy2(native / name, self.build / name)
        for name in ("license.txt", "ThirdPartyNotices.txt"):
            (self.package / name).write_text(f"fixture legal {name}")
        config = self.package / "build/cmake"
        config.mkdir(parents=True)
        (config / "microsoft.windows.ai.machinelearning-config.cmake").write_text(
            'set(WINML_VERSION "2.2.12")\n')
        self.repo = self.root / "repo"
        (self.repo / "scripts").mkdir(parents=True)
        self.script = self.repo / "scripts/package_windows_ml_session_check.ps1"
        shutil.copy2(ROOT / "scripts/package_windows_ml_session_check.ps1", self.script)
        self.model = self.repo / "data/models/mediapipe.onnx"
        self.model.parent.mkdir(parents=True)
        self.model.write_bytes(b"fixture tracked model")
        (self.build / "windows-ml-session-check.exe").write_bytes(b"fixture executable")
        for arguments in (["init", "--quiet"], ["add", "data/models/mediapipe.onnx"],
                          ["-c", "user.name=Fixture", "-c", "user.email=fixture@example.invalid",
                           "-c", "commit.gpgsign=false", "commit", "--quiet", "-m", "fixture"]):
            subprocess.run(["git", "-C", str(self.repo), *arguments], check=True, capture_output=True)
        self.source = subprocess.check_output(["git", "-C", str(self.repo), "rev-parse", "HEAD"], text=True).strip()
        self.destination = self.root / "windows-ml-session-check_2.2.12_x64.zip"

    def package_artifact(self, source=None):
        return subprocess.run([self.pwsh, "-NoProfile", "-File", str(self.script),
                               "-BuildDirectory", str(self.build), "-ModelPath", str(self.model),
                               "-WindowsMlRoot", str(self.package), "-SourceCommit", source or self.source,
                               "-DestinationZip", str(self.destination)],
                              capture_output=True, text=True, check=False, timeout=60)

    def test_packages_only_validated_origins_and_rechecks_hashes_in_zip(self):
        result = self.package_artifact()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.destination) as archive:
            expected = {"windows-ml-session-check.exe", "mediapipe.onnx", *DLLS,
                        "windows-ml-license.txt", "windows-ml-third-party-notices.txt", "manifest.json"}
            self.assertEqual(set(archive.namelist()), expected)
            self.assertEqual(len(archive.namelist()), len(expected))
            manifest = json.loads(archive.read("manifest.json"))
            self.assertEqual(manifest["source_commit"], self.source)
            self.assertEqual(manifest["windows_ml_package_version"], "2.2.12")
            self.assertEqual({entry["path"] for entry in manifest["files"]}, expected - {"manifest.json"})
            for entry in manifest["files"]:
                self.assertEqual(hashlib.sha256(archive.read(entry["path"])).hexdigest(), entry["sha256"])
                self.assertTrue(entry["origin"])
            for name in DLLS:
                self.assertEqual(archive.read(name), (self.package / "runtimes/win-x64/native" / name).read_bytes())
            self.assertEqual(archive.read("mediapipe.onnx"), self.model.read_bytes())
            self.assertEqual(archive.read("windows-ml-license.txt"), (self.package / "license.txt").read_bytes())
            self.assertEqual(archive.read("windows-ml-third-party-notices.txt"),
                             (self.package / "ThirdPartyNotices.txt").read_bytes())

    def test_rejects_missing_runtime_and_replaced_directml(self):
        for name in DLLS:
            original = (self.build / name).read_bytes()
            for content in (None, b"", b"replacement"):
                with self.subTest(name=name, content=content):
                    (self.build / name).unlink(missing_ok=True)
                    if content is not None:
                        (self.build / name).write_bytes(content)
                    result = self.package_artifact()
                    self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertFalse(self.destination.exists())
            (self.build / name).write_bytes(original)

    def test_rejects_stale_destination_without_overwriting_it(self):
        self.destination.write_bytes(b"stale artifact")
        result = self.package_artifact()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.destination.read_bytes(), b"stale artifact")

    def test_rejects_invalid_or_mismatched_source_sha(self):
        for source in ("HEAD", "0" * 40, self.source[:7], "../../unsafe"):
            with self.subTest(source=source):
                result = self.package_artifact(source)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertFalse(self.destination.exists())

    def test_rejects_untracked_model_bytes_and_wrong_package_version(self):
        self.model.write_bytes(b"replacement model")
        result = self.package_artifact()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.destination.exists())
        self.model.write_bytes(b"fixture tracked model")
        version = self.package / "build/cmake/microsoft.windows.ai.machinelearning-config.cmake"
        version.write_text('set(WINML_VERSION "9.9.9")\n')
        result = self.package_artifact()
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()

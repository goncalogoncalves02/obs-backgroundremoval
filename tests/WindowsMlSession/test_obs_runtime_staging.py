# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
OBS_DLLS = (
    "avcodec-62.dll", "avformat-62.dll", "avutil-60.dll", "librist.dll",
    "libx264-164.dll", "srt.dll", "swresample-6.dll", "swscale-9.dll", "zlib.dll",
)
WINDOWS_ML_DLLS = ("Microsoft.Windows.AI.MachineLearning.dll", "onnxruntime.dll", "DirectML.dll")
EXISTING_DLLS = ("obs.dll", "w32-pthreads.dll", *WINDOWS_ML_DLLS)


@unittest.skipUnless(sys.platform == "win32", "PowerShell staging fixtures require Windows")
class ObsRuntimeStagingTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pwsh = shutil.which("pwsh")
        if not cls.pwsh:
            raise AssertionError("pwsh is required on Windows; staging checks cannot be skipped")

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="obs staging [fixture] ")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.prefix = self.root / "obs deps"
        self.source_bin = self.prefix / "bin"
        self.source_bin.mkdir(parents=True)
        self.adapter = self.root / "adapter"
        self.adapter.mkdir()
        self.package = self.root / "windows ml"
        self.native = self.package / "runtimes/win-x64/native"
        self.native.mkdir(parents=True)
        for name in OBS_DLLS:
            (self.source_bin / name).write_bytes(f"supplied OBS dependency {name}".encode())
        for name in WINDOWS_ML_DLLS:
            content = f"pinned Windows ML {name}".encode()
            (self.native / name).write_bytes(content)
            (self.adapter / name).write_bytes(content)
        for name in ("obs.dll", "w32-pthreads.dll", "windows-ml-plugin-session.exe"):
            (self.adapter / name).write_bytes(f"actual target fixture {name}".encode())
        for name in ("datachannel.dll", "lua51.dll", "avdevice-62.dll", "libcurl.dll", "avfilter-11.dll",
                     "onnxruntime.dll", "DirectML.dll"):
            (self.source_bin / name).write_bytes(b"unrelated OBS dependency bytes")
        self.original = self.snapshot()

    def snapshot(self):
        return {p.name: p.read_bytes() for p in self.adapter.iterdir() if p.is_file()}

    def stage(self, prefix=None, adapter=None):
        return subprocess.run([
            self.pwsh, "-NoProfile", "-NonInteractive", "-File",
            str(ROOT / "tests/WindowsMlSession/stage-obs-runtime.ps1"),
            "-ObsDepsPrefix", str(prefix or self.prefix),
            "-AdapterDirectory", str(adapter or self.adapter),
            "-WindowsMlRoot", str(self.package),
        ], capture_output=True, text=True, check=False, timeout=30)

    def assert_preflight_failure(self, **arguments):
        before = self.snapshot()
        result = self.stage(**arguments)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.snapshot(), before, "preflight failure must not deploy any DLL")

    def make_symlink(self, link, target, directory=False):
        try:
            link.symlink_to(target, target_is_directory=directory)
        except OSError as error:
            self.skipTest(f"Symbolic links unavailable on this filesystem: {error}")

    def test_copies_only_nine_dependencies_preserves_origins_and_is_idempotent(self):
        for invocation in range(2):
            with self.subTest(invocation=invocation):
                result = self.stage()
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                expected = dict(self.original)
                for name in OBS_DLLS:
                    content = (self.source_bin / name).read_bytes()
                    expected[name] = content
                self.assertEqual(self.snapshot(), expected)
                for name in OBS_DLLS:
                    digest = hashlib.sha256(expected[name]).hexdigest()
                    self.assertIn(f"{name} sha256={digest}", result.stdout)

    def test_missing_or_empty_last_source_fails_before_any_copy(self):
        source = self.source_bin / "zlib.dll"
        source.unlink()
        self.assert_preflight_failure()
        source.write_bytes(b"")
        self.assert_preflight_failure()

    def test_invalid_prefix_or_missing_bin_fails_before_any_copy(self):
        self.assert_preflight_failure(prefix=self.root / "absent")
        empty_prefix = self.root / "empty prefix"
        empty_prefix.mkdir()
        self.assert_preflight_failure(prefix=empty_prefix)

    def test_different_case_destination_collision_fails_without_overwrite(self):
        (self.adapter / "ZLIB.DLL").write_bytes(b"different existing deployment")
        self.assert_preflight_failure()

    def test_matching_different_case_destination_is_reused_without_duplicate(self):
        (self.adapter / "ZLIB.DLL").write_bytes((self.source_bin / "zlib.dll").read_bytes())
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(len(list(self.adapter.iterdir())), len(self.original) + len(OBS_DLLS))
        self.assertEqual((self.adapter / "ZLIB.DLL").read_bytes(), (self.source_bin / "zlib.dll").read_bytes())

    def test_missing_empty_or_replaced_windows_ml_origin_fails_before_copy(self):
        for name in WINDOWS_ML_DLLS:
            path = self.adapter / name
            original = path.read_bytes()
            for content in (None, b"", b"replacement"):
                with self.subTest(name=name, content=content):
                    path.unlink(missing_ok=True)
                    if content is not None:
                        path.write_bytes(content)
                    self.assert_preflight_failure()
            path.write_bytes(original)

    def test_missing_package_origin_fails_before_copy(self):
        (self.native / "DirectML.dll").unlink()
        self.assert_preflight_failure()

    def test_missing_executable_or_existing_obs_runtime_fails_before_copy(self):
        for name in ("windows-ml-plugin-session.exe", "obs.dll", "w32-pthreads.dll"):
            with self.subTest(name=name):
                path = self.adapter / name
                content = path.read_bytes()
                path.unlink()
                self.assert_preflight_failure()
                path.write_bytes(content)

    def test_source_directory_in_place_of_dll_fails_before_copy(self):
        path = self.source_bin / "zlib.dll"
        path.unlink()
        path.mkdir()
        self.assert_preflight_failure()

    def test_destination_directory_in_place_of_dll_fails_before_copy(self):
        (self.adapter / "zlib.dll").mkdir()
        self.assert_preflight_failure()

    def test_source_file_redirection_outside_bin_fails_before_copy(self):
        path = self.source_bin / "zlib.dll"
        outside = self.root / "outside.dll"
        path.rename(outside)
        self.make_symlink(path, outside)
        self.assert_preflight_failure()

    def test_source_bin_directory_redirection_fails_before_copy(self):
        outside = self.root / "outside bin"
        self.source_bin.rename(outside)
        self.make_symlink(self.source_bin, outside, directory=True)
        self.assert_preflight_failure()

    def test_destination_symlink_fails_without_changing_target(self):
        outside = self.root / "outside.dll"
        content = (self.source_bin / "zlib.dll").read_bytes()
        outside.write_bytes(content)
        self.make_symlink(self.adapter / "zlib.dll", outside)
        self.assert_preflight_failure()
        self.assertEqual(outside.read_bytes(), content)

    def test_destination_hardlink_is_rejected_even_with_matching_bytes(self):
        outside = self.root / "outside.dll"
        content = (self.source_bin / "zlib.dll").read_bytes()
        outside.write_bytes(content)
        os.link(outside, self.adapter / "zlib.dll")
        self.assert_preflight_failure()
        self.assertEqual(outside.read_bytes(), content)

    def test_adapter_directory_redirection_fails_before_copy(self):
        alias = self.root / "adapter alias"
        self.make_symlink(alias, self.adapter, directory=True)
        self.assert_preflight_failure(adapter=alias)

    def test_package_origin_redirection_fails_before_copy(self):
        path = self.native / "DirectML.dll"
        outside = self.root / "outside.dll"
        path.rename(outside)
        self.make_symlink(path, outside)
        self.assert_preflight_failure()


if __name__ == "__main__":
    unittest.main()

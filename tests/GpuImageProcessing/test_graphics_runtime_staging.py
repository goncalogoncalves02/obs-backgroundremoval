# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLOSURE = ('avcodec-62.dll', 'avformat-62.dll', 'avutil-60.dll', 'librist.dll',
           'libx264-164.dll', 'srt.dll', 'swresample-6.dll', 'swscale-9.dll', 'zlib.dll')


class GraphicsRuntimeStaging(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.pwsh = os.environ.get('GPU_TEST_PWSH') or shutil.which('pwsh')
        if not cls.pwsh:
            raise AssertionError('PowerShell required for graphics staging fixtures')

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='graphics staging [fixture] ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build obs'
        self.deps = self.root / 'OBS deps'
        self.bin = self.deps / 'bin'
        self.vcpkg_prefix = self.root / 'vcpkg prefix'
        self.vcpkg = self.vcpkg_prefix / 'bin'
        self.dest = self.root / 'native test'
        for p in (self.build, self.bin, self.vcpkg, self.dest):
            p.mkdir(parents=True)
        for name in ('libobs-d3d11.dll', 'obs.dll', 'w32-pthreads.dll'):
            (self.build / name).write_bytes(('built ' + name).encode())
        for name in CLOSURE:
            (self.bin / name).write_bytes(('pinned dependency ' + name).encode())
        (self.vcpkg / 'opencv_core411.dll').write_bytes(b'pinned OpenCV')
        (self.dest / 'opencv_core411.dll').write_bytes(b'pinned OpenCV')
        (self.dest / 'obs.dll').write_bytes((self.build / 'obs.dll').read_bytes())
        (self.dest / 'gpu-image-processing-native.exe').write_bytes(b'native fixture')
        self.imports = self.root / 'imports.txt'
        self.imports.write_text('    obs.dll\n    D3D11.dll\n    KERNEL32.dll\n')
        # Synthetic PE-inspector boundary only. All staging, hashes, aliases and copies are real.
        self.wrapper = self.root / 'invoke.ps1'
        self.wrapper.write_text('''param($Script, $BuildRoot, $DepsRoot, $VcpkgRoot, $DestRoot, $Imports)
Write-Output 'graphics-staging-fixture-start'
$global:FixtureImports = $Imports
function dumpbin { $global:LASTEXITCODE = 0; Get-Content -LiteralPath $global:FixtureImports }
& $Script -ObsBuildDirectory $BuildRoot -ObsDepsPrefix $DepsRoot -VcpkgInstalledPrefix $VcpkgRoot -TestDirectory $DestRoot
''')

    def snapshot(self):
        return {p.name: p.read_bytes() for p in self.dest.iterdir() if p.is_file()}

    def stage(self):
        return subprocess.run([
            self.pwsh, '-NoProfile', '-NonInteractive', '-File', str(self.wrapper),
            str(ROOT / 'tests/GpuImageProcessing/stage-graphics-runtime.ps1'),
            str(self.build), str(self.deps), str(self.vcpkg_prefix), str(self.dest), str(self.imports),
        ], capture_output=True, text=True, timeout=30)

    def reject(self):
        before = self.snapshot()
        result = self.stage()
        self.assertIn('graphics-staging-fixture-start', result.stdout, result.stdout + result.stderr)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(before, self.snapshot(), 'Failure must precede every copy')

    def test_stages_only_proven_closure_and_is_idempotent(self):
        for _ in range(2):
            result = self.stage()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in ('libobs-d3d11.dll', 'obs.dll', 'w32-pthreads.dll'):
                self.assertEqual((self.dest / name).read_bytes(), (self.build / name).read_bytes())
            for name in CLOSURE:
                self.assertEqual((self.dest / name).read_bytes(), (self.bin / name).read_bytes())
            self.assertEqual(len(self.snapshot()), 14)
            self.assertIn('origin=', result.stdout)

    def test_static_triplet_without_bin_is_supported(self):
        (self.dest / 'opencv_core411.dll').unlink()
        (self.vcpkg / 'opencv_core411.dll').unlink()
        self.vcpkg.rmdir()
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertFalse(self.vcpkg.exists(), 'Staging must not invent a missing dynamic bin')
        self.assertEqual(len(self.snapshot()), 13)

    def test_missing_vcpkg_installed_prefix_fails_before_copies(self):
        (self.vcpkg / 'opencv_core411.dll').unlink()
        self.vcpkg.rmdir()
        self.vcpkg_prefix.rmdir()
        self.reject()

    def test_dynamic_opencv_without_bin_fails_before_copies(self):
        (self.vcpkg / 'opencv_core411.dll').unlink()
        self.vcpkg.rmdir()
        self.reject()

    def test_conflicting_destination_fails_preflight(self):
        (self.dest / 'ZLIB.DLL').write_bytes(b'other origin')
        self.reject()

    def test_missing_last_dependency_fails_preflight(self):
        (self.bin / 'zlib.dll').unlink()
        self.reject()

    def test_unproven_module_import_fails_preflight(self):
        self.imports.write_text('    obs.dll\n    unknown.dll\n')
        self.reject()

    def test_missing_module_or_core_import_fails_preflight(self):
        self.imports.write_text('    D3D11.dll\n')
        self.reject()
        (self.build / 'libobs-d3d11.dll').unlink()
        self.reject()

    def test_conflicting_built_outputs_fail_preflight(self):
        duplicate = self.build / 'rundir'
        duplicate.mkdir()
        (duplicate / 'libobs-d3d11.dll').write_bytes(b'other built bytes')
        self.reject()

    def test_identical_built_duplicates_are_proven(self):
        duplicate = self.build / 'rundir'
        duplicate.mkdir()
        for name in ('libobs-d3d11.dll', 'obs.dll', 'w32-pthreads.dll'):
            (duplicate / name).write_bytes((self.build / name).read_bytes())
        result = self.stage()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unknown_pre_staged_runtime_fails_preflight(self):
        (self.dest / 'onnxruntime.dll').write_bytes(b'unexpected')
        self.reject()

    def test_wrong_opencv_origin_fails_preflight(self):
        (self.vcpkg / 'opencv_core411.dll').write_bytes(b'other OpenCV')
        self.reject()

    def test_source_alias_fails_preflight(self):
        source = self.build / 'libobs-d3d11.dll'
        origin = self.build / 'module-origin.dll'
        source.rename(origin)
        try:
            source.symlink_to(origin)
        except OSError as error:
            self.skipTest(f'Filesystem symlink unavailable: {error}')
        self.reject()


if __name__ == '__main__':
    unittest.main()

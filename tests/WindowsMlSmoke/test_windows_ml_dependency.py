# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
WINDOWS_DEPENDENCIES = (
    "prebuilt_windows_x64",
    "qt6_windows_x64",
    "ccache_windows",
    "windows_ml",
)


class WindowsMlDependencyTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.fixture_root = Path(self.temporary_directory.name)
        self.fixture_root.joinpath("scripts").mkdir()
        self.fixture_root.joinpath("vendor", "obs-studio", ".deps").mkdir(parents=True)

        shutil.copy2(REPOSITORY_ROOT / "scripts" / "download-deps.cmake", self.fixture_root / "scripts")

    def test_downloads_windows_ml_with_a_valid_hash_extracts_its_config_and_reports_its_prefix(self):
        expected_prefix = self._prepare_fixture()

        result = self._run_download_dependencies()

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        self.assertTrue(
            (
                expected_prefix
                / "build"
                / "cmake"
                / "microsoft.windows.ai.machinelearning-config.cmake"
            ).is_file()
        )
        self.assertIn(f"WINDOWS_ML_PREFIX={expected_prefix}", self._combined_output(result))
        self.assertIn(
            f"WINDOWS_ML_PREFIX<<EOS\n{expected_prefix}\nEOS\n",
            self.fixture_root.joinpath("github-output.txt").read_text(),
        )

    def test_rejects_a_windows_ml_archive_with_an_invalid_hash(self):
        expected_prefix = self._prepare_fixture(windows_ml_sha256="0" * 64)

        result = self._run_download_dependencies()

        self.assertNotEqual(result.returncode, 0, self._diagnostic(result))
        self.assertIn("file DOWNLOAD HASH mismatch", self._combined_output(result))
        self.assertFalse(expected_prefix.exists())

    def _prepare_fixture(self, windows_ml_sha256=None):
        archives_directory = self.fixture_root / "archives"
        archives_directory.mkdir()
        archives = {
            "prebuilt_windows_x64": self._create_archive(archives_directory / "obs-deps.zip", {"obs-deps.txt": "obs"}),
            "qt6_windows_x64": self._create_archive(archives_directory / "qt6.zip", {"qt6.txt": "qt6"}),
            "ccache_windows": self._create_archive(
                archives_directory / "ccache-4.13.6-windows-x86_64.zip",
                {"ccache-4.13.6-windows-x86_64/ccache.exe": "ccache"},
            ),
            "windows_ml": self._create_archive(
                archives_directory / "windows-ml.nupkg",
                {"build/cmake/microsoft.windows.ai.machinelearning-config.cmake": "# controlled fixture\n"},
            ),
        }

        buildspec = (REPOSITORY_ROOT / "buildspec.props").read_text()
        for dependency in WINDOWS_DEPENDENCIES:
            archive_path, archive_sha256 = archives[dependency]
            buildspec = self._replace_setting(buildspec, f"{dependency}_url", archive_path.as_uri())
            expected_hash = windows_ml_sha256 if dependency == "windows_ml" and windows_ml_sha256 else archive_sha256
            buildspec = self._replace_setting(buildspec, f"{dependency}_sha256", expected_hash)
        buildspec = self._replace_setting(buildspec, "windows_ml_version", "2.2.12")
        self.fixture_root.joinpath("buildspec.props").write_text(buildspec)

        return self.fixture_root / ".deps" / "windows-ml"

    def _run_download_dependencies(self):
        cmake_command = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake") or "/tmp/obs-br-cmake/bin/cmake"
        github_output = self.fixture_root / "github-output.txt"
        environment = os.environ.copy()
        environment["GITHUB_OUTPUT"] = str(github_output)
        return subprocess.run(
            [cmake_command, "-DWIN32=ON", "-P", "scripts/download-deps.cmake"],
            cwd=self.fixture_root,
            env=environment,
            text=True,
            capture_output=True,
            check=False,
        )

    @staticmethod
    def _create_archive(path, contents):
        with zipfile.ZipFile(path, "w") as archive:
            for archive_path, content in contents.items():
                archive.writestr(archive_path, content)
        return path, hashlib.sha256(path.read_bytes()).hexdigest()

    @staticmethod
    def _replace_setting(buildspec, setting, value):
        prefix = f"{setting}="
        lines = buildspec.splitlines()
        for index, line in enumerate(lines):
            if line.startswith(prefix):
                lines[index] = f"{prefix}{value}"
                break
        else:
            lines.append(f"{prefix}{value}")
        return "\n".join(lines) + "\n"

    @staticmethod
    def _combined_output(result):
        return result.stdout + result.stderr

    def _diagnostic(self, result):
        return f"command output:\n{self._combined_output(result)}"


if __name__ == "__main__":
    unittest.main()

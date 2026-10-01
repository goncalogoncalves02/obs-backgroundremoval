# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class WindowsMlLegalTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.fixture_root = Path(self.temporary_directory.name)
        self.cmake_command = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if not self.cmake_command:
            self.skipTest("CMake is required for the Windows ML legal contract test")
        self.project_root = self.fixture_root / "project"
        self.collect_licenses_module = self.project_root / "cmake/collect_licenses.cmake"
        self._create_fixture()

    def test_windows_legal_text_uses_windows_ml_licenses_without_vendored_onnxruntime(self):
        result = self._configure()

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        windows_legal_text = (self.fixture_root / "build/windows/legal.txt").read_text()
        self.assertIn("PROJECT LICENSE MARKER", windows_legal_text)
        self.assertIn("PROJECT NOTICE MARKER", windows_legal_text)
        self.assertIn("WINDOWS ML LICENSE MARKER", windows_legal_text)
        self.assertIn("WINDOWS ML NOTICES MARKER", windows_legal_text)
        self.assertIn("===== Microsoft.Windows.AI.MachineLearning =====", windows_legal_text)
        self.assertIn(
            "===== Microsoft.Windows.AI.MachineLearning third-party notices =====",
            windows_legal_text,
        )
        self.assertNotIn("VENDORED ONNXRUNTIME LICENSE MARKER", windows_legal_text)
        self.assertNotIn("VENDORED ONNXRUNTIME NOTICES MARKER", windows_legal_text)

    def test_legacy_collection_keeps_vendored_onnxruntime_for_non_windows_consumers(self):
        result = self._configure()

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        legacy_legal_text = (self.fixture_root / "build/legacy/legal.txt").read_text()
        self.assertIn("PROJECT LICENSE MARKER", legacy_legal_text)
        self.assertIn("PROJECT NOTICE MARKER", legacy_legal_text)
        self.assertIn("VENDORED ONNXRUNTIME LICENSE MARKER", legacy_legal_text)
        self.assertIn("VENDORED ONNXRUNTIME NOTICES MARKER", legacy_legal_text)

    def _create_fixture(self):
        self.collect_licenses_module.parent.mkdir(parents=True)
        shutil.copyfile(
            REPOSITORY_ROOT / "cmake/collect_licenses.cmake",
            self.collect_licenses_module,
        )
        self.project_root.joinpath("LICENSE").write_text("PROJECT LICENSE MARKER\n")
        self.project_root.joinpath("NOTICE").write_text("PROJECT NOTICE MARKER\n")
        vendored_onnxruntime = self.project_root / "vendor/onnxruntime"
        vendored_onnxruntime.mkdir(parents=True)
        vendored_onnxruntime.joinpath("LICENSE").write_text("VENDORED ONNXRUNTIME LICENSE MARKER\n")
        vendored_onnxruntime.joinpath("ThirdPartyNotices.txt").write_text(
            "VENDORED ONNXRUNTIME NOTICES MARKER\n"
        )
        windows_ml = self.project_root / "windows-ml"
        windows_ml.mkdir()
        windows_ml.joinpath("license.txt").write_text("WINDOWS ML LICENSE MARKER\n")
        windows_ml.joinpath("ThirdPartyNotices.txt").write_text("WINDOWS ML NOTICES MARKER\n")
        self.project_root.joinpath("CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.28)
project(windows_ml_legal_contract NONE)
include("${COLLECT_LICENSES_MODULE}")
collect_licenses(
  "${OUTPUT_HEADER}"
  SKIP_VENDORED_ONNXRUNTIME
  EXTRA_LICENSE_FILES "${WINDOWS_ML_LICENSE}" "${WINDOWS_ML_NOTICES}"
)
collect_licenses("${LEGACY_OUTPUT_HEADER}")
"""
        )

    def _configure(self):
        return subprocess.run(
            [
                self.cmake_command,
                "-S",
                str(self.project_root),
                "-B",
                str(self.fixture_root / "build"),
                f"-DCOLLECT_LICENSES_MODULE={self.collect_licenses_module}",
                f"-DOUTPUT_HEADER={self.fixture_root / 'build/windows/legal.hpp'}",
                f"-DLEGACY_OUTPUT_HEADER={self.fixture_root / 'build/legacy/legal.hpp'}",
                f"-DWINDOWS_ML_LICENSE={self.project_root / 'windows-ml/license.txt'}",
                f"-DWINDOWS_ML_NOTICES={self.project_root / 'windows-ml/ThirdPartyNotices.txt'}",
            ],
            text=True,
            capture_output=True,
            check=False,
        )

    @staticmethod
    def _diagnostic(result):
        return f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"


if __name__ == "__main__":
    unittest.main()

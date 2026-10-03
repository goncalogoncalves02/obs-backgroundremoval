# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class NativeRegistrationTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.fixture = Path(self.temporary.name)
        self.source = self.fixture / "source"
        self.source.mkdir()
        self.native = self.source / "tests/GpuImageProcessing"
        shutil.copytree(ROOT / "tests/GpuImageProcessing", self.native, ignore=shutil.ignore_patterns("__pycache__"))
        shutil.copytree(ROOT / "src/obs-utils", self.source / "src/obs-utils")
        # Allows the controller to demonstrate registration RED on an immutable
        # prior CMake module; normal CI always consumes the checked-out module.
        prior = os.environ.get("GPU_NATIVE_CMAKE_CHECKPOINT")
        if prior:
            shutil.copyfile(prior, self.native / "CMakeLists.txt")
        self.build = self.fixture / "build"
        query = self.build / ".cmake/api/v1/query"
        query.mkdir(parents=True)
        (query / "codemodel-v2").touch()

    def configure(self, decoder=True):
        targets = ["BuildOptions", "OBS::libobs", "OpenCV::opencv_core", "OpenCV::opencv_imgproc",
                   "OnnxRuntimeBackend", "windows-ml-session-core"]
        if decoder:
            targets.append("opencv_imgcodecs")
        definitions = "\n".join(f"add_library({name} INTERFACE IMPORTED)" for name in targets)
        root = ROOT.as_posix()
        runtime = self.source / "DirectML.dll"
        runtime.write_bytes(b"configure-only runtime path; no native execution")
        (self.source / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.28)\nproject(Registration LANGUAGES CXX)\nenable_testing()\n'
            'set(WIN32 TRUE)\nset(GPU_IMAGE_PROCESSING_POLICY_ONLY OFF)\n'
            f'set(PROJECT_SOURCE_DIR "{root}")\n{definitions}\n'
            f'include("{root}/cmake/windows_ml_sessions.cmake")\n'
            f'set(WINDOWS_ML_DIRECTML_DLL "{runtime.as_posix()}")\n'
            'add_subdirectory(tests/GpuImageProcessing native)\n'
            'file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/native-path-$<CONFIG>.txt" '
            'CONTENT "$<TARGET_FILE:gpu-image-processing-native>")\n'
        )
        return subprocess.run(["cmake", "-S", str(self.source), "-B", str(self.build)],
                              text=True, capture_output=True, check=False)

    def test_real_native_target_registers_model_adapter_and_quality_sources(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        reply = self.build / ".cmake/api/v1/reply"
        index = json.loads(next(reply.glob("index-*.json")).read_text())
        model = json.loads((reply / index["reply"]["codemodel-v2"]["jsonFile"]).read_text())
        for configuration in model["configurations"]:
            target = next(item for item in configuration["targets"] if item["name"] == "gpu-image-processing-native")
            graph = json.loads((reply / target["jsonFile"]).read_text())
            sources = {Path(item["path"]).name for item in graph["sources"]}
            self.assertTrue({"quality-test.cpp", "ort-session-utils.cpp", "filter-boundaries.cpp",
                             "gpu-input-preprocessor.cpp", "gpu-mask-processor.cpp"}.issubset(sources), sources)

    def test_missing_decoder_target_fails_configuration_explicitly(self):
        result = self.configure(decoder=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("existing OpenCV JPEG/imgcodecs", result.stdout + result.stderr)

    def test_ctest_consumes_explicit_portrait_and_keeps_required_timeout(self):
        result = self.configure()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        # Registration only: an executable file lets CTest resolve the generated
        # target path. It is never run or counted as graphics/inference evidence.
        for record in self.build.glob("native-path-*.txt"):
            executable = Path(record.read_text())
            executable.parent.mkdir(parents=True, exist_ok=True)
            executable.touch()
            executable.chmod(0o755)
        result = subprocess.run(["ctest", "--test-dir", str(self.build), "-C", "RelWithDebInfo", "--show-only=json-v1"],
                                text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        native = next(test for test in json.loads(result.stdout)["tests"] if test["name"] == "gpu-image-processing-native")
        command = native["command"]
        for option, path in [("--effect-root", ROOT / "data/effects"), ("--model", ROOT / "data/models/mediapipe.onnx"),
                             ("--portrait", ROOT / "tests/Scenarios/0000-portrait-umireon.jpg")]:
            self.assertIn(option, command)
            self.assertEqual(Path(command[command.index(option) + 1]), path)
        properties = {item["name"]: item["value"] for item in native["properties"]}
        self.assertEqual(properties["TIMEOUT"], 300.0)


if __name__ == "__main__":
    unittest.main()

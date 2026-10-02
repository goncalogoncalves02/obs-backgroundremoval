# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


@unittest.skipIf(os.name == "nt", "These are the non-Windows production provider probes")
class OnnxRuntimeProviderDetectionTest(unittest.TestCase):
    def setUp(self):
        self.cmake_command = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if not self.cmake_command:
            self.skipTest("CMake is required for the ONNX Runtime provider probes")
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.fixture_root = Path(self.temporary_directory.name)

    def test_both_linkable_providers_enable_their_compile_definitions(self):
        self._check_providers(
            True, True,
            "cuda=TRUE\nrocm=TRUE\n"
            "definitions=HAVE_ONNXRUNTIME_CUDA_EP;HAVE_ONNXRUNTIME_ROCM_EP\n",
        )

    def test_only_linkable_cuda_enables_its_compile_definition(self):
        self._check_providers(
            True, False, "cuda=TRUE\nrocm=FALSE\ndefinitions=HAVE_ONNXRUNTIME_CUDA_EP\n",
        )

    def test_only_linkable_rocm_enables_its_compile_definition(self):
        self._check_providers(
            False, True, "cuda=FALSE\nrocm=TRUE\ndefinitions=HAVE_ONNXRUNTIME_ROCM_EP\n",
        )

    def test_declared_but_unlinkable_providers_do_not_enable_compile_definitions(self):
        self._check_providers(False, False, "cuda=FALSE\nrocm=FALSE\ndefinitions=\n")

    def _check_providers(self, cuda, rocm, expected):
        package_directory = self._build_onnxruntime_package(cuda, rocm)
        project_directory = self.fixture_root / "probe-project"
        project_directory.mkdir()

        # Execute the current root helpers and entire non-Windows probe block.
        # Only unrelated OBS/Qt/OpenCV/build/install setup is outside this fixture;
        # no provider-check implementation or required-library choice is copied.
        root_source = REPOSITORY_ROOT.joinpath("CMakeLists.txt").read_text()
        helpers_start = root_source.index("function(map_imported_executable_to_release ")
        helpers_end = root_source.index(
            'include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/onnxruntime_backend.cmake")', helpers_start,
        )
        probes_start = root_source.index("if(NOT WIN32)\n  block(PROPAGATE onnxruntime_FOUND ")
        probes_end = root_source.index("### Plugin main", probes_start)
        project_directory.joinpath("root-provider-probes.cmake").write_text(
            root_source[helpers_start:helpers_end] + root_source[probes_start:probes_end]
        )
        project_directory.joinpath("CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.28)
project(onnxruntime_provider_detection CXX)
include(CheckCXXSymbolExists)
include(CMakePushCheckState)
include("${BACKEND_MODULE}")
add_onnxruntime_backend(OnnxRuntimeBackend "" "")
add_library(BuildOptions INTERFACE)
include("${CMAKE_CURRENT_SOURCE_DIR}/root-provider-probes.cmake")
get_target_property(definitions BuildOptions INTERFACE_COMPILE_DEFINITIONS)
if(NOT definitions)
  set(definitions "")
endif()
file(WRITE "${CMAKE_BINARY_DIR}/result.txt"
  "cuda=${HAVE_ONNXRUNTIME_CUDA_EP}\\nrocm=${HAVE_ONNXRUNTIME_ROCM_EP}\\ndefinitions=${definitions}\\n")
"""
        )
        build_directory = self.fixture_root / "probe-build"
        result = self._run(
            "-S", project_directory, "-B", build_directory,
            f"-DBACKEND_MODULE={REPOSITORY_ROOT / 'cmake' / 'onnxruntime_backend.cmake'}",
            f"-Donnxruntime_DIR={package_directory}",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        configure_log = build_directory / "CMakeFiles" / "CMakeConfigureLog.yaml"
        diagnostic = result.stdout + result.stderr + configure_log.read_text()
        self.assertEqual(build_directory.joinpath("result.txt").read_text(), expected, diagnostic)

    def _build_onnxruntime_package(self, cuda, rocm):
        source_directory = self.fixture_root / "ort-source"
        include_directory = source_directory / "include"
        include_directory.mkdir(parents=True)
        # Declarations stay present even when definitions are absent from the
        # archive: detection must prove linkage, not just header availability.
        include_directory.joinpath("onnxruntime_c_api.h").write_text(
            """#ifndef FIXTURE_ORT_API
#error Imported ORT compile requirements were not propagated
#endif
#ifdef __cplusplus
extern "C" {
#endif
void OrtSessionOptionsAppendExecutionProvider_CUDA(void);
void OrtSessionOptionsAppendExecutionProvider_ROCM(void);
#ifdef __cplusplus
}
#endif
"""
        )
        source_directory.joinpath("onnxruntime.c").write_text(
            """#include "onnxruntime_c_api.h"
#ifdef FIXTURE_CUDA
void OrtSessionOptionsAppendExecutionProvider_CUDA(void) {}
#endif
#ifdef FIXTURE_ROCM
void OrtSessionOptionsAppendExecutionProvider_ROCM(void) {}
#endif
void fixture_ort_runtime(void) {}
"""
        )
        source_directory.joinpath("CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.28)
project(controlled_ort C)
add_library(onnxruntime STATIC onnxruntime.c)
target_include_directories(onnxruntime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(onnxruntime PUBLIC FIXTURE_ORT_API)
if(FIXTURE_CUDA)
  target_compile_definitions(onnxruntime PRIVATE FIXTURE_CUDA)
endif()
if(FIXTURE_ROCM)
  target_compile_definitions(onnxruntime PRIVATE FIXTURE_ROCM)
endif()
export(TARGETS onnxruntime NAMESPACE onnxruntime:: FILE "${CMAKE_BINARY_DIR}/onnxruntime-config.cmake")
"""
        )
        build_directory = self.fixture_root / "ort-build"
        result = self._run(
            "-S", source_directory, "-B", build_directory,
            f"-DFIXTURE_CUDA={'ON' if cuda else 'OFF'}",
            f"-DFIXTURE_ROCM={'ON' if rocm else 'OFF'}",
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = self._run("--build", build_directory)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return build_directory

    def _run(self, *arguments):
        return subprocess.run(
            [self.cmake_command, *(str(argument) for argument in arguments)],
            text=True,
            capture_output=True,
            check=False,
        )


if __name__ == "__main__":
    unittest.main()

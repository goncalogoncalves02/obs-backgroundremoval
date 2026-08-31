# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]


class OnnxRuntimeBackendTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.fixture_root = Path(self.temporary_directory.name)
        self.cmake_command = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
        if not self.cmake_command:
            self.skipTest("CMake is required for the ONNX Runtime backend contract test")

    def test_windows_package_links_the_windows_ml_runtime_and_exports_its_root(self):
        package_root = self._create_windows_ml_package()

        result, output = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=package_root / "build" / "cmake",
        )

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        self.assertEqual(
            output.read_text(),
            f"links=WindowsML::Api;WindowsML::OnnxRuntime\nroot={package_root}\n",
        )

    def test_windows_package_is_required(self):
        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=self.fixture_root / "missing-package",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertRegex(
            self._combined_output(result),
            r"Could not find a package configuration file provided by\s+"
            r"\"microsoft\.windows\.ai\.machinelearning\"",
        )

    def test_windows_package_does_not_fall_back_from_the_requested_directory(self):
        requested_directory = self.fixture_root / "requested-package" / "build" / "cmake"
        requested_directory.mkdir(parents=True)
        decoy_package_root = self._create_windows_ml_package(directory_name="decoy-windows-ml")

        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=requested_directory,
            cmake_prefix_path=decoy_package_root / "build" / "cmake",
        )

        self.assertNotEqual(result.returncode, 0, self._diagnostic(result))
        self.assertRegex(
            self._combined_output(result),
            r"Could not find a package configuration file provided by\s+"
            r"\"microsoft\.windows\.ai\.machinelearning\"",
        )

    def test_windows_package_requires_an_explicit_directory(self):
        decoy_package_root = self._create_windows_ml_package(directory_name="decoy-windows-ml")

        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_one_shot_directory=decoy_package_root / "build" / "cmake",
            cmake_prefix_path=decoy_package_root / "build" / "cmake",
        )

        self.assertNotEqual(result.returncode, 0, self._diagnostic(result))
        self.assertIn(
            "microsoft.windows.ai.machinelearning_DIR must be set explicitly",
            self._combined_output(result),
        )

    def test_windows_package_rejects_a_package_redirect(self):
        requested_package_root = self._create_windows_ml_package(directory_name="requested-windows-ml")
        redirect_package_root = self._create_windows_ml_package(directory_name="redirect-windows-ml")
        redirect_package_config = (
            redirect_package_root
            / "build"
            / "cmake"
            / "microsoft.windows.ai.machinelearning-config.cmake"
        )
        redirect_package_config.write_text(
            redirect_package_config.read_text()
            + "\nset(microsoft.windows.ai.machinelearning_DIR "
            + f'"{requested_package_root / "build" / "cmake"}")\n'
        )

        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=requested_package_root / "build" / "cmake",
            redirect_package_config=redirect_package_config,
        )

        self.assertNotEqual(result.returncode, 0, self._diagnostic(result))
        self.assertRegex(
            self._combined_output(result),
            r"A CMake package redirect for Microsoft\.Windows\.AI\.MachineLearning is not\s+permitted",
        )

    def test_windows_package_requires_the_one_shot_directory_on_every_configure(self):
        invalid_directory = self.fixture_root / "legacy-invalid-package" / "build" / "cmake"
        invalid_directory.mkdir(parents=True)
        decoy_package_root = self._create_windows_ml_package(directory_name="legacy-decoy-windows-ml")

        first_result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=invalid_directory,
            windows_ml_one_shot_directory="",
            cmake_prefix_path=decoy_package_root / "build" / "cmake",
            legacy_find_package=True,
        )
        second_result, _ = self._configure(
            expected_windows_ml_version="2.2.12",
            legacy_find_package=False,
        )

        self.assertEqual(first_result.returncode, 0, self._diagnostic(first_result))
        self.assertNotEqual(second_result.returncode, 0, self._diagnostic(second_result))
        self.assertIn(
            "WINDOWS_ML_PACKAGE_CONFIG_DIR must be supplied for every configure",
            self._combined_output(second_result),
        )

    def test_windows_package_does_not_cache_the_resolved_directory(self):
        package_root = self._create_windows_ml_package()
        package_directory = package_root / "build" / "cmake"

        first_result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=package_directory,
        )
        second_result, _ = self._configure(
            expected_windows_ml_version="2.2.12",
            windows_ml_one_shot_directory=package_directory,
        )

        self.assertEqual(first_result.returncode, 0, self._diagnostic(first_result))
        self.assertNotEqual(second_result.returncode, 0, self._diagnostic(second_result))
        self.assertIn(
            "microsoft.windows.ai.machinelearning_DIR must be set explicitly",
            self._combined_output(second_result),
        )

    def test_windows_package_requires_matching_explicit_directories(self):
        requested_package_root = self._create_windows_ml_package(directory_name="requested-windows-ml")
        other_package_root = self._create_windows_ml_package(directory_name="other-windows-ml")

        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=requested_package_root / "build" / "cmake",
            windows_ml_one_shot_directory=other_package_root / "build" / "cmake",
        )

        self.assertNotEqual(result.returncode, 0, self._diagnostic(result))
        self.assertIn("must name the same directory", self._combined_output(result))

    def test_windows_package_rejects_a_different_version(self):
        package_root = self._create_windows_ml_package(version="2.2.13")

        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=package_root / "build" / "cmake",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(
            "Expected Microsoft.Windows.AI.MachineLearning 2.2.12, found 2.2.13",
            self._combined_output(result),
        )

    def test_windows_package_requires_the_api_target(self):
        package_root = self._create_windows_ml_package(include_api_target=False)

        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=package_root / "build" / "cmake",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("WindowsML::Api", self._combined_output(result))

    def test_windows_package_requires_its_license_file(self):
        package_root = self._create_windows_ml_package(include_license=False)

        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=package_root / "build" / "cmake",
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn(str(package_root / "license.txt"), self._combined_output(result))

    def test_non_windows_package_links_the_standalone_runtime(self):
        package_root = self._create_onnxruntime_package()

        result, output = self._configure(onnxruntime_directory=package_root / "cmake")

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        self.assertEqual(output.read_text(), "links=onnxruntime::onnxruntime\n")

    def _create_windows_ml_package(
        self,
        version="2.2.12",
        include_api_target=True,
        include_license=True,
        directory_name="windows-ml",
    ):
        package_root = self.fixture_root / directory_name
        config_directory = package_root / "build" / "cmake"
        native_directory = package_root / "runtimes" / "win-x64" / "native"
        config_directory.mkdir(parents=True)
        native_directory.mkdir(parents=True)
        native_directory.joinpath("Microsoft.Windows.AI.MachineLearning.dll").write_bytes(b"api")
        native_directory.joinpath("onnxruntime.dll").write_bytes(b"onnxruntime")
        if include_license:
            package_root.joinpath("license.txt").write_text("license\n")
        package_root.joinpath("ThirdPartyNotices.txt").write_text("notices\n")

        api_target = "" if not include_api_target else """
add_library(WindowsML::Api SHARED IMPORTED)
set_target_properties(WindowsML::Api PROPERTIES IMPORTED_LOCATION "${_fixture_root}/runtimes/win-x64/native/Microsoft.Windows.AI.MachineLearning.dll")
"""
        config_directory.joinpath("microsoft.windows.ai.machinelearning-config.cmake").write_text(
            "\n".join(
                (
                    f'set(WINML_VERSION "{version}")',
                    'get_filename_component(_fixture_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)',
                    api_target.strip(),
                    'add_library(WindowsML::OnnxRuntime SHARED IMPORTED)',
                    'set_target_properties(WindowsML::OnnxRuntime PROPERTIES IMPORTED_LOCATION "${_fixture_root}/runtimes/win-x64/native/onnxruntime.dll")',
                    "",
                )
            )
        )
        return package_root

    def _create_onnxruntime_package(self):
        package_root = self.fixture_root / "onnxruntime"
        config_directory = package_root / "cmake"
        config_directory.mkdir(parents=True)
        config_directory.joinpath("onnxruntime-config.cmake").write_text(
            "add_library(onnxruntime::onnxruntime INTERFACE IMPORTED)\n"
        )
        return package_root

    def _configure(
        self,
        system_name=None,
        expected_windows_ml_version="",
        windows_ml_directory=None,
        windows_ml_one_shot_directory=None,
        onnxruntime_directory=None,
        cmake_prefix_path=None,
        redirect_package_config=None,
        legacy_find_package=None,
    ):
        fixture_directory = self.fixture_root / "project"
        fixture_directory.mkdir(exist_ok=True)
        output = fixture_directory / "result.txt"
        fixture_directory.joinpath("CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.28)
project(onnxruntime_backend_contract NONE)
if(LEGACY_FIND_PACKAGE)
  find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED)
  return()
endif()
if(REDIRECT_PACKAGE_CONFIG)
  configure_file(
    "${REDIRECT_PACKAGE_CONFIG}"
    "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/microsoft.windows.ai.machinelearning-config.cmake"
    COPYONLY
  )
endif()
list(APPEND CMAKE_MODULE_PATH "${BACKEND_MODULE_DIR}")
include(onnxruntime_backend)
add_onnxruntime_backend(
  OnnxRuntimeBackend
  "${EXPECTED_WINDOWS_ML_VERSION}"
  "${WINDOWS_ML_PACKAGE_CONFIG_DIR}"
)
get_target_property(backend_links OnnxRuntimeBackend INTERFACE_LINK_LIBRARIES)
file(WRITE "${RESULT_FILE}" "links=${backend_links}\\n")
if(WIN32)
  file(APPEND "${RESULT_FILE}" "root=${WINDOWS_ML_PACKAGE_ROOT}\\n")
endif()
"""
        )
        command = [
            self.cmake_command,
            "-S",
            str(fixture_directory),
            "-B",
            str(self.fixture_root / "build"),
            f"-DBACKEND_MODULE_DIR={REPOSITORY_ROOT / 'cmake'}",
            f"-DEXPECTED_WINDOWS_ML_VERSION={expected_windows_ml_version}",
            f"-DRESULT_FILE={output}",
        ]
        if system_name:
            command.append(f"-DCMAKE_SYSTEM_NAME={system_name}")
        if windows_ml_directory:
            command.append(f"-Dmicrosoft.windows.ai.machinelearning_DIR={windows_ml_directory}")
        if windows_ml_one_shot_directory is None:
            windows_ml_one_shot_directory = windows_ml_directory
        if windows_ml_one_shot_directory:
            command.append(f"-DWINDOWS_ML_PACKAGE_CONFIG_DIR={windows_ml_one_shot_directory}")
        if onnxruntime_directory:
            command.append(f"-Donnxruntime_DIR={onnxruntime_directory}")
        if cmake_prefix_path:
            command.append(f"-DCMAKE_PREFIX_PATH={cmake_prefix_path}")
        if redirect_package_config:
            command.append(f"-DREDIRECT_PACKAGE_CONFIG={redirect_package_config}")
        if legacy_find_package is not None:
            command.append(f"-DLEGACY_FIND_PACKAGE={'ON' if legacy_find_package else 'OFF'}")
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        return result, output

    @staticmethod
    def _combined_output(result):
        return result.stdout + result.stderr

    def _diagnostic(self, result):
        return f"command output:\n{self._combined_output(result)}"


if __name__ == "__main__":
    unittest.main()

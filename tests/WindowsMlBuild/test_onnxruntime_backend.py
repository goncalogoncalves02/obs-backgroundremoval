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
            f"links=WindowsML::Api;WindowsML::OnnxRuntime\nroot={package_root.resolve().as_posix()}\n",
        )

    def test_root_backend_include_resolves_in_an_out_of_source_build(self):
        project_directory = self.fixture_root / "root-project"
        module_directory = project_directory / "cmake"
        module_directory.mkdir(parents=True)
        shutil.copy2(REPOSITORY_ROOT / "cmake" / "onnxruntime_backend.cmake", module_directory)
        root_includes = [
            line for line in REPOSITORY_ROOT.joinpath("CMakeLists.txt").read_text().splitlines()
            if line.startswith("include(") and "onnxruntime_backend" in line
        ]
        self.assertEqual(len(root_includes), 1)
        project_directory.joinpath("CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.28)\n"
            "project(root_backend_include_contract NONE)\n"
            "set(WIN32 OFF)\n"
            + root_includes[0] + "\n"
            'add_onnxruntime_backend(OnnxRuntimeBackend "" "")\n'
            "get_target_property(backend_links OnnxRuntimeBackend INTERFACE_LINK_LIBRARIES)\n"
            'file(WRITE "${CMAKE_BINARY_DIR}/result.txt" "${backend_links}\\n")\n'
        )
        package_root = self._create_onnxruntime_package()
        build_directory = self.fixture_root / "root-build"
        result = subprocess.run(
            [self.cmake_command, "-S", str(project_directory), "-B", str(build_directory),
             f"-Donnxruntime_DIR={package_root / 'cmake'}"],
            cwd=self.fixture_root,
            text=True,
            capture_output=True,
            check=False,
        )

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        self.assertEqual(build_directory.joinpath("result.txt").read_text(), "onnxruntime::onnxruntime\n")

    def test_valid_windows_package_reconfigures_without_repeating_inputs(self):
        package_root = self._create_windows_ml_package()

        first_result, output = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=package_root / "build" / "cmake",
        )
        second_result = self._reconfigure_without_flags()

        self.assertEqual(first_result.returncode, 0, self._diagnostic(first_result))
        self.assertEqual(second_result.returncode, 0, self._diagnostic(second_result))
        self.assertEqual(
            output.read_text(),
            f"links=WindowsML::Api;WindowsML::OnnxRuntime\nroot={package_root.resolve().as_posix()}\n",
        )

    def test_valid_windows_package_survives_automatic_build_regeneration(self):
        package_root = self._create_windows_ml_package()

        configure_result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=package_root / "build" / "cmake",
        )
        project_file = self.fixture_root / "project" / "CMakeLists.txt"
        project_file.write_text(project_file.read_text() + "\n")
        build_result = self._build()

        self.assertEqual(configure_result.returncode, 0, self._diagnostic(configure_result))
        self.assertEqual(build_result.returncode, 0, self._diagnostic(build_result))

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

    def test_legacy_cached_directory_without_a_validated_marker_is_rejected(self):
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
            "WINDOWS_ML_PACKAGE_CONFIG_DIR must be supplied",
            self._combined_output(second_result),
        )

    def test_windows_package_rejects_an_unknown_state_marker_schema(self):
        package_root = self._create_windows_ml_package()

        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_state_schema="unknown",
            windows_ml_state_directory=package_root / "build" / "cmake",
        )

        self.assertNotEqual(result.returncode, 0, self._diagnostic(result))
        self.assertIn("unsupported Windows ML package state schema", self._combined_output(result))

    def test_windows_package_rejects_a_corrupt_state_marker(self):
        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_state_schema="1",
            windows_ml_state_directory=self.fixture_root / "missing-stored-package",
        )

        self.assertNotEqual(result.returncode, 0, self._diagnostic(result))
        self.assertRegex(
            self._combined_output(result),
            r"Could not find a package configuration file provided by\s+"
            r"\"microsoft\.windows\.ai\.machinelearning\"",
        )

    def test_fresh_windows_package_inputs_update_the_validated_marker(self):
        first_package_root = self._create_windows_ml_package(directory_name="first-windows-ml")
        second_package_root = self._create_windows_ml_package(directory_name="second-windows-ml")

        first_result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=first_package_root / "build" / "cmake",
        )
        update_result, output = self._configure(
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=second_package_root / "build" / "cmake",
        )
        stored_result = self._reconfigure_without_flags()

        self.assertEqual(first_result.returncode, 0, self._diagnostic(first_result))
        self.assertEqual(update_result.returncode, 0, self._diagnostic(update_result))
        self.assertEqual(stored_result.returncode, 0, self._diagnostic(stored_result))
        self.assertEqual(
            output.read_text(),
            f"links=WindowsML::Api;WindowsML::OnnxRuntime\nroot={second_package_root.resolve().as_posix()}\n",
        )

    def test_partial_fresh_input_invalidates_the_validated_marker(self):
        package_root = self._create_windows_ml_package()
        package_directory = package_root / "build" / "cmake"

        first_result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=package_directory,
        )
        partial_result, _ = self._configure(
            expected_windows_ml_version="2.2.12",
            windows_ml_one_shot_directory=package_directory,
        )
        stored_result = self._reconfigure_without_flags()

        self.assertEqual(first_result.returncode, 0, self._diagnostic(first_result))
        self.assertNotEqual(partial_result.returncode, 0, self._diagnostic(partial_result))
        self.assertNotEqual(stored_result.returncode, 0, self._diagnostic(stored_result))
        self.assertIn(
            "No validated Windows ML package state is available",
            self._combined_output(stored_result),
        )

    def test_invalid_fresh_inputs_cannot_reuse_a_previous_marker(self):
        for invalid_input in ("directory_only", "empty", "missing", "mismatch", "version", "target", "legal"):
            with self.subTest(invalid_input=invalid_input):
                self.setUp()
                package_root = self._create_windows_ml_package()
                package_directory = package_root / "build" / "cmake"
                first_result, _ = self._configure(
                    system_name="Windows",
                    expected_windows_ml_version="2.2.12",
                    windows_ml_directory=package_directory,
                )
                self.assertEqual(first_result.returncode, 0, self._diagnostic(first_result))
                options = {"windows_ml_directory": package_directory}
                if invalid_input == "directory_only":
                    options["windows_ml_one_shot_directory"] = ""
                elif invalid_input == "empty":
                    self._replace_cache_entry("WINDOWS_ML_PACKAGE_CONFIG_DIR", "PATH", "")
                    options = {}
                elif invalid_input == "missing":
                    options = {"windows_ml_directory": self.fixture_root / "missing"}
                elif invalid_input == "mismatch":
                    other_root = self._create_windows_ml_package(directory_name="other")
                    options["windows_ml_one_shot_directory"] = other_root / "build" / "cmake"
                else:
                    self._invalidate_package(package_root, invalid_input)
                result, _ = self._configure(expected_windows_ml_version="2.2.12", **options)
                stored_result = self._reconfigure_without_flags()
                self.assertNotEqual(result.returncode, 0, self._diagnostic(result))
                self.assertNotEqual(stored_result.returncode, 0, self._diagnostic(stored_result))
                self.assertIn("No validated Windows ML package state", self._combined_output(stored_result))

    def test_persisted_package_is_fully_validated_again(self):
        for invalid_input in ("missing", "version", "target", "legal", "redirect", "decoy", "resolved_mismatch"):
            with self.subTest(invalid_input=invalid_input):
                self.setUp()
                package_root = self._create_windows_ml_package()
                package_directory = package_root / "build" / "cmake"
                first_result, _ = self._configure(
                    system_name="Windows",
                    expected_windows_ml_version="2.2.12",
                    windows_ml_directory=package_directory,
                )
                self.assertEqual(first_result.returncode, 0, self._diagnostic(first_result))
                config_file = package_directory / "microsoft.windows.ai.machinelearning-config.cmake"
                if invalid_input in ("missing", "decoy"):
                    config_file.unlink()
                    if invalid_input == "decoy":
                        decoy = self._create_windows_ml_package(directory_name="decoy")
                        self._replace_cache_entry("CMAKE_PREFIX_PATH", "PATH", decoy / "build" / "cmake")
                elif invalid_input == "redirect":
                    self._replace_cache_entry("REDIRECT_PACKAGE_CONFIG", "FILEPATH", config_file)
                elif invalid_input == "resolved_mismatch":
                    config_file.write_text(config_file.read_text() + '\nset(microsoft.windows.ai.machinelearning_DIR "' + str(self.fixture_root / "other") + '")\n')
                else:
                    self._invalidate_package(package_root, invalid_input)
                result = self._reconfigure_without_flags()
                self.assertNotEqual(result.returncode, 0, self._diagnostic(result))

    def test_state_marker_requires_internal_entries_and_a_canonical_directory(self):
        for invalid_marker in ("schema_type", "directory_type", "relative", "unnormalized", "empty", "no_directory", "no_schema"):
            with self.subTest(invalid_marker=invalid_marker):
                self.setUp()
                package_root = self._create_windows_ml_package()
                package_directory = package_root / "build" / "cmake"
                first_result, _ = self._configure(
                    system_name="Windows",
                    expected_windows_ml_version="2.2.12",
                    windows_ml_directory=package_directory,
                )
                self.assertEqual(first_result.returncode, 0, self._diagnostic(first_result))
                if invalid_marker == "schema_type":
                    self._replace_cache_entry("OBS_WINDOWS_ML_PACKAGE_STATE_SCHEMA", "STRING", "1")
                elif invalid_marker == "directory_type":
                    self._replace_cache_entry("OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY", "PATH", package_directory)
                elif invalid_marker == "relative":
                    self._replace_cache_entry("OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY", "INTERNAL", "../windows-ml/build/cmake")
                elif invalid_marker == "unnormalized":
                    self._replace_cache_entry("OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY", "INTERNAL", str(package_directory) + "/../cmake")
                elif invalid_marker == "empty":
                    self._replace_cache_entry("OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY", "INTERNAL", "")
                else:
                    entry = "OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY" if invalid_marker == "no_directory" else "OBS_WINDOWS_ML_PACKAGE_STATE_SCHEMA"
                    self._replace_cache_entry(entry, None, None)
                result = self._reconfigure_without_flags()
                self.assertNotEqual(result.returncode, 0, self._diagnostic(result))

    def test_success_caches_only_internal_state_and_canonical_path(self):
        package_root = self._create_windows_ml_package()
        package_directory = package_root / "build" / "cmake"
        result, _ = self._configure(
            system_name="Windows",
            expected_windows_ml_version="2.2.12",
            windows_ml_directory=str(package_directory) + "/../cmake",
        )
        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        cache = self.fixture_root.joinpath("build", "CMakeCache.txt").read_text()
        self.assertIn("OBS_WINDOWS_ML_PACKAGE_STATE_SCHEMA:INTERNAL=1\n", cache)
        self.assertIn(
            f"OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY:INTERNAL={package_directory.resolve().as_posix()}\n",
            cache,
        )
        self.assertNotRegex(cache, r"(?m)^(?:WINDOWS_ML_PACKAGE_CONFIG_DIR|microsoft\.windows\.ai\.machinelearning_DIR):")

    def _replace_cache_entry(self, name, entry_type, value):
        cache_file = self.fixture_root / "build" / "CMakeCache.txt"
        lines = []
        replaced = False
        for line in cache_file.read_text().splitlines():
            if line.startswith(name + ":"):
                replaced = True
                if entry_type is not None:
                    lines.append(f"{name}:{entry_type}={value}")
            else:
                lines.append(line)
        if entry_type is not None and not replaced:
            lines.append(f"{name}:{entry_type}={value}")
        cache_file.write_text("\n".join(lines) + "\n")

    def _invalidate_package(self, package_root, invalid_input):
        config_file = package_root / "build" / "cmake" / "microsoft.windows.ai.machinelearning-config.cmake"
        if invalid_input == "version":
            config_file.write_text(config_file.read_text().replace('"2.2.12"', '"2.2.13"'))
        elif invalid_input == "target":
            config_file.write_text(config_file.read_text().replace("WindowsML::OnnxRuntime", "Other::Runtime"))
        elif invalid_input == "legal":
            package_root.joinpath("ThirdPartyNotices.txt").unlink()

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
        self.assertIn((package_root.resolve() / "license.txt").as_posix(), self._combined_output(result))

    def test_non_windows_package_links_the_standalone_runtime(self):
        package_root = self._create_onnxruntime_package()

        result, output = self._configure(
            fixture_win32=False,
            onnxruntime_directory=package_root / "cmake",
        )

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        self.assertEqual(output.read_text(), "links=onnxruntime::onnxruntime\n")

    def test_windows_package_contracts_accept_noncanonical_fixture_paths(self):
        for contract in (
            self.test_windows_package_links_the_windows_ml_runtime_and_exports_its_root,
            self.test_success_caches_only_internal_state_and_canonical_path,
            self.test_windows_package_requires_its_license_file,
        ):
            with self.subTest(contract=contract.__name__):
                self.setUp()
                alias_directory = self.fixture_root / "path-alias"
                alias_directory.mkdir()
                self.fixture_root = alias_directory / ".."
                contract()

    def test_non_windows_contract_branch_is_independent_of_the_target_platform(self):
        package_root = self._create_onnxruntime_package()

        result, output = self._configure(
            system_name="Windows",
            fixture_win32=False,
            onnxruntime_directory=package_root / "cmake",
        )
        repeated_result = self._reconfigure_without_flags()

        self.assertEqual(result.returncode, 0, self._diagnostic(result))
        self.assertEqual(repeated_result.returncode, 0, self._diagnostic(repeated_result))
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
        windows_ml_state_schema=None,
        windows_ml_state_directory=None,
        fixture_win32=None,
    ):
        fixture_directory = self.fixture_root / "project"
        fixture_directory.mkdir(exist_ok=True)
        output = fixture_directory / "result.txt"
        fixture_directory.joinpath("CMakeLists.txt").write_text(
            """cmake_minimum_required(VERSION 3.28)
project(onnxruntime_backend_contract NONE)
# Select only the contract branch; keep the generator and CMAKE_HOST_WIN32 real.
# The cache entry persists across flag-free reconfiguration and regeneration.
if(DEFINED FIXTURE_WIN32)
  set(WIN32 "${FIXTURE_WIN32}")
endif()
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
        if fixture_win32 is not None:
            command.append(f"-DFIXTURE_WIN32:BOOL={'ON' if fixture_win32 else 'OFF'}")
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
        if windows_ml_state_schema is not None:
            command.append(f"-DOBS_WINDOWS_ML_PACKAGE_STATE_SCHEMA:INTERNAL={windows_ml_state_schema}")
        if windows_ml_state_directory is not None:
            command.append(f"-DOBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY:INTERNAL={windows_ml_state_directory}")
        result = subprocess.run(command, text=True, capture_output=True, check=False)
        return result, output

    def _reconfigure_without_flags(self):
        return subprocess.run(
            [
                self.cmake_command,
                "-S",
                str(self.fixture_root / "project"),
                "-B",
                str(self.fixture_root / "build"),
            ],
            text=True,
            capture_output=True,
            check=False,
        )

    def _build(self):
        return subprocess.run(
            [self.cmake_command, "--build", str(self.fixture_root / "build")],
            text=True,
            capture_output=True,
            check=False,
        )

    @staticmethod
    def _combined_output(result):
        return result.stdout + result.stderr

    def _diagnostic(self, result):
        return f"command output:\n{self._combined_output(result)}"


if __name__ == "__main__":
    unittest.main()

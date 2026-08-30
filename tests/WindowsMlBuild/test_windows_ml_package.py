# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

import importlib.util
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
VERIFIER = REPOSITORY_ROOT / "scripts" / "verify_windows_ml_package.py"
PLUGIN_BIN = Path("obs-backgroundremoval/bin/64bit")
PLUGIN_LICENSES = Path("obs-backgroundremoval/licenses")


def load_verifier_module():
    specification = importlib.util.spec_from_file_location("windows_ml_package_verifier", VERIFIER)
    if specification is None or specification.loader is None:
        raise RuntimeError(f"could not load verifier module: {VERIFIER}")
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    return module


class WindowsMlPackageTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary_directory.cleanup)
        self.fixture_root = Path(self.temporary_directory.name)
        self.windows_ml_root = self.fixture_root / "windows-ml"
        self.install_root = self.fixture_root / "install"
        self.archive = self.fixture_root / "plugin.zip"
        self._create_valid_fixture()

    def test_valid_tree_and_archive_pass_the_package_contract(self):
        result = self._verify()

        self.assertEqual(result.returncode, 0, self._diagnostic(result))

    def test_nested_onnxruntime_dll_is_rejected(self):
        nested_runtime = self.install_root / "nested" / "onnxruntime.dll"
        nested_runtime.parent.mkdir()
        nested_runtime.write_bytes(b"second runtime")
        self._write_archive()

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("exactly one onnxruntime.dll", result.stderr)

    def test_tampered_installed_onnxruntime_dll_is_rejected(self):
        self.install_root.joinpath(PLUGIN_BIN, "onnxruntime.dll").write_bytes(b"tampered runtime")
        self._write_archive()

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("does not match the Windows ML package", result.stderr)

    def test_api_dll_must_be_beside_the_plugin(self):
        api_dll = self.install_root / PLUGIN_BIN / "Microsoft.Windows.AI.MachineLearning.dll"
        misplaced_api_dll = self.install_root / "elsewhere" / api_dll.name
        misplaced_api_dll.parent.mkdir()
        api_dll.replace(misplaced_api_dll)
        self._write_archive()

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("obs-backgroundremoval/bin/64bit", result.stderr)

    def test_directml_dll_is_prohibited_even_when_nested(self):
        directml = self.install_root / "nested" / "runtime" / "DirectML.dll"
        directml.parent.mkdir(parents=True)
        directml.write_bytes(b"prohibited runtime")
        self._write_archive()

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("DirectML.dll is prohibited in the Sprint 5 plugin package", result.stderr)

    def test_missing_or_changed_legal_file_is_rejected(self):
        legal_file = self.install_root / PLUGIN_LICENSES / "windows-ml-license.txt"
        legal_file.unlink()
        self._write_archive()

        missing_result = self._verify()

        self.assertEqual(missing_result.returncode, 1)
        self.assertIn("windows-ml-license.txt", missing_result.stderr)

        shutil.copyfile(self.windows_ml_root / "license.txt", legal_file)
        legal_file.write_bytes(b"changed legal text")
        self._write_archive()

        changed_result = self._verify()

        self.assertEqual(changed_result.returncode, 1)
        self.assertIn("windows-ml-license.txt", changed_result.stderr)

    def test_archive_duplicate_case_insensitive_runtime_is_rejected(self):
        with zipfile.ZipFile(self.archive, "a") as package:
            package.writestr(
                "obs-backgroundremoval/bin/64bit/ONNXRUNTIME.DLL",
                b"duplicate runtime",
            )

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("duplicate case-insensitive archive path", result.stderr)

    def test_missing_archive_reports_one_package_contract_error(self):
        self.archive.unlink()

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stderr.count("package-contract-error:"), 1)
        self.assertIn("could not read archive", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_file_read_error_becomes_package_contract_error(self):
        verifier = load_verifier_module()
        unreadable_file = self.fixture_root / "unreadable.bin"
        unreadable_file.write_bytes(b"contents")

        with mock.patch.object(Path, "open", side_effect=PermissionError("access denied")):
            try:
                verifier.sha256_file(unreadable_file)
            except verifier.PackageContractError as error:
                self.assertIn("could not read file", str(error))
                self.assertIn("unreadable.bin", str(error))
            except OSError as error:
                self.fail(f"filesystem error escaped the package contract: {error}")
            else:
                self.fail("filesystem error did not fail the package contract")

    def test_windows_drive_qualified_archive_path_is_rejected(self):
        with zipfile.ZipFile(self.archive, "a") as package:
            package.writestr(r"C:\\outside\\payload.dll", b"untrusted payload")

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("package-contract-error: archive contains unsafe path", result.stderr)

    def test_nested_api_dll_in_install_tree_is_rejected(self):
        nested_api = self.install_root / "nested" / "Microsoft.Windows.AI.MachineLearning.dll"
        nested_api.parent.mkdir()
        nested_api.write_bytes(b"second api")
        self._write_archive()

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("exactly one Microsoft.Windows.AI.MachineLearning.dll", result.stderr)

    def test_nested_api_dll_in_archive_is_rejected(self):
        with zipfile.ZipFile(self.archive, "a") as package:
            package.writestr("nested/Microsoft.Windows.AI.MachineLearning.dll", b"second api")

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("exactly one Microsoft.Windows.AI.MachineLearning.dll", result.stderr)

    def test_symbolic_linked_runtime_in_install_tree_is_rejected(self):
        runtime = self.install_root / PLUGIN_BIN / "onnxruntime.dll"
        runtime.unlink()
        try:
            runtime.symlink_to(self.windows_ml_root / "runtimes/win-x64/native/onnxruntime.dll")
        except OSError as error:
            self.skipTest(f"symbolic links are unavailable in this environment: {error}")
        self._write_archive()

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("package-contract-error: link is prohibited", result.stderr)
        self.assertIn("onnxruntime.dll", result.stderr)

    def test_directory_symbolic_link_in_install_tree_is_rejected_without_following_it(self):
        linked_directory_target = self.fixture_root / "outside-install-tree"
        linked_directory_target.mkdir()
        linked_directory_target.joinpath("DirectML.dll").write_bytes(b"prohibited runtime")
        linked_directory = self.install_root / "linked-directory"
        try:
            linked_directory.symlink_to(linked_directory_target, target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symbolic links are unavailable in this environment: {error}")

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("package-contract-error: link is prohibited", result.stderr)
        self.assertIn("linked-directory", result.stderr)

    def test_broken_directory_symbolic_link_in_install_tree_is_rejected(self):
        linked_directory = self.install_root / "broken-linked-directory"
        try:
            linked_directory.symlink_to(self.fixture_root / "missing-directory", target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symbolic links are unavailable in this environment: {error}")

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("package-contract-error: link is prohibited", result.stderr)
        self.assertIn("broken-linked-directory", result.stderr)

    def test_self_referential_directory_symbolic_link_in_install_tree_is_rejected(self):
        linked_directory = self.install_root / "self-linked-directory"
        try:
            linked_directory.symlink_to("self-linked-directory", target_is_directory=True)
        except OSError as error:
            self.skipTest(f"symbolic links are unavailable in this environment: {error}")

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("package-contract-error: link is prohibited", result.stderr)
        self.assertIn("self-linked-directory", result.stderr)

    def test_file_attribute_reparse_point_is_classified_as_link_like(self):
        verifier = load_verifier_module()
        metadata = type(
            "ReparsePointMetadata",
            (),
            {
                "st_mode": stat.S_IFDIR,
                "st_file_attributes": stat.FILE_ATTRIBUTE_REPARSE_POINT,
            },
        )()
        self.assertTrue(verifier.is_link_like_metadata(metadata))

    @unittest.skipUnless(sys.platform == "win32", "Windows junction test")
    def test_windows_junction_in_install_tree_is_rejected_without_following_it(self):
        junction_target = self.fixture_root / "outside-junction-target"
        junction_target.mkdir()
        junction_target.joinpath("DirectML.dll").write_bytes(b"prohibited runtime")
        junction = self.install_root / "linked-junction"
        creation = subprocess.run(
            ["cmd.exe", "/d", "/c", "mklink", "/j", str(junction), str(junction_target)],
            text=True,
            capture_output=True,
            check=False,
        )
        if creation.returncode != 0:
            diagnostic = f"{creation.stdout}\n{creation.stderr}".strip()
            permission_markers = (
                "access is denied",
                "acesso negado",
                "privilege",
                "privilegio",
                "privilégio",
            )
            if any(marker in diagnostic.casefold() for marker in permission_markers):
                self.skipTest(f"junction creation is not authorized: {diagnostic}")
            self.fail(f"junction creation failed: {diagnostic}")
        self.addCleanup(os.rmdir, junction)

        result = self._verify()

        self.assertEqual(result.returncode, 1)
        self.assertIn("package-contract-error: link is prohibited", result.stderr)
        self.assertIn("linked-junction", result.stderr)

    def _create_valid_fixture(self):
        native_directory = self.windows_ml_root / "runtimes/win-x64/native"
        native_directory.mkdir(parents=True)
        package_files = {
            self.windows_ml_root / "license.txt": b"windows ml license",
            self.windows_ml_root / "ThirdPartyNotices.txt": b"windows ml notices",
            native_directory / "Microsoft.Windows.AI.MachineLearning.dll": b"windows ml api",
            native_directory / "onnxruntime.dll": b"windows ml runtime",
        }
        for path, contents in package_files.items():
            path.write_bytes(contents)

        plugin_bin = self.install_root / PLUGIN_BIN
        plugin_licenses = self.install_root / PLUGIN_LICENSES
        plugin_bin.mkdir(parents=True)
        plugin_licenses.mkdir(parents=True)
        plugin_bin.joinpath("obs-backgroundremoval.dll").write_bytes(b"plugin binary")
        shutil.copyfile(
            native_directory / "Microsoft.Windows.AI.MachineLearning.dll",
            plugin_bin / "Microsoft.Windows.AI.MachineLearning.dll",
        )
        shutil.copyfile(native_directory / "onnxruntime.dll", plugin_bin / "onnxruntime.dll")
        shutil.copyfile(self.windows_ml_root / "license.txt", plugin_licenses / "windows-ml-license.txt")
        shutil.copyfile(
            self.windows_ml_root / "ThirdPartyNotices.txt",
            plugin_licenses / "windows-ml-third-party-notices.txt",
        )
        self._write_archive()

    def _write_archive(self):
        with zipfile.ZipFile(self.archive, "w") as package:
            for path in sorted(self.install_root.rglob("*")):
                if path.is_file():
                    package.write(path, path.relative_to(self.install_root).as_posix())

    def _verify(self):
        return subprocess.run(
            [
                sys.executable,
                str(VERIFIER),
                "--install-root",
                str(self.install_root),
                "--archive",
                str(self.archive),
                "--windows-ml-root",
                str(self.windows_ml_root),
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

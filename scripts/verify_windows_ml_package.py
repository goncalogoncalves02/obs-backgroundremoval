# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import lzma
import os
import stat
import sys
import zipfile
import zlib
from pathlib import Path, PurePosixPath, PureWindowsPath


PLUGIN_BIN = Path("obs-backgroundremoval/bin/64bit")
PLUGIN_LICENSES = Path("obs-backgroundremoval/licenses")
PACKAGE_NATIVE = Path("runtimes/win-x64/native")
RUNTIME_FILES = (
    "obs-backgroundremoval.dll",
    "Microsoft.Windows.AI.MachineLearning.dll",
    "onnxruntime.dll",
    "DirectML.dll",
)
LEGAL_MAP = {
    "windows-ml-license.txt": "license.txt",
    "windows-ml-third-party-notices.txt": "ThirdPartyNotices.txt",
}


class PackageContractError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    try:
        with path.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
    except OSError as error:
        raise PackageContractError(f"could not read file {path}: {error}") from error
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def is_link_like_metadata(metadata: os.stat_result) -> bool:
    return stat.S_ISLNK(metadata.st_mode) or bool(
        getattr(metadata, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT
    )


def require_safe_install_entry(path: Path, metadata: os.stat_result) -> None:
    if is_link_like_metadata(metadata):
        raise PackageContractError(f"link is prohibited: {path}")


def install_tree_entries(root: Path) -> list[Path]:
    entries: list[Path] = []
    pending_directories = [root]
    while pending_directories:
        directory = pending_directories.pop()
        try:
            directory_metadata = os.lstat(directory)
        except OSError as error:
            raise PackageContractError(f"could not inspect install tree directory {directory}: {error}") from error
        require_safe_install_entry(directory, directory_metadata)
        if not stat.S_ISDIR(directory_metadata.st_mode):
            raise PackageContractError(f"install tree directory is missing: {directory}")

        try:
            with os.scandir(directory) as iterator:
                directory_entries = sorted(iterator, key=lambda entry: (entry.name.casefold(), entry.name))
        except OSError as error:
            raise PackageContractError(f"could not enumerate install tree directory {directory}: {error}") from error

        child_directories = []
        for entry in directory_entries:
            path = directory / entry.name
            try:
                metadata = entry.stat(follow_symlinks=False)
            except OSError as error:
                raise PackageContractError(f"could not inspect install tree entry {path}: {error}") from error
            require_safe_install_entry(path, metadata)
            entries.append(path)
            if stat.S_ISDIR(metadata.st_mode):
                child_directories.append(path)
        pending_directories.extend(reversed(child_directories))
    return entries


def require_nonempty_file(path: Path) -> None:
    try:
        metadata = os.lstat(path)
    except (FileNotFoundError, NotADirectoryError):
        raise PackageContractError(f"required non-empty file is missing: {path}") from None
    except OSError as error:
        raise PackageContractError(f"could not inspect required file {path}: {error}") from error
    if is_link_like_metadata(metadata):
        raise PackageContractError(f"link is prohibited: {path}")
    if not stat.S_ISREG(metadata.st_mode) or metadata.st_size == 0:
        raise PackageContractError(f"required non-empty file is missing: {path}")


def files_named(entries: list[Path], filename: str) -> list[Path]:
    expected = filename.casefold()
    return sorted(path for path in entries if path.name.casefold() == expected)


def require_same_file(actual: Path, expected: Path) -> None:
    require_nonempty_file(actual)
    require_nonempty_file(expected)
    if sha256_file(actual) != sha256_file(expected):
        raise PackageContractError(f"{actual} does not match the Windows ML package file {expected}")


def verify_install_tree(install_root: Path, windows_ml_root: Path) -> None:
    entries = install_tree_entries(install_root)
    plugin_bin = install_root / PLUGIN_BIN
    plugin_licenses = install_root / PLUGIN_LICENSES
    package_native = windows_ml_root / PACKAGE_NATIVE
    for filename in RUNTIME_FILES:
        require_nonempty_file(plugin_bin / filename)
    onnx_files = files_named(entries, "onnxruntime.dll")
    if onnx_files != [plugin_bin / "onnxruntime.dll"]:
        raise PackageContractError("install tree must contain exactly one onnxruntime.dll beside the plugin")
    api_files = files_named(entries, "Microsoft.Windows.AI.MachineLearning.dll")
    if api_files != [plugin_bin / "Microsoft.Windows.AI.MachineLearning.dll"]:
        raise PackageContractError(
            "install tree must contain exactly one Microsoft.Windows.AI.MachineLearning.dll beside the plugin"
        )
    directml_files = files_named(entries, "DirectML.dll")
    if directml_files != [plugin_bin / "DirectML.dll"]:
        raise PackageContractError("install tree must contain exactly one DirectML.dll beside the plugin")
    require_same_file(plugin_bin / "DirectML.dll", package_native / "DirectML.dll")
    require_same_file(plugin_bin / "onnxruntime.dll", package_native / "onnxruntime.dll")
    require_same_file(
        plugin_bin / "Microsoft.Windows.AI.MachineLearning.dll",
        package_native / "Microsoft.Windows.AI.MachineLearning.dll",
    )
    for installed_name, package_name in LEGAL_MAP.items():
        require_same_file(plugin_licenses / installed_name, windows_ml_root / package_name)


def normalize_archive_path(name: str) -> PurePosixPath:
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or PureWindowsPath(name).drive or ".." in path.parts:
        raise PackageContractError(f"archive contains unsafe path: {name}")
    return path


def archive_entries(archive: zipfile.ZipFile) -> dict[str, tuple[PurePosixPath, zipfile.ZipInfo]]:
    entries: dict[str, tuple[PurePosixPath, zipfile.ZipInfo]] = {}
    for info in archive.infolist():
        if info.is_dir():
            continue
        path = normalize_archive_path(info.filename)
        key = path.as_posix().casefold()
        if key in entries:
            raise PackageContractError(f"duplicate case-insensitive archive path: {path.as_posix()}")
        entries[key] = (path, info)
    return entries


def archive_files_named(
    entries: dict[str, tuple[PurePosixPath, zipfile.ZipInfo]], filename: str
) -> list[tuple[PurePosixPath, zipfile.ZipInfo]]:
    expected = filename.casefold()
    return sorted((entry for entry in entries.values() if entry[0].name.casefold() == expected), key=lambda entry: entry[0])


def require_archive_file(
    archive: zipfile.ZipFile,
    entries: dict[str, tuple[PurePosixPath, zipfile.ZipInfo]],
    expected_path: Path,
) -> bytes:
    key = expected_path.as_posix().casefold()
    entry = entries.get(key)
    if entry is None:
        raise PackageContractError(f"required non-empty file is missing: {expected_path.as_posix()}")
    try:
        data = archive.read(entry[1])
    except (zipfile.BadZipFile, EOFError, OSError, zlib.error, lzma.LZMAError) as error:
        detail = " ".join(str(error).splitlines())
        raise PackageContractError(f"could not read archive entry {entry[1].filename!r}: {detail}") from error
    if not data:
        raise PackageContractError(f"required non-empty file is missing: {expected_path.as_posix()}")
    return data


def require_archive_same_file(
    archive: zipfile.ZipFile,
    entries: dict[str, tuple[PurePosixPath, zipfile.ZipInfo]],
    actual_path: Path,
    expected_path: Path,
) -> None:
    actual_data = require_archive_file(archive, entries, actual_path)
    require_nonempty_file(expected_path)
    if sha256_bytes(actual_data) != sha256_file(expected_path):
        raise PackageContractError(
            f"{actual_path.as_posix()} does not match the Windows ML package file {expected_path}"
        )


def verify_archive(archive_path: Path, windows_ml_root: Path) -> None:
    package_native = windows_ml_root / PACKAGE_NATIVE
    try:
        archive = zipfile.ZipFile(archive_path)
    except (OSError, zipfile.BadZipFile, zipfile.LargeZipFile) as error:
        raise PackageContractError(f"could not read archive {archive_path}: {error}") from error
    with archive:
        entries = archive_entries(archive)
        for filename in RUNTIME_FILES:
            require_archive_file(archive, entries, PLUGIN_BIN / filename)

        onnx_files = archive_files_named(entries, "onnxruntime.dll")
        if [path for path, _ in onnx_files] != [PurePosixPath(PLUGIN_BIN / "onnxruntime.dll")]:
            raise PackageContractError("archive must contain exactly one onnxruntime.dll beside the plugin")
        api_files = archive_files_named(entries, "Microsoft.Windows.AI.MachineLearning.dll")
        if [path for path, _ in api_files] != [
            PurePosixPath(PLUGIN_BIN / "Microsoft.Windows.AI.MachineLearning.dll")
        ]:
            raise PackageContractError(
                "archive must contain exactly one Microsoft.Windows.AI.MachineLearning.dll beside the plugin"
            )
        directml_files = archive_files_named(entries, "DirectML.dll")
        if [path for path, _ in directml_files] != [PurePosixPath(PLUGIN_BIN / "DirectML.dll")]:
            raise PackageContractError("archive must contain exactly one DirectML.dll beside the plugin")
        require_archive_same_file(
            archive,
            entries,
            PLUGIN_BIN / "DirectML.dll",
            package_native / "DirectML.dll",
        )

        require_archive_same_file(
            archive,
            entries,
            PLUGIN_BIN / "onnxruntime.dll",
            package_native / "onnxruntime.dll",
        )
        require_archive_same_file(
            archive,
            entries,
            PLUGIN_BIN / "Microsoft.Windows.AI.MachineLearning.dll",
            package_native / "Microsoft.Windows.AI.MachineLearning.dll",
        )
        for installed_name, package_name in LEGAL_MAP.items():
            require_archive_same_file(
                archive,
                entries,
                PLUGIN_LICENSES / installed_name,
                windows_ml_root / package_name,
            )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install-root", required=True, type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--windows-ml-root", required=True, type=Path)
    arguments = parser.parse_args()

    try:
        verify_install_tree(arguments.install_root, arguments.windows_ml_root)
        verify_archive(arguments.archive, arguments.windows_ml_root)
    except PackageContractError as error:
        print(f"package-contract-error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

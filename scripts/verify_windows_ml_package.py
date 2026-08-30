# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
#
# SPDX-License-Identifier: Apache-2.0

import argparse
import hashlib
import sys
import zipfile
from pathlib import Path, PurePosixPath, PureWindowsPath


PLUGIN_BIN = Path("obs-backgroundremoval/bin/64bit")
PLUGIN_LICENSES = Path("obs-backgroundremoval/licenses")
PACKAGE_NATIVE = Path("runtimes/win-x64/native")
RUNTIME_FILES = (
    "obs-backgroundremoval.dll",
    "Microsoft.Windows.AI.MachineLearning.dll",
    "onnxruntime.dll",
)
LEGAL_MAP = {
    "windows-ml-license.txt": "license.txt",
    "windows-ml-third-party-notices.txt": "ThirdPartyNotices.txt",
}


class PackageContractError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def is_link_like(path: Path) -> bool:
    if path.is_symlink():
        return True
    is_junction = getattr(path, "is_junction", None)
    return bool(is_junction and is_junction())


def require_nonempty_file(path: Path) -> None:
    if is_link_like(path):
        raise PackageContractError(f"symbolic link is prohibited: {path}")
    if not path.is_file() or path.stat().st_size == 0:
        raise PackageContractError(f"required non-empty file is missing: {path}")


def files_named(root: Path, filename: str) -> list[Path]:
    expected = filename.casefold()
    return sorted(
        path
        for path in root.rglob("*")
        if (path.is_file() or is_link_like(path)) and path.name.casefold() == expected
    )


def require_no_link_like_directories(root: Path) -> None:
    for path in root.rglob("*"):
        if is_link_like(path) and path.is_dir():
            raise PackageContractError(f"directory link is prohibited: {path}")


def require_same_file(actual: Path, expected: Path) -> None:
    require_nonempty_file(actual)
    require_nonempty_file(expected)
    if sha256_file(actual) != sha256_file(expected):
        raise PackageContractError(f"{actual} does not match the Windows ML package file {expected}")


def verify_install_tree(install_root: Path, windows_ml_root: Path) -> None:
    require_no_link_like_directories(install_root)
    plugin_bin = install_root / PLUGIN_BIN
    plugin_licenses = install_root / PLUGIN_LICENSES
    package_native = windows_ml_root / PACKAGE_NATIVE
    for filename in RUNTIME_FILES:
        require_nonempty_file(plugin_bin / filename)
    onnx_files = files_named(install_root, "onnxruntime.dll")
    if onnx_files != [plugin_bin / "onnxruntime.dll"]:
        raise PackageContractError("install tree must contain exactly one onnxruntime.dll beside the plugin")
    api_files = files_named(install_root, "Microsoft.Windows.AI.MachineLearning.dll")
    if api_files != [plugin_bin / "Microsoft.Windows.AI.MachineLearning.dll"]:
        raise PackageContractError(
            "install tree must contain exactly one Microsoft.Windows.AI.MachineLearning.dll beside the plugin"
        )
    if files_named(install_root, "DirectML.dll"):
        raise PackageContractError("DirectML.dll is prohibited in the Sprint 5 plugin package")
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
    data = archive.read(entry[1])
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
    with zipfile.ZipFile(archive_path) as archive:
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
        if archive_files_named(entries, "DirectML.dll"):
            raise PackageContractError("DirectML.dll is prohibited in the Sprint 5 plugin package")

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

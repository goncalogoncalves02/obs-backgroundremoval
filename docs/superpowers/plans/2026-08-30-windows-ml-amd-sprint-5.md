# Windows ML AMD Support — Sprint 5 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build and package the production Windows plugin against the ONNX Runtime supplied by `Microsoft.Windows.AI.MachineLearning` 2.2.12, preserve CPU inference, and remove the standalone Windows ONNX Runtime build without changing Linux or macOS semantics.

**Architecture:** A single internal `OnnxRuntimeBackend` interface target selects Windows ML on Windows and the existing standalone ONNX Runtime path elsewhere. Windows CMake installs the two imported runtime DLLs and package legal files, while a portable Python verifier proves the install tree and ZIP contain one package-origin ONNX Runtime and no DirectML. The exact feature head is reviewed, built, packaged, and tested in OBS before durable acceptance evidence is committed only to the `GPU` documentation branch.

**Tech Stack:** CMake 3.28, C++20, Microsoft.Windows.AI.MachineLearning 2.2.12, ONNX Runtime C/C++ targets, Python 3.9+ `unittest`, PowerShell, GitHub Actions, OBS Studio.

**Spec:** `docs/superpowers/specs/2026-08-30-windows-ml-amd-sprint-5-design.md`

## Global Constraints

- Commit this plan as a direct descendant of reviewed specification head `335cd696d35dbe706f95ba0c21da5eb372d6ce3e`, then keep all implementation on `feature/windows-ml-amd` until the exact-head acceptance gate.
- Use the self-contained `Microsoft.Windows.AI.MachineLearning` package version `2.2.12` from the pinned NuGet URL and require SHA-256 `9cb60543337e6e4eac2a95c2fcb9650a880697ae7190d15499d801c907849da3`.
- Windows links only `WindowsML::Api` and `WindowsML::OnnxRuntime`; it never searches standalone ONNX Runtime or pkg-config as a fallback.
- The root plugin target consumes one internal interface target named `OnnxRuntimeBackend` rather than a platform package target.
- Linux and macOS retain their existing standalone ONNX Runtime discovery, target alias, provider-symbol checks, build/install workflows, and provider semantics.
- Install `obs-backgroundremoval.dll`, `Microsoft.Windows.AI.MachineLearning.dll`, and `onnxruntime.dll` together under `obs-backgroundremoval/bin/64bit/`.
- Install the package's `license.txt` and `ThirdPartyNotices.txt` as `obs-backgroundremoval/licenses/windows-ml-license.txt` and `obs-backgroundremoval/licenses/windows-ml-third-party-notices.txt`.
- The install tree and ZIP contain exactly one `onnxruntime.dll`; its hash and the Windows ML API DLL hash must match the files in the pinned package.
- Do not install or package `DirectML.dll` in Sprint 5.
- Do not compile `src/DelayLoad.cpp`, add `/DELAYLOAD`, change DLL search paths, or add a loader hook. Stop for a separately approved design if OBS cannot load the adjacent dependencies.
- Do not compile the provider module into the plugin, select a GPU provider, add provider UI, change model/session behavior, or modify inference source code.
- Keep `vendor/onnxruntime`, `onnxruntime_git_tag`, `onnxruntime_reduced_ops_config`, `src/required_operators.config`, reduced-operator tests, macOS ORT triplets, and macOS ORT workflow steps.
- Remove only Windows-specific standalone-ORT workflow state and `vcpkg-triplets/x64-windows-static-md-obs-ort.cmake`.
- Keep the standalone Windows ML smoke tool and all existing smoke tests operational.
- Use strict RED/GREEN TDD for every behavior change. Run the named failing test before the implementation that makes it pass.
- New CMake, Python, and workflow files use `Apache-2.0`; the plan/spec and project evidence documents use `GPL-3.0-or-later` through `REUSE.toml`.
- Every project commit uses `Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>`, `git commit -s -S`, signing key `460B18400D17462FF3714A2BDCED30418A1C06FC`, and no assistant or co-author attribution.
- Do not push a task until its independent review is clean. Retain PR labels `windows-only-ci` and `upload-artifacts` during Sprint 5 iteration.
- Preserve the owner's untracked `obs-backgroundremoval-amd-windows-ml-roadmap.md`; never stage, delete, or rewrite it.

---

### Task 1: Add the platform ONNX Runtime dependency boundary

**Files:**

- Create: `cmake/onnxruntime_backend.cmake`
- Create: `tests/WindowsMlBuild/test_onnxruntime_backend.py`
- Modify: `CMakeLists.txt:20-38`
- Modify: `CMakeLists.txt:205-274`
- Modify: `CMakeLists.txt:284-302`

**Interfaces:**

- Consumes: `windows_ml_version` loaded from `buildspec.props`, the package directory supplied through `microsoft.windows.ai.machinelearning_DIR`, `PkgConfig_FOUND`, and the existing `BuildOptions` target.
- Produces: `add_onnxruntime_backend(TARGET_NAME EXPECTED_WINDOWS_ML_VERSION)`, the interface target `OnnxRuntimeBackend`, and Windows-only parent-scope variables `WINDOWS_ML_PACKAGE_ROOT`, `WINDOWS_ML_LICENSE_FILE`, and `WINDOWS_ML_THIRD_PARTY_NOTICES_FILE`.
- Contract: on Windows the interface links `WindowsML::Api;WindowsML::OnnxRuntime`; elsewhere it links the existing `onnxruntime::onnxruntime` target after preserving the current config/pkg-config discovery sequence.

- [ ] **Step 1: Write the failing CMake contract tests**

Create `tests/WindowsMlBuild/test_onnxruntime_backend.py` with an Apache-2.0 SPDX header and a `unittest.TestCase` that builds temporary, language-free CMake fixtures. Locate CMake with:

```python
cmake_command = os.environ.get("CMAKE_COMMAND") or shutil.which("cmake")
if not cmake_command:
    self.skipTest("CMake is required for the ONNX Runtime backend contract test")
```

The fixture root must contain a minimal project that loads the repository module and writes observable target properties:

```cmake
cmake_minimum_required(VERSION 3.28)
project(onnxruntime_backend_contract NONE)
list(APPEND CMAKE_MODULE_PATH "${BACKEND_MODULE_DIR}")
include(onnxruntime_backend)
add_onnxruntime_backend(OnnxRuntimeBackend "${EXPECTED_WINDOWS_ML_VERSION}")
get_target_property(backend_links OnnxRuntimeBackend INTERFACE_LINK_LIBRARIES)
file(WRITE "${RESULT_FILE}" "links=${backend_links}\n")
if(WIN32)
  file(APPEND "${RESULT_FILE}" "root=${WINDOWS_ML_PACKAGE_ROOT}\n")
endif()
```

Generate a controlled Windows ML package with `build/cmake/microsoft.windows.ai.machinelearning-config.cmake`, two fake non-empty DLLs under `runtimes/win-x64/native/`, `license.txt`, and `ThirdPartyNotices.txt`. The controlled config must define the requested targets exactly:

```cmake
set(WINML_VERSION "${FIXTURE_WINML_VERSION}")
get_filename_component(_fixture_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
add_library(WindowsML::Api SHARED IMPORTED)
set_target_properties(WindowsML::Api PROPERTIES IMPORTED_LOCATION "${_fixture_root}/runtimes/win-x64/native/Microsoft.Windows.AI.MachineLearning.dll")
add_library(WindowsML::OnnxRuntime SHARED IMPORTED)
set_target_properties(WindowsML::OnnxRuntime PROPERTIES IMPORTED_LOCATION "${_fixture_root}/runtimes/win-x64/native/onnxruntime.dll")
```

Cover these observable cases:

- Windows success with `CMAKE_SYSTEM_NAME=Windows`, version `2.2.12`, exact interface links, and the normalized fixture package root;
- missing Windows ML package fails with the normal `find_package(... CONFIG REQUIRED)` diagnostic;
- version `2.2.13` fails with `Expected Microsoft.Windows.AI.MachineLearning 2.2.12, found 2.2.13`;
- missing `WindowsML::Api` fails naming that target;
- missing `license.txt` fails naming that exact file;
- non-Windows success with a controlled `onnxruntime-config.cmake` and `links=onnxruntime::onnxruntime`.

Also add a repository-level assertion that the `block(PROPAGATE ...)` declaration in `CMakeLists.txt` includes `windows_ml_version`; this catches a root configure that passes an empty expected version even when the backend module itself is correct.

- [ ] **Step 2: Run the backend tests to verify RED**

Run:

```bash
python3 -m unittest tests/WindowsMlBuild/test_onnxruntime_backend.py -v
```

Expected: every case that imports `onnxruntime_backend.cmake` fails because the module does not exist. If CMake is unavailable on the controller host, run this RED step on the Windows runner before implementation and retain the failing output in `.superpowers/sdd/sprint-5/task-1-red.txt`.

- [ ] **Step 3: Implement `add_onnxruntime_backend` minimally**

Create `cmake/onnxruntime_backend.cmake` with an Apache-2.0 SPDX header. The public function must create the requested interface target once and use this platform split:

```cmake
function(add_onnxruntime_backend TARGET_NAME EXPECTED_WINDOWS_ML_VERSION)
  if(TARGET "${TARGET_NAME}")
    message(FATAL_ERROR "ONNX Runtime backend target '${TARGET_NAME}' already exists.")
  endif()

  add_library("${TARGET_NAME}" INTERFACE)

  if(WIN32)
    find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED)

    set(found_windows_ml_version undefined)
    if(DEFINED WINML_VERSION)
      set(found_windows_ml_version "${WINML_VERSION}")
    endif()
    if(NOT found_windows_ml_version VERSION_EQUAL EXPECTED_WINDOWS_ML_VERSION)
      message(FATAL_ERROR "Expected Microsoft.Windows.AI.MachineLearning ${EXPECTED_WINDOWS_ML_VERSION}, found ${found_windows_ml_version}.")
    endif()

    foreach(required_target IN ITEMS WindowsML::Api WindowsML::OnnxRuntime)
      if(NOT TARGET "${required_target}")
        message(FATAL_ERROR "Microsoft.Windows.AI.MachineLearning is missing required target ${required_target}.")
      endif()
    endforeach()

    target_link_libraries("${TARGET_NAME}" INTERFACE WindowsML::Api WindowsML::OnnxRuntime)

    cmake_path(GET microsoft.windows.ai.machinelearning_DIR PARENT_PATH windows_ml_build_dir)
    cmake_path(GET windows_ml_build_dir PARENT_PATH windows_ml_package_root)
    cmake_path(NORMAL_PATH windows_ml_package_root)
    set(windows_ml_license_file "${windows_ml_package_root}/license.txt")
    set(windows_ml_third_party_notices_file "${windows_ml_package_root}/ThirdPartyNotices.txt")

    foreach(required_file IN ITEMS "${windows_ml_license_file}" "${windows_ml_third_party_notices_file}")
      if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Microsoft.Windows.AI.MachineLearning is missing required legal file ${required_file}.")
      endif()
    endforeach()

    set(WINDOWS_ML_PACKAGE_ROOT "${windows_ml_package_root}" PARENT_SCOPE)
    set(WINDOWS_ML_LICENSE_FILE "${windows_ml_license_file}" PARENT_SCOPE)
    set(WINDOWS_ML_THIRD_PARTY_NOTICES_FILE "${windows_ml_third_party_notices_file}" PARENT_SCOPE)
    return()
  endif()

  find_package(onnxruntime CONFIG)
  if(NOT onnxruntime_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(PC_onnxruntime onnxruntime IMPORTED_TARGET)
    if(PC_onnxruntime_FOUND)
      add_library(onnxruntime::onnxruntime ALIAS PkgConfig::PC_onnxruntime)
      set(onnxruntime_FOUND TRUE)
    endif()
  endif()
  if(NOT onnxruntime_FOUND)
    message(FATAL_ERROR "ONNX Runtime not found via CMake or pkg-config.")
  endif()
  if(NOT TARGET onnxruntime::onnxruntime)
    add_library(onnxruntime::onnxruntime ALIAS onnxruntime)
  endif()
  target_link_libraries("${TARGET_NAME}" INTERFACE onnxruntime::onnxruntime)
endfunction()
```

Do not add an option that permits Windows fallback.

- [ ] **Step 4: Wire the root CMake target without changing inference**

In `CMakeLists.txt`, add `windows_ml_version` to the existing top-level `block(PROPAGATE ...)` list that loads `buildspec.props`. Include the new module after dependency helpers, then call:

```cmake
add_onnxruntime_backend(OnnxRuntimeBackend "${windows_ml_version}")
```

Keep the existing `find_program(Protobuf_PROTOC_EXECUTABLE ...)`, CMake 4 protobuf mapping, CUDA symbol check, ROCm symbol check, and associated `BuildOptions` definitions only inside `if(NOT WIN32)`. Use `OnnxRuntimeBackend` as `CMAKE_REQUIRED_LIBRARIES` for the non-Windows symbol probes.

Replace only this plugin link entry:

```cmake
onnxruntime::onnxruntime
```

with:

```cmake
OnnxRuntimeBackend
```

Do not edit any C/C++ source or `tools/windows-ml-smoke/CMakeLists.txt`.

- [ ] **Step 5: Run GREEN verification for Task 1**

Run:

```bash
python3 -m unittest tests/WindowsMlBuild/test_onnxruntime_backend.py -v
python3 -m unittest tests/WindowsMlSmoke/test_windows_ml_dependency.py -v
git diff --check
rg -n "OnnxRuntimeBackend|WindowsML::Api|WindowsML::OnnxRuntime" CMakeLists.txt cmake/onnxruntime_backend.cmake
rg -n "find_package\(onnxruntime|pkg_check_modules\(PC_onnxruntime" CMakeLists.txt cmake/onnxruntime_backend.cmake
```

Expected: all fixture and dependency tests pass; the Windows branch links exactly the two approved Windows ML targets; standalone discovery appears only in the non-Windows branch; no whitespace errors exist.

- [ ] **Step 6: Commit Task 1**

```bash
git add CMakeLists.txt cmake/onnxruntime_backend.cmake tests/WindowsMlBuild/test_onnxruntime_backend.py
git -c user.name='Gonçalo Filipe Brigues Gonçalves' \
    -c user.email='goncalogoncalves.02@gmail.com' \
    -c user.signingkey='460B18400D17462FF3714A2BDCED30418A1C06FC' \
    commit -s -S -m 'Select ONNX Runtime by platform'
```

- [ ] **Step 7: Run the Task 1 independent review gate**

Give a fresh reviewer the Task 1 commit, the Sprint 5 spec, and these review questions: Does Windows have any standalone fallback? Is the version check exact despite the missing config-version file? Do Linux/macOS preserve the previous discovery and provider checks? Are package-root and legal-file outputs derived only from the resolved config directory? Resolve every finding with the original implementer, add a signed fix commit when needed, and repeat the focused review before Task 2.

---

### Task 2: Install and verify Windows ML runtime and legal files

**Files:**

- Create: `scripts/verify_windows_ml_package.py`
- Create: `tests/WindowsMlBuild/test_windows_ml_package.py`
- Create: `tests/WindowsMlBuild/test_windows_ml_legal.py`
- Modify: `cmake/collect_licenses.cmake:5-42`
- Modify: `CMakeLists.txt:315-320`
- Modify: `CMakeLists.txt:406-417`

**Interfaces:**

- Consumes: `WindowsML::Api`, `WindowsML::OnnxRuntime`, `WINDOWS_ML_PACKAGE_ROOT`, `WINDOWS_ML_LICENSE_FILE`, `WINDOWS_ML_THIRD_PARTY_NOTICES_FILE`, `OBS_PLUGIN_BIN_DIR`, and the installed plugin root.
- Produces: `collect_licenses(OUTPUT_HEADER [SKIP_VENDORED_ONNXRUNTIME] [EXTRA_LICENSE_FILES files...])` and the CLI `scripts/verify_windows_ml_package.py --install-root PATH --archive PATH --windows-ml-root PATH`.
- Package verifier exit contract: exit `0` only when both the install tree and ZIP pass; exit `1` with one actionable `package-contract-error: ...` line for any missing, duplicate, misplaced, empty, wrong-origin, or prohibited file.

- [ ] **Step 1: Write the failing portable package-verifier tests**

Create `tests/WindowsMlBuild/test_windows_ml_package.py` with controlled temporary fixtures. The successful fixture must contain:

```text
windows-ml/
  license.txt
  ThirdPartyNotices.txt
  runtimes/win-x64/native/
    Microsoft.Windows.AI.MachineLearning.dll
    onnxruntime.dll
install/
  obs-backgroundremoval/bin/64bit/
    obs-backgroundremoval.dll
    Microsoft.Windows.AI.MachineLearning.dll
    onnxruntime.dll
  obs-backgroundremoval/licenses/
    windows-ml-license.txt
    windows-ml-third-party-notices.txt
plugin.zip
```

Use distinct non-empty byte strings for each source file, copy the approved source files into the install fixture, and create the ZIP from the install tree. Invoke the verifier through `subprocess.run` and cover:

- the complete valid tree and ZIP exit `0`;
- a second nested `onnxruntime.dll` exits `1` and reports `exactly one onnxruntime.dll`;
- a tampered installed `onnxruntime.dll` exits `1` and reports `does not match the Windows ML package`;
- a misplaced API DLL exits `1` and names `obs-backgroundremoval/bin/64bit`;
- any `DirectML.dll`, including a nested one, exits `1` and reports it is prohibited in Sprint 5;
- a missing or changed legal file exits `1` and names that file;
- an archive with a duplicate case-insensitive `ONNXRUNTIME.DLL` entry exits `1`.

- [ ] **Step 2: Write the failing legal-collector test**

Create `tests/WindowsMlBuild/test_windows_ml_legal.py`. Its fixture must copy `cmake/collect_licenses.cmake`, create marker text in project `LICENSE`, `NOTICE`, vendored ONNX Runtime license/notices, and Windows ML license/notices, then execute a CMake script containing:

```cmake
include("${COLLECT_LICENSES_MODULE}")
collect_licenses(
  "${OUTPUT_HEADER}"
  SKIP_VENDORED_ONNXRUNTIME
  EXTRA_LICENSE_FILES "${WINDOWS_ML_LICENSE}" "${WINDOWS_ML_NOTICES}"
)
```

Read the generated `legal.txt` and assert it contains the project and both Windows ML markers, uses distinct `Microsoft.Windows.AI.MachineLearning` and `Microsoft.Windows.AI.MachineLearning third-party notices` headings, and excludes both vendored ONNX Runtime markers. Add a second invocation without arguments and assert the legacy vendored markers remain for non-Windows consumers.

- [ ] **Step 3: Run Task 2 tests to verify RED**

Run:

```bash
python3 -m unittest tests/WindowsMlBuild/test_windows_ml_package.py -v
python3 -m unittest tests/WindowsMlBuild/test_windows_ml_legal.py -v
```

Expected: the verifier tests fail because `scripts/verify_windows_ml_package.py` does not exist; the legal test fails because `collect_licenses` does not parse the new arguments or exclude vendored ORT.

- [ ] **Step 4: Implement the package verifier**

Create `scripts/verify_windows_ml_package.py` with an Apache-2.0 SPDX header and these exact path constants:

```python
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
```

Implement the filesystem half around these helpers and checks:

```python
class PackageContractError(RuntimeError):
    pass


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_nonempty_file(path: Path) -> None:
    if not path.is_file() or path.stat().st_size == 0:
        raise PackageContractError(f"required non-empty file is missing: {path}")


def files_named(root: Path, filename: str) -> list[Path]:
    expected = filename.casefold()
    return sorted(path for path in root.rglob("*") if path.is_file() and path.name.casefold() == expected)


def require_same_file(actual: Path, expected: Path) -> None:
    require_nonempty_file(actual)
    require_nonempty_file(expected)
    if sha256_file(actual) != sha256_file(expected):
        raise PackageContractError(f"{actual} does not match the Windows ML package file {expected}")


def verify_install_tree(install_root: Path, windows_ml_root: Path) -> None:
    plugin_bin = install_root / PLUGIN_BIN
    plugin_licenses = install_root / PLUGIN_LICENSES
    package_native = windows_ml_root / PACKAGE_NATIVE
    for filename in RUNTIME_FILES:
        require_nonempty_file(plugin_bin / filename)
    onnx_files = files_named(install_root, "onnxruntime.dll")
    if onnx_files != [plugin_bin / "onnxruntime.dll"]:
        raise PackageContractError("install tree must contain exactly one onnxruntime.dll beside the plugin")
    if files_named(install_root, "DirectML.dll"):
        raise PackageContractError("DirectML.dll is prohibited in the Sprint 5 plugin package")
    require_same_file(plugin_bin / "onnxruntime.dll", package_native / "onnxruntime.dll")
    require_same_file(
        plugin_bin / "Microsoft.Windows.AI.MachineLearning.dll",
        package_native / "Microsoft.Windows.AI.MachineLearning.dll",
    )
    for installed_name, package_name in LEGAL_MAP.items():
        require_same_file(plugin_licenses / installed_name, windows_ml_root / package_name)
```

Implement `verify_archive()` without extraction: read non-directory `ZipInfo` entries, normalize each name by replacing backslashes with slashes and constructing `PurePosixPath`, reject absolute paths and `..`, build a case-folded name map, reject every duplicate key, and apply the same exact-path/count/hash rules to `ZipFile.read(info)`. `sha256_bytes(data)` returns `hashlib.sha256(data).hexdigest()`.

`main()` must parse the three required `Path` arguments, call `verify_install_tree()` and `verify_archive()`, and return `0`. Catch only `PackageContractError`, print `package-contract-error: <message>` to stderr, and return `1`; let unexpected programming errors retain a traceback during development.

- [ ] **Step 5: Extend legal collection without changing non-Windows defaults**

In `cmake/collect_licenses.cmake`, parse arguments with:

```cmake
cmake_parse_arguments(
  PARSE_ARGV 1
  COLLECT
  "SKIP_VENDORED_ONNXRUNTIME"
  ""
  "EXTRA_LICENSE_FILES"
)
```

Append vendored ONNX Runtime files only when `COLLECT_SKIP_VENDORED_ONNXRUNTIME` is false. Append and require every path in `COLLECT_EXTRA_LICENSE_FILES`. When an extra file is named `license.txt`, label it `Microsoft.Windows.AI.MachineLearning`; when named `ThirdPartyNotices.txt`, label it `Microsoft.Windows.AI.MachineLearning third-party notices`; reject any other extra filename with a fatal error. Preserve sorting, duplicate removal, `legal.txt`, and `legal.hpp` byte generation for existing callers.

In the root `collect_licenses` call, use the new mode only on Windows:

```cmake
if(WIN32)
  collect_licenses(
    "${legal_header}"
    SKIP_VENDORED_ONNXRUNTIME
    EXTRA_LICENSE_FILES "${WINDOWS_ML_LICENSE_FILE}" "${WINDOWS_ML_THIRD_PARTY_NOTICES_FILE}"
  )
else()
  collect_licenses("${legal_header}")
endif()
```

- [ ] **Step 6: Install imported runtimes and legal files**

Inside the existing `elseif(MSVC)` install branch, immediately after installing the plugin target, add:

```cmake
install(
  IMPORTED_RUNTIME_ARTIFACTS WindowsML::Api WindowsML::OnnxRuntime
  RUNTIME DESTINATION "${OBS_PLUGIN_BIN_DIR}"
)
install(
  FILES "${WINDOWS_ML_LICENSE_FILE}"
  DESTINATION "${CMAKE_PROJECT_NAME}/licenses"
  RENAME windows-ml-license.txt
)
install(
  FILES "${WINDOWS_ML_THIRD_PARTY_NOTICES_FILE}"
  DESTINATION "${CMAKE_PROJECT_NAME}/licenses"
  RENAME windows-ml-third-party-notices.txt
)
```

Do not install `WindowsML::DirectML`, use `WINML_DIRECTML_DLL`, glob DLLs, or copy from `PATH`.

- [ ] **Step 7: Run GREEN verification for Task 2**

Run:

```bash
python3 -m unittest tests/WindowsMlBuild/test_windows_ml_package.py -v
python3 -m unittest tests/WindowsMlBuild/test_windows_ml_legal.py -v
python3 -m unittest tests/WindowsMlBuild/test_onnxruntime_backend.py -v
git diff --check
rg -n "IMPORTED_RUNTIME_ARTIFACTS|windows-ml-license|windows-ml-third-party" CMakeLists.txt cmake/collect_licenses.cmake
```

Expected: all new contract tests pass; existing backend tests remain green; the CMake install path contains no reference to `WindowsML::DirectML` or `WINML_DIRECTML_DLL`.

- [ ] **Step 8: Commit Task 2**

```bash
git add CMakeLists.txt cmake/collect_licenses.cmake scripts/verify_windows_ml_package.py tests/WindowsMlBuild
git -c user.name='Gonçalo Filipe Brigues Gonçalves' \
    -c user.email='goncalogoncalves.02@gmail.com' \
    -c user.signingkey='460B18400D17462FF3714A2BDCED30418A1C06FC' \
    commit -s -S -m 'Package Windows ML plugin runtime'
```

- [ ] **Step 9: Run the Task 2 independent review gate**

Give a fresh reviewer the Task 2 commit and ask them to trace every installed byte back to the resolved package, inspect case-insensitive duplicate handling, confirm the verifier never extracts untrusted ZIP paths, confirm Windows legal text excludes the unrelated vendored runtime, and confirm non-Windows legal behavior is unchanged. Resume the original implementer for fixes and repeat the focused review before Task 3.

---

### Task 3: Remove standalone Windows ORT and enforce the package contract in CI

**Files:**

- Modify: `.github/workflows/build-windows.yml:45-50`
- Modify: `.github/workflows/build-windows.yml:78-80`
- Modify: `.github/workflows/build-windows.yml:294-448`
- Modify: `.github/workflows/build-windows.yml:450-471`
- Modify: `.github/workflows/build-windows.yml:490-505`
- Delete: `vcpkg-triplets/x64-windows-static-md-obs-ort.cmake`

**Interfaces:**

- Consumes: `WINDOWS_ML_PREFIX` from `scripts/download-deps.cmake`, the Task 1/2 tests, the installed `build_prefix`, and the plugin ZIP path.
- Produces: a Windows workflow with no standalone ORT build state, root plugin configuration pinned directly to `${WINDOWS_ML_PREFIX}\build\cmake`, and a package upload blocked by `scripts/verify_windows_ml_package.py`.

- [ ] **Step 1: Capture the RED workflow/package behavior**

Before editing the workflow, run the new portable verifier against the current plugin artifact shape if an extracted artifact is available. Otherwise, create the valid Task 2 test fixture, remove both Windows ML DLLs and legal files, and run:

```bash
python3 scripts/verify_windows_ml_package.py \
  --install-root .superpowers/sdd/sprint-5/current-package/install \
  --archive .superpowers/sdd/sprint-5/current-package/plugin.zip \
  --windows-ml-root .superpowers/sdd/sprint-5/current-package/windows-ml
```

Expected: exit `1`, reporting the first missing required Windows ML runtime. Save the output under the ignored `.superpowers/sdd/sprint-5/task-3-red.txt`.

- [ ] **Step 2: Remove Windows-only standalone ORT state and steps**

In `.github/workflows/build-windows.yml`:

- remove job env values `CCACHE_ORT_DIR` and `VCPKG_ORT_TARGET_TRIPLET`;
- change the Windows submodule command to initialize only `vendor/obs-studio` and `vendor/vcpkg`;
- delete the complete steps named `Install vcpkg_ort dependencies (Windows)`, `Restore ccache_ort (Windows)`, `Reset ccache_ort stats`, `Generate reduced operators for ONNX Runtime`, `Verify reduced operators for ONNX Runtime (Windows)`, `Configure ONNX Runtime (Windows)`, `Build ONNX Runtime (Windows)`, `Install ONNX Runtime (Windows)`, `Save ccache_ort`, and `Show ccache_ort stats`;
- leave the Windows ML smoke configure/build/test/package steps unchanged except for formatting required by adjacent edits;
- leave normal vcpkg dependency installation intact.

Delete `vcpkg-triplets/x64-windows-static-md-obs-ort.cmake`. Do not change `vcpkg-triplets/REUSE.toml`, because its `*.cmake` annotation continues to cover the remaining triplets.

- [ ] **Step 3: Configure the plugin directly against the pinned package**

In `Configure (Windows)`, add:

```yaml
WINDOWS_ML_DIR: ${{ steps.setup-buildspec-deps.outputs.WINDOWS_ML_PREFIX }}\build\cmake
```

Remove `${{ github.workspace }}\vcpkg_ort_installed\${{ env.VCPKG_ORT_TARGET_TRIPLET }}` and `${{ github.workspace }}\ort_installed` from `CMAKE_PREFIX_PATH`. Add this exact configure argument:

```powershell
"-Dmicrosoft.windows.ai.machinelearning_DIR=$env:WINDOWS_ML_DIR"
```

This direct package directory is the only permitted Windows ML discovery input for the plugin build.

- [ ] **Step 4: Run build-contract tests in Windows CI**

After Python setup and before configuring the smoke tool, add a Windows step that enters the x64 developer shell and runs:

```powershell
& $env:PYTHON_COMMAND -m unittest tests/WindowsMlBuild/test_onnxruntime_backend.py -v
& $env:PYTHON_COMMAND -m unittest tests/WindowsMlBuild/test_windows_ml_package.py -v
& $env:PYTHON_COMMAND -m unittest tests/WindowsMlBuild/test_windows_ml_legal.py -v
& $env:PYTHON_COMMAND -m unittest tests/WindowsMlSmoke/test_windows_ml_dependency.py -v
```

Pass `PYTHON_COMMAND` from `steps.setup-python.outputs.python-path`. These tests must run before the plugin build so dependency-contract failures remain cheap.

- [ ] **Step 5: Verify the completed install tree and ZIP before upload**

Keep `cmake --install build --config RelWithDebInfo --prefix build_prefix`. Create the ZIP exactly as the current workflow does, then return to the repository root and run:

```powershell
Set-Location $env:GITHUB_WORKSPACE
& $env:PYTHON_COMMAND scripts/verify_windows_ml_package.py `
  --install-root (Join-Path $env:GITHUB_WORKSPACE 'build_prefix') `
  --archive (Join-Path $env:GITHUB_WORKSPACE $env:DLL_ARTIFACT_NAME) `
  --windows-ml-root $env:WINDOWS_ML_PREFIX
```

Add `PYTHON_COMMAND` and `WINDOWS_ML_PREFIX: ${{ steps.setup-buildspec-deps.outputs.WINDOWS_ML_PREFIX }}` to the package step environment. The verifier must execute after `Compress-Archive` and before attestation/upload.

- [ ] **Step 6: Run local workflow and isolation checks**

Run:

```bash
python3 -m unittest discover -s tests/WindowsMlBuild -p 'test_*.py' -v
python3 -m unittest tests/WindowsMlSmoke/test_windows_ml_dependency.py -v
python3 -m unittest tests/WindowsMlSmoke/test_windows_ml_smoke.py -v
gersemi --check CMakeLists.txt cmake/collect_licenses.cmake cmake/onnxruntime_backend.cmake
reuse lint
git diff --check
! rg -n "CCACHE_ORT_DIR|VCPKG_ORT_TARGET_TRIPLET|vcpkg_ort_installed|ort_installed|build_ort|ccache_ort" .github/workflows/build-windows.yml
! rg -n "vendor/onnxruntime" .github/workflows/build-windows.yml
rg -n "vendor/onnxruntime|build_ort|vcpkg_ort_installed|ort_installed" .github/workflows/build-macos.yml
git diff --exit-code HEAD -- .github/workflows/build-macos.yml .github/workflows/build-ubuntu.yml .github/workflows/build-debian.yml .github/workflows/build-arch.yml bin/build bin/configure CMakePresets.json
```

Expected: all portable tests pass or only CMake-dependent tests are explicitly skipped when CMake is unavailable; Windows workflow contains none of the removed standalone state; macOS still contains its standalone ORT path; no non-Windows workflow or local Unix build entry point changed.

- [ ] **Step 7: Commit Task 3**

```bash
git add .github/workflows/build-windows.yml vcpkg-triplets/x64-windows-static-md-obs-ort.cmake
git -c user.name='Gonçalo Filipe Brigues Gonçalves' \
    -c user.email='goncalogoncalves.02@gmail.com' \
    -c user.signingkey='460B18400D17462FF3714A2BDCED30418A1C06FC' \
    commit -s -S -m 'Remove standalone Windows ONNX Runtime'
```

- [ ] **Step 8: Run the Task 3 independent review gate**

Give a fresh reviewer the workflow before/after diff and ask them to inventory every removed variable, cache, directory, vcpkg installation, reduced-operator step, ORT configure/build/install step, and prefix entry. Require them to confirm the smoke tool remains intact, package verification precedes upload, Linux/macOS files are unchanged, and no second runtime can enter through CMake search paths. Resolve findings with the original implementer and repeat the focused review.

- [ ] **Step 9: Run the final whole-sprint source review**

Use a new reviewer who has not implemented Tasks 1–3. Give them the Sprint 5 spec, all commits since `335cd696d35dbe706f95ba0c21da5eb372d6ce3e`, and the full diff. The review must separately report:

- specification compliance;
- Windows no-fallback and one-runtime proof;
- package verifier safety and completeness;
- legal-file correctness;
- Linux/macOS semantic isolation;
- absence of provider integration, UI, inference, DirectML, and loader-hook scope creep;
- test gaps and actionable findings.

Do not push until every blocking finding is fixed, the affected task is re-reviewed, and the whole-sprint reviewer confirms the final diff is clean.

---

### Task 4: Run exact-head CI, package inspection, and OBS CPU acceptance

**Files:**

- Create transiently: `.superpowers/sdd/sprint-5/exact-head-verification.md`
- Modify after acceptance on branch `GPU`: `docs/windows-ml-amd-session-handoff.md`
- Modify after acceptance on branch `GPU`: `REUSE.toml` only if a new durable evidence document is added during review

**Interfaces:**

- Consumes: the reviewed feature head from Task 3, PR #1 labels, GitHub Actions run/job/artifact metadata, the exact plugin ZIP, verifier output, and the owner's sanitized OBS log.
- Produces: a feature head whose exact artifact passed Windows CI and manual CPU-mask acceptance, plus a signed documentation-only `GPU` commit recording that evidence without changing the tested feature head.

- [ ] **Step 1: Verify commit integrity and record the exact feature head**

Run:

```bash
git switch feature/windows-ml-amd
git status --short --branch
git log --show-signature --format=fuller -4
git log -4 --format='%H%n%an <%ae>%n%B%n---'
git diff --check origin/feature/windows-ml-amd..HEAD
FEATURE_SHA=$(git rev-parse HEAD)
printf '%s\n' "$FEATURE_SHA"
```

Require every Sprint 5 commit to have a good signature from key `460B18400D17462FF3714A2BDCED30418A1C06FC`, the exact author identity, and a matching `Signed-off-by` trailer. The only allowed untracked owner file is `obs-backgroundremoval-amd-windows-ml-roadmap.md`.

- [ ] **Step 2: Confirm PR labels and push the reviewed head**

Run:

```bash
gh pr view 1 --repo goncalogoncalves02/obs-backgroundremoval --json labels,headRefName,headRefOid,isDraft,url
git push origin feature/windows-ml-amd
```

Require `headRefName=feature/windows-ml-amd` and labels `windows-only-ci` plus `upload-artifacts`. After the push, require the PR head OID to equal `$FEATURE_SHA`. Do not remove the quota-conservation label in this sprint.

- [ ] **Step 3: Require exact-head Check CI and Windows PR Check success**

Use `gh run list` and `gh run view` to select runs whose `headSha` equals `$FEATURE_SHA`; never use merely the newest run. Require:

- `Check CI` conclusion `success`, including clang-format, gersemi, and REUSE;
- `PR Check` job `build-windows-x64 / build` conclusion `success` at the same SHA;
- no cancelled, skipped, or superseded Windows job used as evidence.

Record the exact run IDs, job ID, URLs, start/end timestamps, and source SHA in `.superpowers/sdd/sprint-5/exact-head-verification.md`.

- [ ] **Step 4: Download and independently verify the exact plugin artifact**

Download the plugin artifact from the exact Windows job into a new directory under `.superpowers/sdd/sprint-5/artifact-$FEATURE_SHA/`. Record the GitHub artifact ID, artifact name, GitHub digest, ZIP SHA-256, and extracted file hashes. Run the repository verifier against the downloaded archive and the pinned package root:

```bash
python3 scripts/verify_windows_ml_package.py \
  --install-root .superpowers/sdd/sprint-5/artifact-$FEATURE_SHA/extracted \
  --archive .superpowers/sdd/sprint-5/artifact-$FEATURE_SHA/obs-backgroundremoval.zip \
  --windows-ml-root .superpowers/sdd/sprint-5/windows-ml-2.2.12
```

The downloaded/extracted package root must first be hash-verified against the pinned NuGet SHA. Require exactly these runtime entries beside the plugin: `obs-backgroundremoval.dll`, `Microsoft.Windows.AI.MachineLearning.dll`, and `onnxruntime.dll`; require both legal entries; require no `DirectML.dll` anywhere.

- [ ] **Step 5: Hand the exact artifact to the owner for OBS acceptance**

Provide the owner the artifact URL, artifact/ZIP digest, `$FEATURE_SHA`, and a safe installation procedure that backs up any existing `%ProgramData%\obs-studio\plugins\obs-backgroundremoval` directory before copying the complete `obs-backgroundremoval` directory from the ZIP.

Ask the owner to start OBS, add or recreate the Background Removal filter with `MediaPipe`, verify a visibly working CPU mask, remove and recreate the filter once, close OBS, and return the relevant sanitized log section. The log review must require normal input `1x144x256x3` and output `1x144x256x2` initialization and must reject:

- `The specified module could not be found` or equivalent DLL-load errors;
- ONNX Runtime load, model initialization, or session creation errors;
- `Model is not initialized`;
- crashes during filter destruction/recreation.

If adjacent DLL resolution fails, stop Sprint 5 and return to architectural design. Do not activate `DelayLoad.cpp` or patch search paths.

- [ ] **Step 6: Record durable evidence on `GPU` without changing the tested feature head**

After owner acceptance, leave `feature/windows-ml-amd` at `$FEATURE_SHA`. Switch the clean checkout to `GPU`, update `docs/windows-ml-amd-session-handoff.md` with the exact feature SHA, CI run/job IDs, artifact ID/name/digest, ZIP and DLL hashes, OBS/Windows versions, CPU-mask result, sanitized log conclusion, and the Sprint 6 entry decision. State explicitly that DirectML remains unintegrated in the production plugin.

Commit only the handoff change on `GPU`:

```bash
git add docs/windows-ml-amd-session-handoff.md REUSE.toml
git -c user.name='Gonçalo Filipe Brigues Gonçalves' \
    -c user.email='goncalogoncalves.02@gmail.com' \
    -c user.signingkey='460B18400D17462FF3714A2BDCED30418A1C06FC' \
    commit -s -S -m 'Record Windows ML plugin packaging acceptance'
git push origin GPU
```

Do not merge or copy feature code to `GPU`. If `REUSE.toml` did not change, omit it from `git add`.

- [ ] **Step 7: Verify the final branch states**

Verify:

```bash
git verify-commit GPU
git show --check --oneline GPU
git rev-parse feature/windows-ml-amd
git ls-remote --heads origin GPU feature/windows-ml-amd
git status --short --branch
```

Require local/remote `feature/windows-ml-amd` to remain `$FEATURE_SHA`, local/remote `GPU` to equal the signed documentation commit, the feature PR to remain draft, and no feature implementation files to appear in the `GPU` commit.

## Sprint completion gate

Sprint 5 is complete only when Tasks 1–3 have clean independent reviews, the final whole-sprint source review is clean, `Check CI` and the exact `build-windows-x64 / build` job succeed for the same feature SHA, the downloaded ZIP passes independent package verification, and the owner confirms that exact artifact loads in OBS and produces a working MediaPipe CPU mask through filter destruction/recreation. The durable `GPU` handoff must record all evidence without moving the tested feature head. A compile-only result, a smoke-tool-only result, a ZIP with unverified DLL origin, or a docs-only descendant that was not manually tested is insufficient.

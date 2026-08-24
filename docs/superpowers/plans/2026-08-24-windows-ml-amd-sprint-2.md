# Windows ML AMD Sprint 2 CPU Smoke Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. All production behavior follows strict red-green-refactor with superpowers:test-driven-development.

**Goal:** Pin the stable self-contained Windows ML package and prove, in a standalone Windows x64 Release executable, that its ONNX Runtime can load and execute the tracked `data/models/mediapipe.onnx` model on CPU.

**Architecture:** `tools/windows-ml-smoke` is an independent C++20 CMake project. It links `WindowsML::Api` and `WindowsML::OnnxRuntime`, copies their runtime DLLs beside the executable, and does not link OBS or the plugin's reduced static ONNX Runtime. A small platform-neutral command-line layer owns argument validation and deterministic diagnostics; the Windows entry point owns OS/runtime inspection and real ONNX Runtime inference. CI builds and runs this tool before the expensive OBS/standalone-ORT build, while the existing plugin build remains unchanged.

**Tech Stack:** C++20, CMake 3.21+, Visual Studio 2026 x64 toolchain, Windows SDK 10.0.26100, `Microsoft.Windows.AI.MachineLearning` 2.2.12, ONNX Runtime C++ API, Python 3.9 `unittest`, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-24-windows-ml-amd-design.md`

## Global Constraints

- Pin `Microsoft.Windows.AI.MachineLearning` exactly to `2.2.12` from `https://api.nuget.org/v3-flatcontainer/microsoft.windows.ai.machinelearning/2.2.12/microsoft.windows.ai.machinelearning.2.2.12.nupkg` with SHA-256 `9cb60543337e6e4eac2a95c2fcb9650a880697ae7190d15499d801c907849da3`.
- Build only x64 Release with C++20 and Windows SDK `10.0.26100`; the package requires CMake 3.21 or newer.
- Link the smoke executable only to the package's imported targets `WindowsML::Api` and `WindowsML::OnnxRuntime`; deploy their `$<TARGET_RUNTIME_DLLS:windows-ml-smoke>` beside the executable.
- Keep the smoke tool in a separate process. Do not link it to OBS, plugin targets, `ort_installed`, or the plugin's reduced static ONNX Runtime.
- Do not replace or modify the Windows plugin runtime in this sprint. Existing Windows, Linux, and macOS plugin build semantics remain unchanged.
- Support only `--provider cpu --model <path>` in this sprint. Do not add provider discovery, preparation, registration, GPU device selection, comparison, or benchmarking.
- The only accepted model for the passing gate is the tracked `data/models/mediapipe.onnx`; the smoke tool must perform a real inference, not only construct a session.
- Diagnostics are stable `key=value` lines written to standard output. Errors are prefixed `error=` on standard error and return nonzero.
- The success report includes `status=ok`, `provider=cpu`, `architecture=x64`, `windows_version`, `windows_ml_package_version=2.2.12`, `onnxruntime_version`, model path, input/output counts, input/output names, element types, tensor shapes, `iterations=1`, finite output count, and `latency_ms`.
- A valid MediaPipe run requires one float input of shape `1x144x256x3`, one float output of shape `1x144x256x2`, every output value finite, and exactly `73728` output values.
- Keep the pull request's `windows-only-ci` behavior during this sprint. Do not enable macOS/Linux jobs; the full matrix remains a later release gate.
- Every new source, test, and CMake file must carry the repository's SPDX copyright and Apache-2.0 license headers so REUSE remains green.
- Preserve all changes in the original `GPU` worktree.
- Commits use `Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>` only and contain no assistant attribution or co-author trailer.

---

### Task 1: Close the Sprint 1 baseline with the evidence actually collected

**Files:**

- Modify: `docs/windows-ml-baseline.md`
- Modify (ignored): `.superpowers/sdd/2026-08-24-windows-ml-amd-sprint-1/progress.md`

**Interfaces:**

- Consumes: tested commit `2a41a6f822375299117a448f358eb64f8082b4de`, Check CI run `32741918244`, PR Check run `32741919482`, Windows job `97478151708`, artifact ID `9526045291`, GitHub artifact digest `e3d4981980e8383491b4e697cf3e466dbf28b1bb5be15e6d48651d06c5691ec4`, extracted DLL SHA-256 `1122ff3576fe750a344acb209263eb8f8432ed736fc52fa5eddb9d8488090e51`, and the user's OBS log from 2026-08-24 16:17.
- Produces: a truthful durable baseline that distinguishes the completed functional smoke test from the deferred extended lifecycle checks.

- [ ] **Step 1: Replace the two awaiting status lines with exact CI and manual evidence**

Record that Windows CI succeeded for the exact tested commit, including run/job/artifact identifiers and hashes. Record that OBS 32.2.2 on Windows build 26200 with Ryzen 7 5800X3D and Radeon RX 9070 XT initialized the MediaPipe CPU session twice, reported input `1x144x256x3`, output `1x144x256x2`, allocated both buffers, and survived filter destroy/recreate.

- [ ] **Step 2: Record the bounded acceptance ruling**

State explicitly that the original kernel/session errors are absent and the user confirmed the mask works. Also state explicitly that the requested five-minute observation, source hide/show, and filter disable/enable sequence was not separately confirmed; those checks are deferred to Sprint 8 lifecycle hardening and are not represented as passed.

- [ ] **Step 3: Verify the evidence text**

Run:

```bash
rg -n '2a41a6f|32741918244|32741919482|97478151708|9526045291|1122ff3576fe750a344acb209263eb8f8432ed736fc52fa5eddb9d8488090e51|1x144x256x3|1x144x256x2|deferred|Sprint 8' docs/windows-ml-baseline.md
git diff --check
```

Expected: every evidence identifier and both tensor shapes are present; the deferred checks are unambiguous; `git diff --check` exits zero.

- [ ] **Step 4: Commit the closed baseline**

```bash
git add docs/windows-ml-baseline.md
git -c user.name='Gonçalo Filipe Brigues Gonçalves' -c user.email='goncalogoncalves.02@gmail.com' commit -m 'Record validated Windows CPU baseline'
```

---

### Task 2: Pin and expose the self-contained Windows ML package

**Files:**

- Modify: `buildspec.props`
- Modify: `scripts/download-deps.cmake`
- Create: `tests/WindowsMlSmoke/test_windows_ml_dependency.py`

**Interfaces:**

- Consumes: the exact package URL/hash in Global Constraints and the existing `set_output()` GitHub Actions interface.
- Produces: buildspec keys `windows_ml_version`, `windows_ml_url`, and `windows_ml_sha256`; Windows-only output `WINDOWS_ML_PREFIX` pointing to `.deps/windows-ml`; a behavioral dependency-contract test.

- [ ] **Step 1: RED — add the dependency contract test**

Create a Python `unittest` that copies `buildspec.props` and `scripts/download-deps.cmake` into a temporary fixture, replaces every Windows dependency URL/hash in that fixture with controlled local `file://` zip archives, creates the expected `vendor/obs-studio/.deps` fixture directory, and invokes CMake script mode with `-DWIN32=ON` from the fixture root. Assert the observable result: hash validation, extraction of `build/cmake/microsoft.windows.ai.machinelearning-config.cmake`, and output `WINDOWS_ML_PREFIX=<fixture>/.deps/windows-ml`. The test must fail because no Windows ML dependency is currently downloaded or exposed; it must not assert source text.

Run:

```bash
python3 -m unittest tests/WindowsMlSmoke/test_windows_ml_dependency.py
```

Expected: FAIL for the missing Windows ML output/extraction behavior.

- [ ] **Step 2: GREEN — add the exact buildspec pin and Windows download**

Add the three exact buildspec values. Extend only the Windows branch of `scripts/download-deps.cmake` to download and extract the package as `windows-ml`, then call:

```cmake
set_output(WINDOWS_ML_PREFIX "${CMAKE_SOURCE_DIR}/.deps/windows-ml")
```

Do not add a production test override: the test selects the existing CMake `WIN32` branch and replaces only files inside its temporary fixture.

- [ ] **Step 3: Verify GREEN and regression safety**

Run:

```bash
python3 -m unittest tests/WindowsMlSmoke/test_windows_ml_dependency.py
python3 -m unittest discover -s tests -p 'test_*.py'
git diff --check
```

Expected: all tests pass and no whitespace errors are reported.

- [ ] **Step 4: Commit the dependency pin**

```bash
git add buildspec.props scripts/download-deps.cmake tests/WindowsMlSmoke/test_windows_ml_dependency.py
git -c user.name='Gonçalo Filipe Brigues Gonçalves' -c user.email='goncalogoncalves.02@gmail.com' commit -m 'Pin self-contained Windows ML runtime'
```

---

### Task 3: Build the standalone CPU inference smoke executable

**Files:**

- Create: `tools/windows-ml-smoke/CMakeLists.txt`
- Create: `tools/windows-ml-smoke/cli.hpp`
- Create: `tools/windows-ml-smoke/cli.cpp`
- Create: `tools/windows-ml-smoke/main.cpp`
- Create: `tools/windows-ml-smoke/tests/cli-test.cpp`
- Create: `tests/WindowsMlSmoke/test_windows_ml_smoke.py`

**Interfaces:**

- Consumes: `WINDOWS_ML_PREFIX`, imported targets `WindowsML::Api` and `WindowsML::OnnxRuntime`, and a model path from the CLI.
- Produces: `windows-ml-smoke.exe`, runtime DLLs beside it, deterministic diagnostics, and exit codes `0` success, `2` CLI usage error, `3` unsupported platform/architecture, `4` model/session/inference validation failure.

- [ ] **Step 1: RED — specify the CLI contract with a portable C++ test**

Write table-driven tests with hand-written expected values for: the accepted `--provider cpu --model model.onnx` form; missing provider; non-CPU provider; missing model; duplicate options; and unknown options. The production change each test catches is incorrect acceptance/rejection or loss of the exact model path. Configure a portable `windows-ml-smoke-cli-test` target that does not link Windows ML.

Run:

```bash
cmake -S tools/windows-ml-smoke -B /tmp/obs-br-winml-smoke-red -DWINDOWS_ML_SMOKE_CLI_TESTS_ONLY=ON
cmake --build /tmp/obs-br-winml-smoke-red
ctest --test-dir /tmp/obs-br-winml-smoke-red --output-on-failure
```

Expected: the test executable builds and at least one behavioral assertion fails for the unimplemented parser.

- [ ] **Step 2: GREEN — implement only the accepted Sprint 2 CLI**

Implement a dependency-free parser returning a structured result rather than exiting. Make all Task 3 CLI tests pass. Keep help/usage deterministic and do not accept future Sprint 3/4 commands yet.

- [ ] **Step 3: RED — add the Windows black-box inference test**

Create a Python `unittest` that receives the executable through `WINDOWS_ML_SMOKE_EXE`, invokes invalid argument cases and the valid CPU command with `WINDOWS_ML_SMOKE_MODEL`, and parses `key=value` output. It must assert the exact success contract and numeric invariants from Global Constraints. Run it before `main.cpp` provides inference; on non-Windows it is skipped with an explicit reason, while the portable parser tests still provide local RED/GREEN evidence.

- [ ] **Step 4: GREEN — implement real CPU inference and runtime diagnostics**

In `main.cpp`:

1. Parse arguments and return the specified usage exit code on failure.
2. Report the native Windows version using a version API that is not virtualized by application manifest compatibility, and require `_M_X64`.
3. Report compile-time package version `2.2.12` and `OrtGetApiBase()->GetVersionString()`.
4. Construct `Ort::Env`, CPU-only `Ort::SessionOptions`, and `Ort::Session` from the supplied model.
5. Read the single input/output names, types, and shapes and validate the exact MediaPipe contract.
6. Allocate a zero-filled float input of `110592` values, run exactly one inference, validate output shape, count all finite values, and require `73728` finite values.
7. Measure only the `Session::Run()` call with `std::chrono::steady_clock` and report a nonnegative floating-point `latency_ms`.
8. Print `status=ok` only after every validation succeeds; translate `Ort::Exception` and standard exceptions into `error=<actionable message>` with exit code `4`.

The CMake project must use `find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED)`, link only the two approved imported targets, define `WINDOWS_ML_PACKAGE_VERSION="2.2.12"`, link the Windows version library only if needed, and copy `$<TARGET_RUNTIME_DLLS:windows-ml-smoke>` after build.

- [ ] **Step 5: Verify the portable surface and prepare the Windows gate**

Run:

```bash
cmake -S tools/windows-ml-smoke -B /tmp/obs-br-winml-smoke -DWINDOWS_ML_SMOKE_CLI_TESTS_ONLY=ON
cmake --build /tmp/obs-br-winml-smoke
ctest --test-dir /tmp/obs-br-winml-smoke --output-on-failure
python3 -m unittest discover -s tests -p 'test_*.py'
git diff --check
```

Expected: portable tests pass; Windows-only black-box test is explicitly skipped locally; all other tests pass.

- [ ] **Step 6: Commit the smoke tool**

```bash
git add tools/windows-ml-smoke tests/WindowsMlSmoke/test_windows_ml_smoke.py
git -c user.name='Gonçalo Filipe Brigues Gonçalves' -c user.email='goncalogoncalves.02@gmail.com' commit -m 'Add Windows ML CPU inference smoke tool'
```

---

### Task 4: Run the CPU smoke gate early in Windows CI

**Files:**

- Modify: `.github/workflows/build-windows.yml`
- Create (ignored): `.superpowers/windows-results/sprint-2/windows-cpu-smoke-handoff.md`

**Interfaces:**

- Consumes: `WINDOWS_ML_PREFIX`, `windows_sdk_version`, the standalone smoke CMake project, and the tracked MediaPipe model.
- Produces: an early Windows x64 Release build/test gate whose log contains the complete `key=value` report before OBS and standalone ONNX Runtime compilation begin.

- [ ] **Step 1: Add the early configure/build/test steps**

Immediately after Python setup and before `Configure OBS Studio (Windows)`, launch the VS x64 developer shell and add three named steps:

1. Configure `tools/windows-ml-smoke` into `build_windows_ml_smoke` with Ninja, `CMAKE_BUILD_TYPE=Release`, `CMAKE_SYSTEM_VERSION` from buildspec, and `CMAKE_PREFIX_PATH` equal to `WINDOWS_ML_PREFIX`.
2. Build `windows-ml-smoke` in Release.
3. Run `ctest --test-dir build_windows_ml_smoke --output-on-failure`, then run `tests/WindowsMlSmoke/test_windows_ml_smoke.py` with `WINDOWS_ML_SMOKE_EXE` set to the built executable and `WINDOWS_ML_SMOKE_MODEL` set to the absolute tracked model path.

The valid command must appear in the job log and fail the job on any nonzero exit. Do not alter the existing plugin ORT or packaging steps.

- [ ] **Step 2: Create the Sprint 2 CI evidence handoff**

Write the exact PowerShell/GitHub CLI commands needed to verify, for the tested commit: Check CI success, `build-windows-x64 / build` success, the three new smoke step conclusions, and log lines containing `status=ok`, `provider=cpu`, package/runtime versions, exact shapes, `finite_output_count=73728`, and `latency_ms`.

- [ ] **Step 3: Run local static and regression checks**

Run:

```bash
python3 -m unittest discover -s tests -p 'test_*.py'
cmake -S tools/windows-ml-smoke -B /tmp/obs-br-winml-smoke-final -DWINDOWS_ML_SMOKE_CLI_TESTS_ONLY=ON
cmake --build /tmp/obs-br-winml-smoke-final
ctest --test-dir /tmp/obs-br-winml-smoke-final --output-on-failure
git diff --check
```

Expected: all local executable tests pass, Windows-only black-box inference is skipped with its explicit reason, and no whitespace errors exist.

- [ ] **Step 4: Commit the Windows CI gate**

```bash
git add .github/workflows/build-windows.yml
git -c user.name='Gonçalo Filipe Brigues Gonçalves' -c user.email='goncalogoncalves.02@gmail.com' commit -m 'Run Windows ML CPU smoke test in CI'
```

---

## Sprint 2 Acceptance Gate

The sprint is ready for external validation only after every task has an independent clean task review and a clean whole-branch review. The controller then pushes the approved feature branch and observes the existing draft PR with `windows-only-ci` and `upload-artifacts` still applied.

Pass requires all of the following for one exact commit:

1. `Check CI` succeeds.
2. `build-windows-x64 / build` succeeds on `windows-2025-vs2026`.
3. The standalone tool configures and builds x64 Release against Windows ML 2.2.12.
4. Portable CLI tests and the Windows black-box test pass.
5. The log records `status=ok`, `provider=cpu`, `architecture=x64`, `windows_ml_package_version=2.2.12`, a nonempty `onnxruntime_version`, MediaPipe input `1x144x256x3`, output `1x144x256x2`, `iterations=1`, `finite_output_count=73728`, and nonnegative `latency_ms`.
6. The existing plugin build/package still succeeds unchanged.
7. No GPU provider support is claimed or exposed yet.

The exact Windows ML-provided ONNX Runtime version captured by this gate becomes the compatibility baseline for Sprint 3.

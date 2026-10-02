<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Sprint 6 Windows ML Sessions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (established project method) or superpowers:executing-plans if the owner explicitly chooses a different method. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate CPU, DirectML and MIGraphX into Windows production sessions with explicit, safe CPU fallback and exact-artifact RX 9070 XT validation.

**Architecture:** Share the existing provider module between plugin and smoke consumers, adding a small Windows session core with portable request policy. `createOrtSession()` adapts its result to existing model state and OBS logs. A standalone acceptance executable uses the same production core while existing smoke GPU benchmarks retain their strict behavior.

**Tech Stack:** C++20, self-contained Windows ML 2.2.12 / embedded ONNX Runtime 1.25.2, CMake, Python unittest, CTest, PowerShell and existing OBS/OpenCV dependencies. Non-Windows standalone ONNX Runtime remains 1.28.0.

**Spec:** [Approved Sprint 6 design](../specs/2026-10-02-windows-ml-amd-sprint-6-design.md).

**Status:** Plan awaiting owner review; implementation has not started.

## Global Constraints

- Windows ML **2.2.12**; Linux/macOS standalone ONNX Runtime **1.28.0**. Pinned package headers govern API syntax; do not upgrade dependencies to match newer examples.
- Stored identifiers: `cpu`, `winml-directml`, `winml-migraphx`; runtime names: `CPUExecutionProvider`, `DmlExecutionProvider`, `MIGraphXExecutionProvider`. Preserve legacy non-Windows identifiers.
- GPU model eligibility: exact tracked `data/models/mediapipe.onnx` / `MODEL_MEDIAPIPE`. Other recognized GPU/model requests use CPU with `unsupported_model`; unknown Windows identifiers fail.
- GPU options: `ORT_ENABLE_ALL`, `DisableMemPattern()`, `ORT_SEQUENTIAL`, `session.disable_cpu_ep_fallback=1`. Whole-session fallback uses new CPU-only options and existing CPU inter/intra-op thread settings.
- Never acquire/install providers from OBS. `NotPresent` and `Unknown` never reach `EnsureReady`; `NotReady` activates only the installed package and must become `Ready` before registration.
- Package exactly one copy each of Windows ML API DLL, `onnxruntime.dll`, and `DirectML.dll` from the pinned package beside the plugin; preserve license/notices. No GPU EP package redistribution.
- Keep preprocessing, postprocessing and rendering provider-independent. Preserve model-lock serialization; sessions must die before their owning environment. No provider UI, automatic GPU cascade, new inference recovery, loader hooks or search-path changes.
- GPU correctness: 100 calls after 10 warm-ups, finite output, input `1x144x256x3`, output `1x144x256x2`, normalized mean absolute error **<= 0.05**, binary-mask IoU **>= 0.95**. Record latency without adding a minimum speedup requirement.
- Fresh task-scoped implementer and independent review per task, then whole-sprint review. All commits use the owner's identity, `-s -S`, key `460B18400D17462FF3714A2BDCED30418A1C06FC`, and no assistant/co-author attribution.
- Work in `.superpowers/worktrees/windows-ml-amd-sprint-6` on `feature/windows-ml-amd-sprint-6`, based on accepted source `74b0498d197288e54f932b818bb9e857b984db7f`. Preserve `GPU`, the accepted Sprint 5 branch and the owner's separate roadmap. Temporary evidence belongs under primary-worktree `.superpowers/sdd/2026-10-02-windows-ml-amd-sprint-6/`.

## Review Focus

1. Saved unknown/legacy Windows provider IDs must produce controlled failure, without creating a silently labelled CPU session (Task 1, Task 4).
2. A failed GPU candidate may have modified options; CPU fallback must not inherit any GPU attachment, configuration marker or CPU prohibition (Task 2).
3. Installed-provider activation can succeed while readiness remains `NotReady`; registration must still be blocked (Task 1, Task 2).
4. Reinitialization can fail after session construction or reuse previous diagnostics; no stale session, tensor metadata, GPU device or effective-provider claim may remain (Task 4).
5. DirectML may be missing, tampered, duplicated under another case/path, or unreadable inside the ZIP; reject the package with one actionable contract error (Task 3, Task 5).

## File structure and responsibility

| Files | Responsibility |
| --- | --- |
| New `src/ort-utils/windows-ml-session-policy.hpp/.cpp` | Portable logical request mapping, model eligibility and owned session diagnostics |
| Existing `src/ort-utils/windows-ml-provider-policy.hpp/.cpp` | Readiness decisions and a testable installed-activation gate |
| Existing `src/ort-utils/windows-ml-provider.hpp/.cpp` | Shared exact EP/device attachment; add structured failing-stage diagnostics |
| New `src/ort-utils/windows-ml-session.hpp/.cpp`, `windows-ml-session-internal.hpp` | Production native session construction/fallback; internal fault-injection seam for native tests |
| New `cmake/windows_ml_sessions.cmake` | Shared policy/core CMake targets for plugin and smoke builds |
| Existing `cmake/onnxruntime_backend.cmake`, `CMakeLists.txt` | Validated DirectML deployment, Windows-only linking and existing plugin install layout |
| Existing `src/consts.h`, `FilterData.hpp`, `ort-utils/ORTModelData.hpp`, `ort-utils/ort-session-utils.hpp/.cpp` | Logical constants, runtime state, ordered cleanup and OBS initialization adapter |
| Existing `src/background-filter.cpp`, `src/enhance-filter.cpp` | Preserve locked initialization/inference and honour session failures in both callers |
| Existing `scripts/verify_windows_ml_package.py`, `tests/WindowsMlBuild/*.py` | DirectML uniqueness/location/origin and build-contract regression tests |
| Existing `tools/windows-ml-smoke/CMakeLists.txt`; new tests and `production-session-check.cpp` beneath it | Portable policy tests, native core tests and production-core hardware harness |
| New `tests/WindowsMlSession/CMakeLists.txt`, `plugin-session-test.cpp`, `obs-boundary-stubs.cpp`; existing `tests/CMakeLists.txt` | Windows native plugin-adapter and metadata/lifetime tests |
| New `scripts/package_windows_ml_session_check.ps1`, `tests/WindowsMlSession/test_session_artifact.py`, `test_production_session_check.py` in that test directory; existing `.github/workflows/build-windows.yml` | Verified dedicated harness artifact, CLI outcome tests and native CI wiring |
| New `docs/windows-ml-session-integration.md`; existing durable handoff | Exact commands, acceptance receipts and bounded support claims |

## Execution and verification conventions

Each task performs red/green verification, self-review, independent review, corrections and a scoped signed/DCO commit before the next dependent task. Stage only its listed files. Native Windows checks unavailable on Linux are explicitly pending until exact-commit CI; a skipped test is not a pass.

New policy/session-core APIs use namespace `windows_ml`; OBS adapter helpers retain the existing global style. Register each new test target while adding its failing test, before running the red check. Use the existing smoke-test `expect`/failure-return style so Release builds do not disable checks through `NDEBUG`; do not use bare C/C++ `assert` as the test mechanism. CTest selections must fail when no matching test is registered.

Commands below run from the Sprint 6 worktree. Ensure a project-compatible `cmake`, `ctest`, Ninja, compiler, Python and existing lint tools are on PATH at execution time. Linux can use the already-provisioned CMake 3.31.6 if still present; do not install product dependencies merely to write/review this plan. Native Windows uses the existing workflow's MSVC developer shell, downloaded pinned package and SDK settings.

Portable policy configure/build/test commands:

```sh
cmake -S tools/windows-ml-smoke -B .superpowers/build-s6-policy -G Ninja -DWINDOWS_ML_SMOKE_CLI_TESTS_ONLY=ON
cmake --build .superpowers/build-s6-policy
ctest --test-dir .superpowers/build-s6-policy --output-on-failure --no-tests=error
```

Native smoke/core build directory is existing `build_windows_ml_smoke`; plugin/native-adapter tests use existing root `build`. Existing `.github/workflows/build-windows.yml` supplies exact package paths and toolchain commands. Register new CTest names specified below in those builds. Expected green output: all selected tests pass, zero failures; unavailable AMD hardware is not required for native CPU/fault tests.

### Task 1: Portable session request and installed-activation policy

**Files:** Create session-policy header/source and `cmake/windows_ml_sessions.cmake`; modify provider-policy header/source and smoke CMake; create `tools/windows-ml-smoke/tests/session-policy-test.cpp`; extend `tests/provider-policy-test.cpp` there.

**Interfaces:**

- `SessionRequest { std::string requested_provider; bool gpu_model_eligible; int cpu_threads; }`.
- `SessionRoute { Cpu, Gpu, Reject }`; `ResolvedSessionRequest { SessionRoute route; std::string requested_runtime_provider; std::string runtime_provider; std::string fallback_reason; std::string error; }`.
- `ResolvedSessionRequest resolve_session_request(const SessionRequest&)`: CPU maps directly; eligible GPU maps exactly; recognized ineligible GPU maps to CPU/`unsupported_model`, retaining the requested runtime name. Unsupported IDs and negative `cpu_threads` are rejected; the plugin adapter additionally rejects unsigned thread counts above `INT_MAX` before conversion. Zero preserves ORT's default thread behavior.
- `SessionDiagnostics` owns string fields `requested_provider`, `requested_runtime_provider`, `effective_provider`, `fallback_reason`, `error`, `cpu_error`, and `std::optional<ProviderSessionResult> provider_attempt`. Effective CPU has no active GPU device; an attempted device remains only within `provider_attempt`. Define `SessionOutcome { NotInitialized, Constructed, Ready, Failed }` and field `SessionOutcome outcome`; core construction alone cannot claim plugin readiness.
- `InstalledProviderActivation` abstract interface: `void activate()` and `ProviderReadyState read_state()`; `ProviderReadyState ensure_installed_provider_ready(ProviderReadyState initial, InstalledProviderActivation&)`. Exceptions propagate to the provider module's existing error boundary.
- CMake functions `add_windows_ml_policy_targets()` and, from Task 2, `add_windows_ml_session_core()`. Policy targets: `windows-ml-provider-policy` and `windows-ml-session-policy`; preserve existing smoke provider-policy target name through an alias, avoiding duplicate definitions.

- [ ] **Step 1: Add failing request-policy tests.** `cpu_skips_gpu`, `exact_gpu_mapping`, `unsupported_model_uses_cpu`, `unknown_windows_identifier_rejected`, `invalid_thread_count_rejected`: check exact names/reasons, both GPU identifiers, empty/legacy/invalid IDs, negative threads and zero/default threads, and no mutation of the stored request. Use `expect(resolve_session_request({"winml-directml", true, 1}).runtime_provider == "DmlExecutionProvider", "DirectML exact mapping")` and equivalent MIGraphX/CPU checks.
- [ ] **Step 2: Add failing readiness call-count tests.** `not_present_never_activates`, `unknown_never_activates`, `ready_never_activates`, `not_ready_requires_ready_after_activation`, `activation_exception_propagates`: fake counts `activate` and `read_state`; assert unavailable states cause zero calls, `NotReady` causes one of each, and a remaining non-Ready state is returned as unusable.
- [ ] **Step 3: Run the portable commands above.** Expect missing interfaces/target or assertion failures attributable to the new contracts; retain the red result.
- [ ] **Step 4: Implement the specified policy and shared policy targets.** No OBS, Windows API or runtime acquisition dependency. Owned diagnostics can include existing provider result types, whose header itself is portable. Add CTest `windows-ml-session-policy`; retain all existing portable tests.
- [ ] **Step 5: Re-run portable CTest.** Expect all tests pass, including activation call-count cases; lint new C++ and CMake files.
- [ ] **Step 6: Independent review and signed commit:** `Add Windows ML session request and activation policy`.

### Task 2: Native production session core and provider diagnostics

**Files:** Create session header/source/internal header and `tools/windows-ml-smoke/tests/session-core-test.cpp`; modify provider header/source, shared CMake and smoke CMake.

**Interfaces:**

- `SessionCreationResult { std::unique_ptr<Ort::Session> session; SessionDiagnostics diagnostics; }`.
- Public `SessionCreationResult create_session(Ort::Env&, const std::filesystem::path&, const SessionRequest&)` calls the production configuration/constructor operations and catches initialization errors into the result. Environment ownership remains with the caller.
- Internal `SessionOperations`: virtual `ProviderSessionResult configure(Ort::Env&, Ort::SessionOptions&, std::string_view exact_name)` and `std::unique_ptr<Ort::Session> construct(Ort::Env&, const std::filesystem::path&, const Ort::SessionOptions&)`; exact coordinator signature is `SessionCreationResult create_session_with_operations(Ort::Env&, const std::filesystem::path&, const SessionRequest&, SessionOperations&)`. It is a test seam, not a plugin/UI option.
- Add `ProviderFailureStage { None, Discovery, Activation, Registration, DeviceSelection, Attachment }` and `failure_stage` to `ProviderSessionResult`; set stage at actual boundaries, reset to `None` on success. Map these stages to the specification's stable reason categories; do not parse human error text.
- `add_windows_ml_session_core()` creates `windows-ml-session-core`, links both policy targets and `WindowsML::Api` / `WindowsML::OnnxRuntime`, and compiles the shared provider source once per consuming build. Guard existing `NOMINMAX` / `WIN32_LEAN_AND_MEAN` definitions against the plugin's `/WX` configuration.

- [ ] **Step 1: Add failing native tests with real pinned CPU runtime and injected GPU operations.** Cover direct CPU, each exact GPU dispatch, every provider failure stage, thrown attachment/configuration error, failed or null GPU constructor, successful CPU fallback, unsupported model, and CPU/double failure. Injected GPU success is explicitly a control-flow test, never hardware evidence.
- [ ] **Step 2: Pin fresh-options behavior.** `gpu_failure_builds_clean_cpu_options`: the fake GPU configuration adds `sprint6.test.gpu_candidate=1`; injected GPU construction checks `GetConfigEntry("session.disable_cpu_ep_fallback") == "1"` then throws. CPU construction asserts both keys are absent, creates an actual MediaPipe CPU session, and returns it. Assert one GPU attempt, one CPU attempt, effective `CPUExecutionProvider`, retained requested ID and original failure; no active GPU claim. Test an unusable activation result never proceeds to registration via Task 1's shared gate.
- [ ] **Step 3: Build with `cmake --build build_windows_ml_smoke --config Release` and run `ctest --test-dir build_windows_ml_smoke -R windows-ml-session-core --output-on-failure --no-tests=error` on Windows.** Expect red failures before implementation; CPU-only tests must not rely on a GPU/provider installed on the runner.
- [ ] **Step 4: Implement the native coordinator and actual provider-stage recording.** Wire `ensure_installed_provider_ready()` into the catalog configuration path using an installed-provider adapter over existing `WinMLEpEnsureReady` and readiness reads. Preserve the visible-device-first path, exact AMD selection, owned diagnostics and explicit external preparation. Discard GPU options before fresh CPU options; perform one fallback only. Validate a regular nonempty model file before GPU setup. Return outcome `Constructed` on successful construction; no success log here.
- [ ] **Step 5: Run native core CTest and existing smoke inference/metadata tests.** Verify corrupt/missing models and constructor exceptions return failure/null session, and strict existing GPU CLI semantics are unchanged. Retain real-runtime and injected-path evidence separately.
- [ ] **Step 6: Independent review and signed commit:** `Add shared Windows ML session construction and fallback`.

### Task 3: DirectML deployment and package validation

**Files:** Modify backend CMake, root CMake, shared core CMake/smoke staging, package verifier, `test_onnxruntime_backend.py`, `test_windows_ml_package.py`; update only affected fixtures in `test_onnxruntime_provider_detection.py` / `test_windows_ml_legal.py` if their pinned-package fixtures require it.

**Interfaces:** Backend additionally exports `WINDOWS_ML_DIRECTML_DLL`: canonical `<validated package root>/runtimes/win-x64/native/DirectML.dll`. Preserve existing backend link interface and legal exports. `verify_install_tree()` / `verify_archive()` retain their existing signatures and error format.

- [ ] **Step 1: Replace the obsolete DirectML-prohibition fixture with the new valid package layout.** Add `test_missing_directml_is_rejected`, `test_tampered_directml_is_rejected`, `test_duplicate_or_misplaced_directml_is_rejected`, `test_archive_only_directml_corruption_names_entry`. Test tree and ZIP independently, including mixed-case duplicates and CRC/decompression failures; expect exit 1, one `package-contract-error`, entry name and no traceback.
- [ ] **Step 2: Add failing backend tests for DirectML path export, missing/empty DLL and cached reconfiguration.** Keep existing non-Windows targets and configuration-less Windows ML imported mappings unchanged. Run `python3 -m unittest discover -s tests/WindowsMlBuild -v`; expect failures restricted to the new deployment contract.
- [ ] **Step 3: Implement validation and installation.** Check the required pinned DLL before persisting validated state, export its canonical path, and install it once into `${OBS_PLUGIN_BIN_DIR}`. Advance the validated-state schema from `1` to `2`, update schema fixtures, and reject a historical `1` marker without fresh explicit package selection; this prevents accepting a weaker cached contract. Preserve strict version, directory, redirect, marker-type, regeneration and legal validation.
- [ ] **Step 4: Implement tree/ZIP uniqueness and package-byte comparison for DirectML.** Preserve single ORT, API/legal hashes, safe paths and expected archive-read error conversion. Explicitly copy pinned DirectML beside every native smoke/core-test executable; `WindowsML::DirectML` is an interface target and is not sufficient for runtime copying.
- [ ] **Step 5: Run the complete build-contract suite, portable tests and native Windows configure/build/install/package verifier.** Re-run relevant CMake generator fixtures with Ninja as well as the default generator. Expect zero failures and all three runtime DLLs adjacent and byte-identical to the pinned package. Native deployment proof remains pending until Windows CI runs the actual root project.
- [ ] **Step 6: Independent review and signed commit:** `Deploy and verify pinned DirectML runtime`.

### Task 4: Plugin session adapter, cleanup and truthful logging

**Files:** Modify constants, FilterData, ORTModelData, session-utils header/source, root CMake, background/enhance callers and tests CMake; create the WindowsMlSession native adapter-test files listed above.

**Interfaces:**

- Add constants `USEGPU_WINML_DIRECTML = "winml-directml"`, `USEGPU_WINML_MIGRAPHX = "winml-migraphx"`; keep all existing constants.
- Add Windows-only `windows_ml::SessionDiagnostics sessionDiagnostics` to `filter_data`.
- Public `int createOrtSession(filter_data*)` and existing return codes remain. A Windows helper `int createWindowsMlOrtSession(filter_data*, const std::filesystem::path&)` performs the same production initialization with a resolved path, allowing native adapter tests to bypass OBS module-file lookup, not session policy.
- `void resetOrtSessionData(ORTModelData&) noexcept` clears tensor handles, names, dimensions, session and backing buffers in safe ownership order, without destroying `env`. Declare `env` before `session` and backing buffers before their referencing tensor members so default destruction also respects ownership. Retain existing field names/model APIs.
- `void logWindowsMlSessionOutcome(const windows_ml::SessionDiagnostics&, std::string_view model)` logs a single initialization outcome; intermediate `Constructed` is never reported as plugin success.

- [ ] **Step 1: Add failing Windows adapter tests.** `cpu_initializes_real_mediapipe`, `bad_metadata_clears_session_and_diagnostics`, `allocation_exception_is_controlled`, `unknown_identifier_after_success_clears_old_state`, `unsupported_model_cpu_fallback_is_logged`, `reinitialization_clears_previous_fallback`, `repeated_init_and_teardown`. Derive test Models overriding existing metadata/buffer virtual methods to return false or throw; after failure assert null session, empty names/dims/tensors/buffers and outcome `Failed` with empty effective provider. Repeat 64 CPU construction/initialization/destruction cycles using the real model/runtime.
- [ ] **Step 2: Build/register `windows-ml-plugin-session` under `WIN32 AND BUILD_TESTING`.** Compile the production session-utils source with existing OBS/OpenCV/backend targets and shared core. Test-only `obs-boundary-stubs.cpp` defines `extern "C" void obs_log(int, const char*, ...)` to capture logs and the module accessor required by pinned OBS headers; link actual libobs for its memory/API functions. Stage target runtime DLLs, including libobs and explicit pinned DirectML, beside the test executable. The path helper avoids module lookup during these tests. Build with `cmake --build build --target windows-ml-plugin-session --config RelWithDebInfo`, then run `ctest --test-dir build -R windows-ml-plugin-session --output-on-failure --no-tests=error`; expect red failures before adapter changes.
- [ ] **Step 3: Connect the Windows adapter to `create_session()`.** Clear old session data at initialization boundaries, check model/environment/path and representable thread count, pass exact MediaPipe eligibility, initialize metadata/tensors within the exception boundary, and publish `Ready` only after completion. On any later failure, clear state and return the relevant existing error code; do not convert metadata/allocation failure into provider fallback.
- [ ] **Step 4: Preserve caller locking and error behavior.** Existing background and enhancement update/inference paths both use their own `modelMutex`; do not assume it belongs to `filter_data`. Background stays disabled on failure. Enhancement must check the formerly ignored `createOrtSession()` result, stay disabled and avoid using failed state; recover through successful explicit reinitialization. An explicit settings update must permit retry when the previous session is null even if the requested settings are unchanged. Neither activation callback may re-enable a failed/null session. Do not alter rendering/preprocessing or add UI choices. Review all actual initialization/inference call sites for serialized access, including an inference already waiting when initialization fails.
- [ ] **Step 5: Add outcome logging and verify it through captured native test logs.** Requested/effective/model, readiness/activation/registration, GPU device IDs, original provider stage/error/HRESULT and separate CPU error must be unambiguous. Test built-in device success without catalog registration, CPU request with no fallback, and failed metadata after a constructed candidate. Run adapter/core/portable tests and compile the actual Windows plugin with existing `/WX`; run existing non-Windows configuration contracts without Windows-only headers or sources leaking into their builds.
- [ ] **Step 6: Independent review and signed commit:** `Integrate Windows ML sessions into plugin initialization`.

### Task 5: Production-core acceptance harness and exact-commit CI artifacts

**Files:** Create `tools/windows-ml-smoke/production-session-check.cpp`, `tests/WindowsMlSession/test_session_artifact.py`, `tests/WindowsMlSession/test_production_session_check.py`, `scripts/package_windows_ml_session_check.ps1`; modify smoke CMake and Windows workflow. Extend existing smoke dependency/native tests only where needed for regression coverage.

**Interfaces:**

- Executable `windows-ml-session-check.exe --provider <cpu|winml-directml|winml-migraphx> --model <path> --iterations 100 --cycles 3 --require-effective`.
- Each cycle creates its own environment and production-core session, runs deterministic MediaPipe tensors, destroys session before environment, and copies result diagnostics/outputs into owned report values. GPU comparison uses a separate CPU session through the same core; reuse existing deterministic input and comparison/latency helpers, never the smoke runner's separate session-dispatch implementation.
- Exit codes: `0` all checks passed, `2` invalid arguments, `3` requested GPU fell back/unavailable, `4` initialization/inference/correctness failure. `--require-effective` demands the requested runtime provider; a production fallback is reported accurately but cannot pass GPU acceptance. No fault-injection CLI.
- Report includes `requested_provider`, `effective_provider`, fallback cause, device/vendor IDs, package/runtime versions, input/output shapes, finite count, iteration/cycle counts, MAE/IoU, latency and `status=ok|error`. Do not invent Windows/driver versions; acceptance records them from the actual machine.
- PowerShell packaging script parameters: `BuildDirectory`, `ModelPath`, `WindowsMlRoot`, `SourceCommit`, `DestinationZip`. It validates exact files/hashes before and after ZIP creation and creates dedicated `windows-ml-session-check_2.2.12_x64.zip`, containing the executable, tracked model, three pinned runtime DLLs, legal files and a source-SHA/hash manifest. Keep the existing strict smoke ZIP's one-executable layout unchanged.

- [ ] **Step 1: Add native harness tests for invalid provider/model/counts and real CPU success across three cycles.** `test_production_session_check.py` executes the harness, using `WINDOWS_ML_SESSION_CHECK_EXE` / `WINDOWS_ML_SESSION_CHECK_MODEL` set by CI. Assert 110592 input floats, 73728 finite output floats, exact shapes, requested/effective CPU, 100 timed calls after 10 warm-ups per cycle, exit 0; invalid arguments exit 2 and missing/corrupt model exit 4. Register `windows-ml-session-check-cpu` in native CTest. Keep GPU tests outside generic CI; use Task 2 injection tests for deterministic fallback faults.
- [ ] **Step 2: Add executable packaging-script tests.** Python invokes `pwsh` against temporary fixtures: valid ZIP/manifest origins, missing/changed DirectML, stale destination and invalid source SHA. Assert rejection rather than stale artifact upload. Native test command: `python -m unittest discover -s tests/WindowsMlSession -p 'test_*.py' -v`; on Windows a missing executable or `pwsh` must fail, while Linux reports an explicit platform skip. Run new tests first and retain red results.
- [ ] **Step 3: Implement harness and packaging using the stated interfaces.** Use `create_session()` for every production session; reuse comparison helpers with foreground channel 1, channel count 2 and threshold 0.5. Enforce MAE/IoU/finite/shape gates, not speedup. Do not copy dispatch or allow silently successful GPU fallback.
- [ ] **Step 4: Wire native Windows CI.** Run policy/core/harness tests in smoke build, adapter tests in root build, Python artifact tests with explicit native exit checks, and existing contract/smoke tests. Set needed executable/model environment paths locally in each step. Package/upload the dedicated harness alongside the verified plugin ZIP only after checks pass, with source SHA and SHA-256 receipts. Reuse pinned workflow actions and existing dependency setup. No AMD success claim from a generic runner.
- [ ] **Step 5: Verify local available suites/format/REUSE, independently review this task and sign its commit:** `Validate production Windows ML sessions in CI`. Obtain whole-sprint independent review; resolve findings with scoped regressions/re-review and signed fix commits. Then run exact-head Windows CI and retain Check CI / PR Check URLs, source SHA, run IDs and artifact IDs/hashes. The new isolated branch is not automatically covered by PR #1: do not update its accepted Sprint 5 head or create a new PR without explicit authorization. Prepare a concrete draft PR preview or use another authorized exact-ref CI path; if neither is available, report the native gate pending.
- [ ] **Step 6: Verify downloaded exact-head plugin and harness ZIPs independently against the pinned NuGet/model and manifests.** Check path safety, unique runtime files, legal origin and manifest source SHA before owner testing. Native failures return to the owning task with a red regression, signed fix, scoped review and new exact-head CI run.

### Task 6: RX 9070 XT acceptance and durable handoff

**Files:** Create `docs/windows-ml-session-integration.md`; update relevant Sprint 6 entries in `docs/windows-ml-amd-session-handoff.md` on the documentation branch only after acceptance. Scratch receipt/logs remain ignored.

**Interfaces:** An immutable receipt names reviewed source SHA, actual CI runs/artifact IDs and ZIP hashes, installed plugin backup location, each provider's effective outcome/device/correctness, versions, and owner OBS CPU result. No support claim substitutes historical Sprint 4 results for this receipt.

- [ ] **Step 1: Write the exact-artifact owner test handoff.** Supply independent installation/extraction, provider-preparation and log-collection blocks, each defining its own paths and checking native `$LASTEXITCODE` on Windows PowerShell 5.1 as well as PowerShell 7. Fill actual delivered ZIP names/hashes and source SHA from Task 5; do not ask the owner to guess variables or copy an earlier `$work`. Back up the existing plugin outside OBS search paths and document removal/restoration using that recorded backup. Keep the received original logs local pending privacy review.
- [ ] **Step 2: Prepare providers outside OBS and capture readiness.** In the verified extracted harness/smoke directories, explicitly run the existing smoke `--prepare-provider MIGraphXExecutionProvider` when required and collect `--list-providers` output. DirectML's built-in route may need no catalog preparation; do not treat that as failed registration. No OBS acquisition or automatic downloads during filter loading.
- [ ] **Step 3: Run these acceptance invocations from the verified harness directory.** The delivered PowerShell block wraps each invocation in exit-code capture and stores logs in a newly defined output directory; expected exit 0, exact effective provider, three successful cycles, shapes/finite/correctness gates passed:

```powershell
& .\windows-ml-session-check.exe --provider cpu --model .\mediapipe.onnx --iterations 100 --cycles 3 --require-effective
& .\windows-ml-session-check.exe --provider winml-directml --model .\mediapipe.onnx --iterations 100 --cycles 3 --require-effective
& .\windows-ml-session-check.exe --provider winml-migraphx --model .\mediapipe.onnx --iterations 100 --cycles 3 --require-effective
```

- [ ] **Step 4: Check the exact plugin package in OBS.** Owner confirms visible MediaPipe CPU mask, removes/recreates the filter, confirms working mask again and closes OBS. Collect fresh logs independently and verify requested/effective CPU, normal initialization/destruction and absence of relevant crashes/load/session errors. GPU live OBS masks and interactive switching remain Sprint 7, not an omitted Sprint 6 acceptance claim.
- [ ] **Step 5: Evaluate gates without broadening scope.** GPU fallback, bad correctness or crash blocks that provider's Sprint 6 acceptance; retain failure cause, investigate with systematic debugging and re-run only affected checks after a fix, including exact-head CI if source changes. Loader-resolution failure requires separate design review. Report native Linux/macOS checks accurately; full platform release matrix, long-duration and clean-machine release acceptance remain later gates.
- [ ] **Step 6: Record sanitized evidence and completion.** Only after both GPU routes, CPU and package/OBS checks pass, update durable evidence and handoff; review documentation diff, REUSE, owner signature/DCO and remote readback for scope-authorized publication. Keep feature implementation separate from `GPU` documentation. Signed documentation commit subject: `Record Sprint 6 session and hardware acceptance`.

## Plan self-review and execution handoff

Coverage: Task 1 maps identifiers/readiness; Task 2 owns native provider/session fallback; Task 3 owns DirectML packaging and cached validation; Task 4 owns plugin publication/cleanup/errors/logs; Task 5 proves the same core and exact-commit artifacts; Task 6 owns hardware/OBS acceptance and receipts. All five Review Focus conditions have explicit tests. No product UI, provider acquisition in OBS, model expansion, performance promise or loader workaround is introduced.

The established execution method is fresh task-scoped implementers with independent task reviews and a final whole-sprint review. The owner reviews this written plan before execution. Use the approved worktree, preserve accepted branches, and keep pending native/hardware gates explicit rather than declaring completion from portable green checks.

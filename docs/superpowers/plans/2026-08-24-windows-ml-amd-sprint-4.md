# Windows ML AMD Support — Sprint 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove that the tracked MediaPipe model can execute repeatedly through MIGraphX or DirectML on the RX 9070 XT with valid output, CPU-equivalent masks, and lower average latency than CPU.

**Architecture:** Extend only the standalone Windows ML smoke tool and the OBS-independent Windows provider helper. Keep deterministic input generation and comparison mathematics portable, keep provider acquisition outside inference, activate only already-installed `NotReady` providers in the current process, and attach an exact-name AMD GPU EP device to a new ONNX Runtime session. OBS and plugin session creation remain unchanged in this sprint.

**Tech Stack:** C++20, CMake, Microsoft.Windows.AI.MachineLearning 2.2.12, ONNX Runtime C++ API, WinMLEpCatalog C API, Python 3.9 `unittest`, GitHub Actions, PowerShell.

**Spec:** `docs/superpowers/specs/2026-08-24-windows-ml-amd-design.md`

## Global Constraints

- Target the self-contained `Microsoft.Windows.AI.MachineLearning` package `2.2.12` and its bundled ONNX Runtime; do not introduce another ONNX Runtime into the smoke process.
- Use only the tracked `data/models/mediapipe.onnx` model with input `1x144x256x3` (`110592` floats) and output `1x144x256x2` (`73728` floats).
- Preserve the legacy command and output contract for `--provider cpu --model <path>` with one timed inference and no warm-up.
- New benchmark commands use exactly 10 untimed warm-up calls and the requested number of timed calls; the RX 9070 XT gate uses exactly 100 timed calls.
- Deterministic input is generated locally from the fixed seed `0x6d2b79f5`; no webcam, image decoder, network access, or mutable external fixture participates in comparison.
- A `NotPresent` provider never reaches `WinMLEpEnsureReady` during inference. It reports controlled unavailability and exits `5`.
- A `NotReady` provider may call `WinMLEpEnsureReady` only to activate the already-installed provider in the current process, must be re-read as `Ready`, and is then registered. The standalone `--prepare-provider` command remains the only path allowed to acquire a `NotPresent` provider.
- `DmlExecutionProvider` may be selected directly from already-visible EP devices without catalog acquisition or registration when ONNX Runtime exposes it as a built-in device.
- Provider selection is exact-name, GPU-only, AMD vendor ID `0x1002`, deterministic by ascending vendor ID then device ID, and never silently substitutes another EP.
- GPU benchmark failure never falls back inside the smoke tool; it must remain observable. CPU fallback belongs to the later plugin integration sprint.
- Every output value must be finite and the returned tensor must retain the exact expected type, count, and shape on every timed call.
- Comparison uses mean absolute error over all `73728` output floats. Because MediaPipe output values are probabilities, normalized MAE is that mean on the `[0,1]` scale and must be at most `0.05`.
- Binary-mask IoU uses foreground channel `1`, threshold `>= 0.5`, and must be at least `0.95`; if both masks have an empty union, IoU is `1.0`.
- A candidate passes only when it completes 100 timed calls, meets both correctness thresholds, and its average timed latency is strictly lower than CPU for the same process, model, and input.
- All diagnostics are unique `key=value` lines, strings are single-line sanitized, decimal floating-point values use six digits after the decimal point, and stdout ends with `status=ok`, `status=unavailable`, or `status=failed` for exit-4 model/session/inference/comparison validation failures.
- Exit codes remain `0` success, `2` CLI usage, `3` unsupported platform/architecture, `4` model/session/inference/comparison validation failure, and `5` provider discovery/activation/registration/device-visibility failure.
- Do not modify OBS plugin code, model preprocessing/postprocessing, Linux/macOS provider behavior, or the existing standalone ONNX Runtime build.
- Use strict RED/GREEN TDD: each production behavior starts with a test that is run and observed failing for the intended reason.
- Generated C/C++ files use `GPL-3.0-or-later`; Python/CMake/workflow files use `Apache-2.0`; documentation is covered by `REUSE.toml` as `GPL-3.0-or-later`.
- Commits use `Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>`, `git commit -s -S`, signing key `460B18400D17462FF3714A2BDCED30418A1C06FC`, and no assistant/co-author attribution.
- During sprint CI, retain the PR labels `windows-only-ci` and `upload-artifacts` to conserve GitHub Student quota and preserve the downloadable hardware-test artifact.

---

### Task 1: Add the benchmark CLI and portable numerical contract

**Files:**
- Create: `tools/windows-ml-smoke/benchmark.hpp`
- Create: `tools/windows-ml-smoke/benchmark.cpp`
- Create: `tools/windows-ml-smoke/tests/benchmark-test.cpp`
- Modify: `tools/windows-ml-smoke/cli.hpp`
- Modify: `tools/windows-ml-smoke/cli.cpp`
- Modify: `tools/windows-ml-smoke/tests/cli-test.cpp`
- Modify: `tools/windows-ml-smoke/CMakeLists.txt`

**Interfaces:**
- Consumes: the existing `parse_cli()` API and the model's fixed input/output element counts from Global Constraints.
- Produces: `CliCommand::Inference`, `CliCommand::Compare`; `CliOptions::{provider_name, comparison_provider_name, model_path, iterations, benchmark_requested}`; `make_deterministic_input(std::size_t)`; `summarize_latencies(std::span<const double>)`; and `compare_outputs(std::span<const float>, std::span<const float>, std::size_t foreground_channel, std::size_t channel_count, float threshold)`.

- [ ] **Step 1: RED — write parser tests for the new command surface**

  Extend `cli-test.cpp` with literal accepted cases for:

  ```text
  --provider cpu --model model.onnx
  --provider cpu --model model.onnx --iterations 100
  --provider MIGraphXExecutionProvider --model model.onnx --iterations 100
  --provider DmlExecutionProvider --model model.onnx --iterations 100
  --compare cpu MIGraphXExecutionProvider --model model.onnx --iterations 100
  --compare cpu DmlExecutionProvider --model model.onnx --iterations 100
  ```

  Assert exact provider spelling is preserved, legacy CPU selects non-benchmark mode with one iteration, and explicit benchmark commands retain iteration count `100`. Add rejected literal cases for zero, negative, non-decimal, overflow, duplicate `--iterations`, named provider without iterations, compare without iterations, compare baseline other than exact `cpu`, identical CPU/candidate names, mixed compare/provider commands, and list/prepare commands carrying benchmark options.

- [ ] **Step 2: Verify parser RED**

  Run:

  ```bash
  cmake -S tools/windows-ml-smoke -B /tmp/obs-br-winml-sprint4-red \
    -DWINDOWS_ML_SMOKE_CLI_TESTS_ONLY=ON
  cmake --build /tmp/obs-br-winml-sprint4-red
  ctest --test-dir /tmp/obs-br-winml-sprint4-red --output-on-failure
  ```

  Expected: `windows-ml-smoke-cli` fails because the new commands and iteration parsing do not exist; the provider-report test remains green.

- [ ] **Step 3: RED — write hand-derived benchmark-math tests**

  Create `benchmark-test.cpp`. Before each test, record in the task report the production mutation it catches. Assert these literal contracts:

  - the first eight values generated from seed `0x6d2b79f5` match the hand-calculated fixture `{0.682725887, 0.875184648, 0.062029365, 0.441263046, 0.804730225, 0.419035400, 0.150124320, 0.031266632}` within `1e-7`, every value is finite and in `[0,1]`, and two invocations are byte-identical;
  - latencies `{4.0, 1.0, 3.0, 2.0}` produce average `2.5`, median `2.5`, and nearest-rank p95 `4.0` without mutating the caller's vector;
  - empty or non-finite latency input is rejected;
  - CPU output `{0.9, 0.1, 0.4, 0.6, 0.8, 0.2, 0.3, 0.7}` and candidate output `{0.85, 0.15, 0.55, 0.45, 0.75, 0.25, 0.35, 0.65}` produce MAE `0.075`, foreground intersection `1`, union `2`, and IoU `0.5`;
  - an empty union produces IoU `1.0`;
  - size mismatch, invalid channel index/count, and non-finite output are rejected.

- [ ] **Step 4: Verify benchmark RED**

  Reconfigure and build after registering the test target in CMake. Expected: compile/link failure because `benchmark.hpp` and its functions are not implemented.

- [ ] **Step 5: GREEN — implement the minimal portable CLI and benchmark mathematics**

  Use a strict decimal `std::from_chars` parse into `std::size_t` and accept `1..10000`. Preserve the legacy CPU invocation without `--iterations`. Generate each input float by advancing this xorshift32 state exactly once and scaling the low 24 bits:

  ```cpp
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  value = static_cast<float>(state & 0x00ffffffU) / 16777215.0F;
  ```

  `summarize_latencies()` copies and sorts its input, calculates arithmetic mean, median (mean of the middle pair for even sizes), and nearest-rank p95 at `ceil(0.95 * count) - 1`. `compare_outputs()` computes MAE over every output float and IoU only from foreground channel `1` in each interleaved two-channel pixel.

- [ ] **Step 6: Verify GREEN and regressions**

  Run the fresh configure/build/CTest cycle and require all three portable tests to pass. Run `git diff --check`.

- [ ] **Step 7: Commit Task 1**

  ```bash
  git add tools/windows-ml-smoke
  git -c user.name='Gonçalo Filipe Brigues Gonçalves' \
      -c user.email='goncalogoncalves.02@gmail.com' \
      -c user.signingkey='460B18400D17462FF3714A2BDCED30418A1C06FC' \
      commit -s -S -m 'Add Windows ML benchmark contracts'
  ```

### Task 2: Enforce process-local provider activation and exact device attachment

**Files:**
- Create: `src/ort-utils/windows-ml-provider-policy.hpp`
- Create: `src/ort-utils/windows-ml-provider-policy.cpp`
- Create: `tools/windows-ml-smoke/tests/provider-policy-test.cpp`
- Modify: `src/ort-utils/windows-ml-provider.hpp`
- Modify: `src/ort-utils/windows-ml-provider.cpp`
- Modify: `tools/windows-ml-smoke/CMakeLists.txt`

**Interfaces:**
- Consumes: `Ort::Env`, `Ort::SessionOptions`, the exact provider name, Windows ML ready state, and the device metadata already copied by the provider module.
- Produces: `ProviderActivationAction activation_action(ProviderReadyState)` and `ProviderSessionResult configure_provider_session(Ort::Env&, Ort::SessionOptions&, std::string_view)`. The result carries requested/discovered provider, ready states before/after, whether process activation and registration occurred, selected `EpDeviceInfo`, HRESULT/error, and success.

- [ ] **Step 1: RED — add portable provider-policy tests**

  Use literal table cases:

  ```text
  Ready      -> RegisterReady
  NotReady   -> ActivateInstalledThenRegister
  NotPresent -> UnavailableWithoutActivation
  Unknown    -> UnavailableWithoutActivation
  ```

  Name the mutation caught by each branch, especially the security-relevant mutation that maps `NotPresent` to activation.

- [ ] **Step 2: Verify policy RED**

  Register `provider-policy-test` in CLI-tests-only CMake and run the portable suite. Expected: compile failure because the policy interface does not exist.

- [ ] **Step 3: GREEN — implement the policy and Windows provider configuration**

  Implement the pure policy first. In `configure_provider_session()`:

  1. Enumerate existing `Ort::Env::GetEpDevices()` and retain exact-name AMD GPU candidates.
  2. If candidates already exist, select the lowest `(vendor_id, device_id)` pair and append it with `SessionOptions::AppendExecutionProvider_V2`; this is the built-in DirectML path and does not touch the catalog.
  3. Otherwise find the exact provider in the catalog and read its ready state.
  4. For `NotPresent` or unknown state, return controlled unavailability without calling `WinMLEpEnsureReady`.
  5. For `NotReady`, call `WinMLEpEnsureReady`, record that process activation occurred, re-read the state, and require `Ready`.
  6. For `Ready`, obtain the non-empty library path and register the exact discovered name with the supplied `Ort::Env`.
  7. Re-enumerate devices, select an exact-name AMD GPU deterministically, and attach only that device with empty `Ort::KeyValuePairs`.
  8. Catch and sanitize HRESULT, `Ort::Exception`, standard exception, and unknown failures without throwing through the public boundary.

  Do not reuse `prepare_provider()` and do not add a boolean that could permit `NotPresent` acquisition from this interface.

- [ ] **Step 4: Verify GREEN locally and compile surface in CI-ready form**

  Run all portable tests, `git diff --check`, clang-format dry-run on changed C/C++ files, and verify with `rg` that the new inference configuration path contains no call to `prepare_provider()`. Native header/API compatibility is deferred only to exact-head Windows CI.

- [ ] **Step 5: Commit Task 2**

  ```bash
  git add src/ort-utils tools/windows-ml-smoke
  git -c user.name='Gonçalo Filipe Brigues Gonçalves' \
      -c user.email='goncalogoncalves.02@gmail.com' \
      -c user.signingkey='460B18400D17462FF3714A2BDCED30418A1C06FC' \
      commit -s -S -m 'Configure installed Windows ML GPU providers'
  ```

### Task 3: Run deterministic CPU/GPU inference and comparison

**Files:**
- Create: `tools/windows-ml-smoke/inference-runner.hpp`
- Create: `tools/windows-ml-smoke/inference-runner.cpp`
- Create: `tools/windows-ml-smoke/benchmark-report.hpp`
- Create: `tools/windows-ml-smoke/benchmark-report.cpp`
- Create: `tools/windows-ml-smoke/tests/benchmark-report-test.cpp`
- Modify: `tools/windows-ml-smoke/main.cpp`
- Modify: `tools/windows-ml-smoke/CMakeLists.txt`
- Modify: `tests/WindowsMlSmoke/test_windows_ml_smoke.py`

**Interfaces:**
- Consumes: parsed commands, deterministic input and math from Task 1, and provider configuration from Task 2.
- Produces: `InferenceResult run_inference(...)`, `ComparisonResult run_comparison(...)`, and deterministic report formatters. Each successful inference result owns the last output vector and latency summary so CPU and candidate results can be compared after both sessions finish.

- [ ] **Step 1: RED — add report-format and black-box command tests**

  Add portable formatter tests using literal result structures. Assert unique ordered keys, fixed six-decimal floats, sanitized provider/device/error values, and terminal status. Add Windows black-box tests that:

  - preserve the exact legacy CPU report for the legacy command;
  - run `--provider cpu --model <model> --iterations 3`, require 10 warm-ups, 3 timed calls, exact tensor metadata, `73728` finite output values, and finite nonnegative average/p50/p95;
  - reject a missing named provider without acquisition, exit `5`, report `process_activation_attempted=false`, and end with `status=unavailable`;
  - reject comparison threshold failure through exit `4` when a portable formatter fixture is marked failed.

  Hardware GPU success is not asserted on a hosted CI runner.

- [ ] **Step 2: Verify RED**

  Run the portable suite and the Python suite discovery. Expected: portable formatter compile failure; on Linux, Windows black-box cases remain explicitly skipped. On Windows CI the new CPU benchmark test will fail until the runner exists.

- [ ] **Step 3: GREEN — extract the legacy CPU flow and implement the benchmark runner**

  `run_inference()` must:

  1. Create a fresh `Ort::Env` and `Ort::SessionOptions` per result.
  2. Keep CPU options provider-free. For a named provider, call only `configure_provider_session()`.
  3. Construct the session and validate one float input/output with exact shapes and counts before running.
  4. Build one CPU-memory tensor from the deterministic input.
  5. Run zero warm-ups for legacy mode and exactly 10 warm-ups for benchmark mode.
  6. Time each `Session::Run()` separately with `steady_clock`; exclude setup, session creation, validation, warm-up, and comparison math.
  7. On every timed call, validate one float tensor, exact shape/count, and all `73728` finite values.
  8. Retain the final timed output and summarize only timed latency samples.

  `run_comparison()` runs CPU first and the candidate second in the same process with the exact same immutable input. It computes Task 1 metrics, requires MAE `<= 0.05`, IoU `>= 0.95`, and candidate average `<` CPU average, and never substitutes CPU for a failed candidate.

- [ ] **Step 4: GREEN — wire commands and stable diagnostics**

  Keep legacy output unchanged. Benchmark inference emits at least:

  ```text
  operation=inference
  requested_provider=<exact name>
  effective_provider=<cpu or exact EP>
  process_activation_attempted=<true|false>
  provider_registration_succeeded=<true|false>
  selected_ep_name=<value or empty>
  selected_device_id=<0x... or empty>
  warmup_iterations=10
  iterations=<N>
  finite_output_count=73728
  latency_average_ms=<fixed decimal>
  latency_p50_ms=<fixed decimal>
  latency_p95_ms=<fixed decimal>
  status=ok
  ```

  Comparison emits CPU/candidate averages, p50, p95, speedup ratio, normalized MAE, foreground intersection/union/IoU, the three boolean gates, and terminal status. Provider setup failures exit `5`; model/session/run/shape/finite/correctness/performance failures exit `4`.

- [ ] **Step 5: Verify GREEN and regression behavior**

  Run the portable suite, Python test discovery, `git diff --check`, clang-format dry-run, and `reuse lint`. Record that native compilation and CPU execution still require Windows CI.

- [ ] **Step 6: Commit Task 3**

  ```bash
  git add tools/windows-ml-smoke tests/WindowsMlSmoke
  git -c user.name='Gonçalo Filipe Brigues Gonçalves' \
      -c user.email='goncalogoncalves.02@gmail.com' \
      -c user.signingkey='460B18400D17462FF3714A2BDCED30418A1C06FC' \
      commit -s -S -m 'Run Windows ML provider benchmarks'
  ```

### Task 4: Package the exact hardware gate and record Sprint 4 evidence

**Files:**
- Modify: `.github/workflows/build-windows.yml`
- Create: `docs/windows-ml-inference-comparison.md`
- Modify: `REUSE.toml`

**Interfaces:**
- Consumes: the Windows executable and reports from Task 3, `data/models/mediapipe.onnx`, the exact-head GitHub Actions artifact, and user-supplied RX 9070 XT logs.
- Produces: one self-contained smoke ZIP containing the executable, runtime DLLs, and exact model; an exact PowerShell acceptance sequence; and durable CPU/MIGraphX/DirectML evidence with a Sprint 5 go/no-go decision.

- [ ] **Step 1: RED — extend the Windows packaging and artifact assertions**

  In the workflow, make the packaging step fail unless the staged directory contains:

  ```text
  windows-ml-smoke.exe
  mediapipe.onnx
  at least one adjacent runtime DLL
  ```

  Copy the tracked model with `Copy-Item -LiteralPath`. Extend the existing artifact verification to inspect the ZIP and assert exact single entries for the executable and model plus one or more DLLs. Run the workflow change through actionlint/yamllint where available; expected pre-change package inspection would fail because the model is absent.

- [ ] **Step 2: GREEN — add exact hardware commands**

  Document this sequence from the extracted artifact directory:

  ```powershell
  & .\windows-ml-smoke.exe --prepare-provider MIGraphXExecutionProvider 2>&1 |
    Tee-Object -FilePath .\sprint4-migraphx-prepare.log
  "prepare_exit_code=$LASTEXITCODE"

  & .\windows-ml-smoke.exe --provider cpu --model .\mediapipe.onnx --iterations 100 2>&1 |
    Tee-Object -FilePath .\sprint4-cpu.log
  "cpu_exit_code=$LASTEXITCODE"

  & .\windows-ml-smoke.exe --provider MIGraphXExecutionProvider --model .\mediapipe.onnx --iterations 100 2>&1 |
    Tee-Object -FilePath .\sprint4-migraphx.log
  "migraphx_exit_code=$LASTEXITCODE"

  & .\windows-ml-smoke.exe --compare cpu MIGraphXExecutionProvider --model .\mediapipe.onnx --iterations 100 2>&1 |
    Tee-Object -FilePath .\sprint4-migraphx-compare.log
  "migraphx_compare_exit_code=$LASTEXITCODE"

  & .\windows-ml-smoke.exe --provider DmlExecutionProvider --model .\mediapipe.onnx --iterations 100 2>&1 |
    Tee-Object -FilePath .\sprint4-directml.log
  "directml_exit_code=$LASTEXITCODE"

  & .\windows-ml-smoke.exe --compare cpu DmlExecutionProvider --model .\mediapipe.onnx --iterations 100 2>&1 |
    Tee-Object -FilePath .\sprint4-directml-compare.log
  "directml_compare_exit_code=$LASTEXITCODE"
  ```

  State that PowerShell comments or explanatory prose must not be pasted after a pipeline continuation character. Record executable/model SHA-256, artifact digest, Windows build, GPU/driver identifiers, provider readiness/activation/registration, selected EP device, all latency summaries, correctness metrics, and exit codes.

- [ ] **Step 3: Verify exact-head CI and artifact integrity**

  Push only after task review. Require `Check CI` and the exact `build-windows-x64 / build` job to succeed at the same SHA, download the artifact, verify its GitHub digest and local SHA-256, and confirm the model in the ZIP hashes identically to tracked `data/models/mediapipe.onnx`.

- [ ] **Step 4: Run the RX 9070 XT hardware gate**

  Accept a provider only when its compare command exits `0`, completes 100 timed calls for both CPU and candidate, reports `finite_output_count=73728`, normalized MAE `<= 0.05`, IoU `>= 0.95`, and candidate average latency strictly below CPU. MIGraphX is primary; DirectML is an independent comparison and fallback candidate. A controlled failure remains evidence but does not authorize integration of that provider.

- [ ] **Step 5: Finalize and commit durable evidence**

  After receiving the logs, replace the evidence document's initial pending-result section with exact sanitized results and the Sprint 5 decision. Raw logs remain under ignored `.superpowers/sdd/`; do not commit them.

  ```bash
  git add .github/workflows/build-windows.yml docs/windows-ml-inference-comparison.md REUSE.toml
  git -c user.name='Gonçalo Filipe Brigues Gonçalves' \
      -c user.email='goncalogoncalves.02@gmail.com' \
      -c user.signingkey='460B18400D17462FF3714A2BDCED30418A1C06FC' \
      commit -s -S -m 'Document Windows ML inference comparison'
  ```

## Sprint completion gate

Sprint 4 is complete only when all four task reviews are clean, the final whole-sprint review is clean, exact-head Windows CI and artifact integrity pass, and the RX 9070 XT logs establish a passing MIGraphX or DirectML candidate. A build-only success, provider discovery, session construction, or fewer than 100 timed calls is insufficient.

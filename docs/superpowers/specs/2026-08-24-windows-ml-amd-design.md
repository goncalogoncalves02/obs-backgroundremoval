# Windows ML AMD GPU Support Design

**Date:** 2026-08-24

**Status:** Approved for implementation planning

## Goal

Add AMD GPU inference support to the Windows build of OBS Background Removal while preserving the current `main` branch architecture, keeping CPU inference as a reliable fallback, and leaving Linux and macOS behaviour unchanged.

The first supported hardware target is Windows 11 with an AMD Radeon RX 9070 XT. Support is only claimed for provider, model, and hardware combinations that pass the defined validation gates.

## Baseline and dependency strategy

Implementation starts from the current `main` branch at commit `9772c540279cc84b8c5be5442c50ccb00b399a6e`. This baseline uses standalone ONNX Runtime 1.28.0.

The Windows build will move to the ONNX Runtime distributed by the stable self-contained `Microsoft.Windows.AI.MachineLearning` package. The initial pinned package for the compatibility spike is `2.2.12`. The smoke test must report the embedded ONNX Runtime version before plugin integration proceeds.

Linux and macOS continue using the existing standalone ONNX Runtime dependency and existing execution-provider logic. The Windows plugin must not load two different ONNX Runtime copies into the OBS process.

## Architectural approach

The existing inference pipeline remains intact:

```text
OBS frame
  -> OpenCV preprocessing
  -> existing Model implementation
  -> Ort::Session
  -> CPU or registered Windows ML execution provider
  -> existing postprocessing and OBS rendering
```

Windows ML integration is limited to build configuration, provider discovery and registration, execution-provider device selection, and session creation. `Model.hpp`, model-specific preprocessing, postprocessing, and OBS rendering remain provider-independent.

The implementation uses a standalone smoke-test executable to remove OBS from the initial compatibility and hardware investigation. GPU support is not exposed in OBS until the exact tracked MediaPipe model, `data/models/mediapipe.onnx`, passes the isolated GPU correctness gate.

## Components

### Windows ML provider module

`src/ort-utils/windows-ml-provider.hpp` and `src/ort-utils/windows-ml-provider.cpp` form a small OBS-independent module used by both the smoke tool and the plugin.

The module owns:

- execution-provider catalog discovery;
- provider ready-state inspection;
- registration of an already-installed provider library with an `Ort::Env`;
- enumeration and selection of ONNX Runtime EP devices;
- attachment of a selected EP device to `Ort::SessionOptions`;
- structured diagnostics describing every decision and failure.

The module does not own:

- provider downloads initiated by the OBS plugin;
- OBS logging or UI widgets;
- model loading policy;
- image preprocessing or mask postprocessing;
- session fallback policy.

Provider preparation is exposed separately for the smoke tool. The OBS plugin never calls provider preparation and never performs network or package acquisition while loading a filter, opening properties, or running inference.

### Windows ML smoke tool

`tools/windows-ml-smoke` is a Windows-only C++20 executable linked against the self-contained Windows ML package. It does not depend on OBS rendering or an active OBS process.

Its staged command surface is:

```text
windows-ml-smoke.exe --provider cpu --model <path>
windows-ml-smoke.exe --list-providers
windows-ml-smoke.exe --prepare-provider <provider-name>
windows-ml-smoke.exe --provider <provider-name> --model <path> --iterations 100
windows-ml-smoke.exe --compare cpu <provider-name> --model <path> --iterations 100
```

The tool reports Windows and architecture information, Windows ML and ONNX Runtime versions, provider state, registered EP devices, selected device, model metadata, session outcome, tensor shapes, correctness results, and latency statistics.

### Plugin session integration

The existing `createOrtSession(filter_data*)` function remains the session lifecycle seam. Windows GPU selection calls the provider module before constructing the existing `Ort::Session`.

The provider module returns structured data to the caller. The plugin translates that data into OBS logs and stores the requested provider, effective provider, selected device, and fallback reason in runtime state.

### OBS provider selection

Windows uses logical identifiers that do not collide with existing platform semantics:

```text
cpu
winml-migraphx
winml-directml
```

The exact Windows ML and ONNX Runtime provider names are discovered and recorded by the smoke tool rather than assumed from display labels.

CPU always appears in the inference-device list. A Windows GPU option appears only when the corresponding runtime/provider path is usable. Opening OBS properties never prepares or downloads a provider and never runs inference.

The first integrated GPU alpha supports MediaPipe only. Other models remain on CPU until each provider/model combination passes isolated and OBS validation.

## Provider and session flow

Provider handling is split into three operations:

```text
discover -> register -> attach to SessionOptions
```

Preparation is an explicit smoke-tool-only operation that precedes those three operations when a provider is not ready.

Each session tracks four distinct values:

```text
requested_provider
available_provider
effective_provider
fallback_reason
```

For a requested Windows GPU provider:

1. Discover the provider and inspect its ready state without preparing it.
2. Register the ready provider library with the filter's `Ort::Env`.
3. Enumerate compatible EP devices and select the intended device.
4. Attach that device to a new `Ort::SessionOptions`.
5. Create the existing `Ort::Session` with the exact plugin model.
6. Record and log the effective provider and device.

Provider catalog metadata may be cached to avoid repeated discovery from UI paths. Registration remains associated with the `Ort::Env` that owns the session.

## CPU fallback and errors

GPU failure never leaves a partially configured session active. If discovery, registration, device selection, provider attachment, or session creation fails:

1. Log the requested provider and exact failure.
2. Destroy/reset partial GPU session state.
3. Construct new CPU-only session options with the existing CPU thread settings.
4. Create a CPU session.
5. Store and log CPU as the effective provider and retain the fallback reason.
6. Keep the filter active.

The plugin returns a startup error only when the CPU fallback also fails. Silent fallback is prohibited.

Logs must make the result unambiguous:

```text
Requested inference provider: winml-migraphx
Provider available: yes
Effective inference provider: MIGraphXExecutionProvider
EP device: <runtime-reported name>
Fallback reason: none
```

or:

```text
Requested inference provider: winml-migraphx
Provider available: no
Effective inference provider: CPU
Fallback reason: <actionable error>
```

## Sprint structure and gates

### Sprint 1: Baseline and isolation

Create the feature workspace from `main`, ignore `.superpowers/`, audit the current Windows build and ORT lifecycle, and record the clean baseline. Existing user changes in other worktrees remain untouched.

### Sprint 2: Windows ML and CPU smoke test

Pin the self-contained Windows ML package, add the Windows-only smoke target and CLI, report runtime details, and load the exact tracked `data/models/mediapipe.onnx` model on CPU.

Gate: x64 Release build and CPU session must pass on Windows before provider preparation work proceeds.

### Sprint 3: MIGraphX discovery and preparation

Add catalog enumeration, ready-state diagnostics, EP registration/device enumeration, and the explicit provider-preparation command.

Gate: the RX 9070 XT machine must produce either a ready/registered MIGraphX device or a controlled incompatibility report with an actionable reason.

### Sprint 4: Isolated inference and comparison

Add deterministic MediaPipe input, warm-up, 100 inference iterations, latency statistics, finite-value and shape checks, CPU/GPU comparison, and an independent DirectML run.

Gate: GPU integration proceeds only if MIGraphX or DirectML completes 100 calls, produces valid output, satisfies normalized mean absolute error at most 0.05 and binary-mask intersection-over-union at least 0.95, and records lower average inference latency than the CPU path for the same model and input.

### Sprint 5: Windows plugin build integration

Replace standalone ORT with the Windows ML runtime only on Windows, package required self-contained runtime dependencies, and preserve existing Linux/macOS dependency paths.

Gate: Windows plugin and package build, OBS loads the DLL, CPU inference works, and non-Windows configuration remains semantically unchanged.

### Sprint 6: Provider module and session integration

Share the provider module with the plugin, add Windows provider identifiers, attach selected EP devices in `createOrtSession()`, and implement explicit CPU fallback and effective-provider logging.

Gate: CPU, successful GPU creation, unavailable-provider fallback, and failed-session fallback are all observable and safe.

### Sprint 7: OBS UI and MediaPipe alpha

Expose only usable Windows provider choices, keep provider acquisition outside OBS, reinitialize sessions through the existing update flow, and restrict initial GPU claims to MediaPipe.

Gate: repeated CPU to GPU to CPU switching, source lifecycle actions, and ten minutes per validated provider complete without crashes, frozen masks, or silent fallback.

### Sprint 8: Compatibility and lifecycle hardening

Validate every plugin model independently, treat RVM as stateful, audit critical OBS threads, run 50 provider-switch and filter-lifetime cycles, and add rate-limited inference timing.

Gate: only passing provider/model combinations are documented as supported; memory stabilizes and no deadlock, crash, or persistent mask freeze occurs.

### Sprint 9: Packaging, CI, and documentation

Finalize self-contained packaging, add Windows CPU smoke coverage to CI, validate clean-machine installation, document provider preparation and diagnostics, record the compatibility matrix and RX 9070 XT benchmarks, and run the release-candidate stress matrix.

Gate: a clean Windows machine loads the packaged plugin without developer dependencies, CPU fallback works, and all support claims have stored evidence.

## Testing and evidence

Every sprint follows this control loop:

```text
detailed sprint plan
  -> fresh implementer subagent
  -> available automated checks
  -> independent code-review subagent
  -> fix and scoped re-review when required
  -> Windows or hardware acceptance gate when required
  -> commit
```

No Windows/GPU sprint is complete solely because code compiles. Hardware-dependent acceptance is performed by the user on the RX 9070 XT using exact PowerShell commands supplied with the sprint handoff. Each handoff states the working directory, prerequisites, commands, expected output, log-capture command, and files to return.

Temporary plans, subagent ledgers, review packages, and raw Windows logs live under the ignored `.superpowers/` directory. Permanent user-facing evidence is committed only to the relevant documentation files.

CI validates compilation, CPU model loading, error handling, and packaging. It does not claim GPU success without target hardware.

For each Windows-focused sprint, the blocking CI and acceptance gate is `Check CI`, a successful exact `build-windows-x64 / build` job for the tested commit, an intact Windows artifact from that run, and the required manual Windows hardware test. The aggregate cross-platform matrix may continue in GitHub in the background; it becomes blocking only before merge, release, or an explicitly shared cross-platform acceptance point. A successful Windows job is sufficient for the sprint even when the enclosing PR Check run remains in progress for non-Windows jobs.

## Git and collaboration policy

Each sprint produces a small, reversible commit only after its acceptance gate passes. Commits use the Git identity already configured by the user and contain no Codex attribution, `Co-authored-by` trailer, or assistant signature.

No push, pull request, release, or other external publication occurs without confirmation at the relevant gate. Pull-request text, if later requested, does not mention the assistant as author.

## Non-goals

- Rewriting model preprocessing, postprocessing, or OBS rendering.
- Replacing OpenCV.
- Adding zero-copy GPU texture interop in the first release.
- Changing Linux or macOS provider behaviour.
- Preparing or downloading providers from inside OBS.
- Claiming support for untested GPUs or model/provider combinations.
- Removing CPU inference or allowing silent fallback.
- Maintaining two ONNX Runtime copies inside the OBS process.

## Completion criteria

The project is complete when the release checklist in the AMD/Windows ML roadmap is satisfied with evidence: clean Windows build and installation, stable CPU fallback, a validated AMD provider on the RX 9070 XT, explicit effective-provider logs, model compatibility results, lifecycle stress results, performance benchmarks, and user documentation, while Linux and macOS behaviour remain unchanged.

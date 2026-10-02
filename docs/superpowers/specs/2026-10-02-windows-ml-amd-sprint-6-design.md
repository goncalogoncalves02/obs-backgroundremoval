<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Sprint 6: Windows ML provider and session integration

**Date:** 2026-10-02

**Status:** Written specification approved by the owner on 2026-10-02 for implementation planning.

## Intent and baseline

Integrate CPU, DirectML and MIGraphX into the Windows plugin's session-creation path, using the provider module already validated by the standalone smoke tool. The first GPU model is the exact tracked `data/models/mediapipe.onnx`; the target machine is the owner's Radeon RX 9070 XT. A failed GPU initialization must leave an explicit, usable CPU session or a controlled startup error.

The owner selected both GPU providers and approved this design in conversation. This specification refines the previously approved [overall architecture](2026-08-24-windows-ml-amd-design.md), preserving its Sprint 6 backend / Sprint 7 visual-selection boundary.

Implementation baseline is accepted Sprint 5 commit `74b0498d197288e54f932b818bb9e857b984db7f` on `feature/windows-ml-amd`. The Sprint 6 branch is isolated from `GPU` and the accepted Sprint 5 branch. Windows retains self-contained Windows ML **2.2.12**; its runtime reported ONNX Runtime **1.25.2** in Sprint 5 CI. Linux and macOS retain standalone ONNX Runtime **1.28.0** and their existing provider semantics.

Sprint 5's exact package passed Windows CI and owner OBS CPU acceptance, including filter removal and recreation without a crash. Earlier Sprint 4 GPU results are compatibility evidence, not validation of this new production session path or a promise of current performance. The owner now uses OBS 32.2.2, Windows build 26300 (26H2 revision 9550), and AMD driver 32.0.31041.3013.

## Scope and boundaries

Sprint 6 delivers Windows provider identifiers, shared provider configuration, a reusable production session-construction core, explicit CPU fallback, runtime diagnostics, DirectML runtime packaging, and automated/native acceptance evidence.

The existing `createOrtSession(filter_data*)` remains the plugin entry point. Model preprocessing, inference inputs/outputs, mask postprocessing and OBS rendering remain provider-independent. Changes to lifetime handling are limited to making session initialization and failure safe.

Visual provider options, properties discovery, interactive switching and the ten-minute OBS GPU alpha gate belong to Sprint 7. Broader model validation and stress testing remain later sprints. Sprint 6 does not add automatic GPU preference selection, GPU-to-GPU fallback, an adapter selector, runtime loader hooks, search-path changes, dependency upgrades, or provider acquisition from OBS.

## Identifiers and shared components

| Stored logical identifier | Exact runtime provider | Windows behavior |
| --- | --- | --- |
| `cpu` | `CPUExecutionProvider` | Existing CPU baseline and thread settings |
| `winml-directml` | `DmlExecutionProvider` | Explicit DirectML AMD GPU route |
| `winml-migraphx` | `MIGraphXExecutionProvider` | Explicit MIGraphX AMD GPU route |

The Windows identifiers do not reinterpret legacy `migraphx`, `cuda`, `rocm`, `tensorrt` or `coreml` settings. Unsupported identifiers on Windows produce a controlled diagnostic and error rather than successful, mislabeled CPU execution. Non-Windows dispatch remains unchanged. Existing Windows CPU settings continue to work, and no new GPU option is added to OBS properties in this sprint.

`windows-ml-provider.hpp/.cpp` and the portable provider policy remain OBS-independent and shared by the smoke tool and plugin. `configure_provider_session()` configures an exact requested provider on the environment/options supplied by its caller. Provider readiness, registration, device enumeration and attachment remain in this module; CPU fallback and model eligibility belong to the session layer.

A small OBS-independent Windows session-construction core accepts the environment, exact model path, logical provider, model eligibility and CPU thread count. It returns a session or an initialization failure together with owned diagnostic data. The plugin adapts this result to filter runtime state and OBS logs. A native integration harness calls this same core; it must not duplicate the production dispatch/fallback implementation.

The existing smoke `--provider` and `--compare` GPU commands remain strict: an unavailable or failing requested GPU is a failure, never a passing CPU benchmark. Tests of production fallback use the production core explicitly, keeping strict benchmarking and plugin fallback policies distinct.

## Readiness and device selection

The existing provider module first looks for an already-visible EP device matching the exact provider name, GPU hardware type and AMD vendor ID `0x1002`. This supports the built-in DirectML route without requiring a catalog entry or falsely reporting catalog registration.

If no matching device is visible, the module inspects the exact catalog provider:

| Readiness | Allowed action inside OBS | Result |
| --- | --- | --- |
| `NotPresent` | No `EnsureReady`, acquisition or installation | Unavailable; explicit CPU fallback |
| `Unknown`, absent entry or discovery failure | No preparation attempt | Controlled failure; explicit CPU fallback |
| `NotReady` | Activate only the already-installed package in this process, then re-read readiness | Continue only if `Ready` |
| `Ready` | Register the installed provider library with the session environment | Enumerate and attach the matching AMD GPU |

The explicit preparation command remains an external smoke/deployment operation. Successful preparation in another process does not remove the need for process-local activation where applicable. Opening properties and running frames never acquire providers.

Device selection preserves the existing module's lowest vendor/device-ID selection among matching devices, with enumeration order breaking ties. Diagnostics identify the actual selected EP and hardware IDs. This sprint does not promise matching the OBS graphics adapter on a multi-GPU machine. The selected device is attached through the pinned runtime's `AppendExecutionProvider_V2` API; borrowed runtime device handles do not escape their valid lifetime.

## Session creation and fallback contract

1. Validate the logical identifier and model/path prerequisites. Reset stale diagnostics for the new attempt. A direct CPU request skips GPU discovery and builds the existing CPU options.
2. For an eligible GPU request, construct fresh GPU options: existing graph optimization, disabled memory pattern, sequential execution, and `session.disable_cpu_ep_fallback=1`. Configure only the exact requested GPU provider and construct its session.
3. Publish GPU success only after successful session construction and plugin model metadata/buffer initialization. Store the exact effective provider and copied selected-device information. No GPU success is reported merely because attachment succeeded.
4. If GPU discovery, activation, registration, device selection, attachment or session construction fails, record the failure and discard the partial GPU session/options. Construct **new CPU-only options**, with the existing CPU inter/intra-op thread count and graph optimization, without GPU attachment or the CPU-fallback prohibition. Attempt one CPU session.
5. If CPU fallback initializes successfully, keep the filter usable, retain the requested identifier, record CPU as effective and record the original GPU failure. If CPU initialization also fails, return the existing startup failure with both causes and no usable partial session.

The explicit node-fallback prohibition prevents a successful GPU-labelled session from silently delegating unsupported graph nodes to CPU. It is not a claim that host-side preprocessing or transfer work runs on GPU. A successful whole-session CPU fallback is deliberately logged as CPU.

GPU eligibility in this sprint is restricted to MediaPipe. A recognized Windows GPU request for another model goes directly to CPU with `unsupported_model` as the explicit fallback reason; it does not probe or attach a GPU. Missing/invalid model inputs, metadata errors and tensor-allocation errors remain controlled initialization errors rather than being disguised as provider availability failures. They must not escape the plugin initialization boundary as uncaught exceptions.

Fallback occurs during initialization only. No per-frame retry, provider switch, repeated acquisition, or new inference-error recovery policy is added. A later explicit reinitialization may retry the requested provider. The stored request is never rewritten to CPU merely because a previous initialization fell back.

## Lifetime and concurrency

Every `Ort::Session` uses the filter's owning `Ort::Env`; provider registration is associated with that environment. Candidate sessions are destroyed before the environment that supports them. Device/name metadata retained for diagnostics owns its strings and values.

The existing model mutex continues to serialize session initialization, model inference and replacement. A DirectML session must never receive concurrent `Run` calls. Initialization does not expose a candidate session or mismatched names, dimensions and tensor buffers to the render/inference path. Failure leaves the filter in its controlled disabled/error state, without stale effective-provider claims or use of previous model metadata. The implementation plan must cover normal destruction and repeated initialization with these invariants.

## Diagnostics

One initialization outcome records the following, through structured state and corresponding OBS log messages:

- requested logical provider, exact runtime provider and model;
- provider availability, observed readiness and activation/registration outcomes where applicable;
- effective provider, selected EP/vendor/device IDs for GPU success, and session outcome;
- fallback reason, failing stage and original error/HRESULT where available;
- CPU failure as a separate cause when fallback also fails.

Use stable reason categories such as `unsupported_model`, `provider_unavailable`, `provider_activation_failed`, `provider_registration_failed`, `device_unavailable`, `provider_attachment_failed` and `gpu_session_failed`, while retaining the detailed runtime error. Direct CPU success and GPU success have no fallback reason. Failed initialization has no effective provider; fallback to CPU has no active GPU device. An attempted device may be retained separately as diagnostic context.

Successful visible-device attachment need not involve catalog activation or registration; logs must represent that path accurately. Diagnostics are emitted per initialization, not per frame, and exclude complete OBS logs or personal installation paths from committed evidence.

## Build and package contract

Compile/link the shared provider and production session core into the Windows plugin and native integration harness using the validated Windows ML targets. Linux/macOS source selection and dependency discovery remain semantically unchanged.

The Sprint 6 Windows plugin package contains exactly one package-origin copy each of `Microsoft.Windows.AI.MachineLearning.dll`, `onnxruntime.dll` and **`DirectML.dll`**, adjacent to the plugin DLL in its existing runtime directory. DirectML comes from the pinned 2.2.12 package's `runtimes/win-x64/native/DirectML.dll`; no standalone Windows ORT or separately downloaded DirectML is added. Required Windows ML license/notices remain packaged.

Extend the existing installed-tree and ZIP verifier from Sprint 5's DirectML prohibition to this required-file contract, including duplicate, location, package-origin and archive-read failure checks. Preserve existing canonical package-validation, configuration mapping and legal-origin checks. Test/harness runtime staging also includes the required DirectML DLL explicitly; do not assume an interface target appears in `TARGET_RUNTIME_DLLS`.

GPU EP packages acquired through external preparation are not redistributed inside the plugin ZIP. Runtime lookup uses the approved existing deployment layout. If exact-artifact testing reveals a loader-resolution failure, capture evidence and obtain a separately reviewed design before introducing loader/search-path workarounds.

## Verification and acceptance gates

| Layer | Required evidence |
| --- | --- |
| Portable policy and controlled failures | CPU dispatch, both exact GPU mappings, unsupported model/identifier handling, readiness rules, GPU setup/session failure, fresh CPU options, CPU fallback failure and truthful diagnostics |
| Native Windows runtime | Pinned-runtime CPU session and MediaPipe inference through the production core, metadata/buffer initialization, repeated creation/destruction, controlled fallback/error cases; existing strict smoke commands remain strict |
| Build/package regression | Windows plugin and harness compile, installed tree and ZIP pass origin/uniqueness/legal checks with DirectML required; existing backend and non-Windows configuration contracts remain valid |
| RX 9070 XT acceptance | Exact new artifact/source SHA; production-core CPU, DirectML and MIGraphX sessions; each GPU effective as requested, matching AMD device, valid MediaPipe output and repeated successful teardown/recreation |
| OBS package check | Exact package loads, existing CPU filter produces a visible mask and survives remove/recreate; requested/effective CPU logs are accurate |

Controlled tests must prove `NotPresent` never calls `EnsureReady` and that `NotReady` cannot proceed without a verified transition to `Ready`. Fallback tests must inspect fresh CPU options and actual outcome, not merely a successful return code. Missing/corrupt model and double-failure cases must leave no partial session. Include metadata lifetime and serialized inference checks where the session integration changes those paths.

The hardware harness exercises the **same production core** used by `createOrtSession()`, without adding an OBS provider UI. For CPU and each GPU, use the exact tracked MediaPipe model and deterministic input, warm up, run 100 inference calls, check expected tensor shapes and finite output, and compare GPU output to CPU: normalized mean absolute error at most 0.05 and binary-mask intersection-over-union at least 0.95. Record latency and current software versions; no new minimum speedup is promised. GPU acceptance requires actual requested-provider success, not a passing fallback.

CI may prove CPU and controlled failure behavior without AMD hardware; it must not claim either GPU passed on such a runner. Sprint completion requires independent review, passing checks and artifacts for the exact reviewed commit, independent package verification, and owner hardware acceptance. The implementation handoff supplies self-contained PowerShell commands, prerequisites, working directory, expected output, independent log collection and the files to return. Provider preparation is a separate explicit step outside OBS.

Sprint 6 acceptance establishes backend/session support for both GPU routes. Visual selection, interactive CPU/GPU switching, live GPU masks in OBS and extended OBS lifecycle acceptance remain Sprint 7 gates.

## Documentation and approval boundary

Durable specification, implementation plan and sanitized acceptance evidence are tracked; temporary briefs, raw reports and test receipts remain under ignored `.superpowers/`. Commits use the owner's signed identity and DCO sign-off. Preserve the owner's separate roadmap and the accepted Sprint 5 references.

Written-spec approval permits writing the implementation plan. The owner then reviews that written plan and chooses its execution method before implementation begins. This document does not itself authorize implementation or a new external submission.

## API references and version constraints

Current official guidance was consulted through Context7 during design:

- [Select execution providers](https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/select-execution-providers): visible EP devices and explicit provider/device selection.
- [Register execution providers](https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/register-execution-providers): registration with the owning environment.
- [Initialize execution providers](https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/initialize-execution-providers): process readiness and activation/acquisition distinction.

The exact pinned 2.2.12 package headers govern API syntax and option support. In particular, its `onnxruntime_session_options_config_keys.h` defines `session.disable_cpu_ep_fallback`, and the existing provider module uses the pinned device accessor. Do not copy newer documentation syntax or change dependency versions merely to match current examples.

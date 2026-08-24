# Windows ML MIGraphX provider discovery on AMD Radeon RX 9070 XT

## Scope and ruling

This document records the Sprint 3 hardware-acceptance result for provider discovery and preparation only. It does not establish MIGraphX inference correctness, output quality, stability, or performance.

The Sprint 3 RX 9070 XT gate is satisfied. The explicit preparation process found `MIGraphXExecutionProvider`, changed its readiness from `not_present` to `ready`, registered it with ONNX Runtime, and enumerated an exact-name AMD GPU EP device with vendor ID `0x1002` and device ID `0x7550`. The command reported `prepare_exit_code=0`.

## Tested build and host

| Input | Recorded value | Evidence |
|---|---|---|
| Tested source commit | `8fa1c9a2279f07bcaf15d789ff7861f35cdbbb70` | Sprint 3 exact-head CI and artifact audit |
| Smoke-tool artifact | `windows-ml-smoke_2.2.12_x64.zip` (artifact `9535150059`) | User-supplied hardware evidence |
| Artifact SHA-256 | `2cb4b9d6aedf1785929064dc004f1ab03fd10baea24578ff3d95b58c5934a9da` | User-supplied hardware evidence; matches exact-head artifact audit |
| Windows ML package | `2.2.12` | Artifact name and exact-head CI CPU report |
| ONNX Runtime | `1.25.2` | Exact-head CI CPU report; not separately printed by the RX 9070 XT transcript |
| Windows | `10.0.26200`, release `25H2`, revision `9168`, x64 | User's OBS log |
| GPU | AMD Radeon RX 9070 XT, PCI ID `1002:7550` | User's OBS log |
| GPU driver | `32.0.31041.1004` | User's OBS log |

## Catalog and preparation evidence

The intended PowerShell sequence was list, prepare, then list again. The first list attempt is not usable as a pre-prepare catalog record: Portuguese prose, `ausencia controlada:`, was pasted after a PowerShell pipeline operator. PowerShell treated that text as a command, so the attempt was malformed and produced neither a valid list report nor a list exit code. This document does not infer either missing value.

The subsequent explicit command used the exact provider name `MIGraphXExecutionProvider` and returned this decisive state:

| Field | Value |
|---|---|
| `operation` | `prepare-provider` |
| `provider_found` / discovered name | `true` / `MIGraphXExecutionProvider` |
| `ready_state_before` -> `ready_state_after` | `not_present` -> `ready` |
| `registration_succeeded` | `true` |
| Exact-name matching-device count | `1` |
| Matching device EP name / vendor | `MIGraphXExecutionProvider` / `AMD` |
| Matching device hardware type | `gpu` |
| Matching device hardware vendor | `Advanced Micro Devices, Inc.` |
| Matching device vendor ID / device ID | `0x1002` / `0x7550` |
| `matching_amd_gpu` | `true` |
| Terminal status / exit code | `status=ok` / `prepare_exit_code=0` |

The preparation report also enumerated a DirectML GPU device with the same AMD hardware identifiers and a CPU device. Those are catalog/device observations; the successful Sprint 3 match is specifically the exact-name `MIGraphXExecutionProvider` GPU device.

## Later list in a new process

The later `--list-providers` invocation ran in a new process. It listed `MIGraphXExecutionProvider` as `certified` with `ready_state=not_ready`, and `WebGpuExecutionProvider` as `uncertified` with `ready_state=not_present`; the report ended in `status=ok` without error output.

This later `not_ready` result does not contradict the successful preparation process. `not_ready` means the provider is installed but has not yet been added to that new application's runtime dependency graph. Preparation made the provider ready and registered it in its own process; installation persists, while the dependency-graph readiness is process-specific. Each consumer process must ensure readiness and register the provider before inference.

The user-supplied transcript did not include a printed `after_exit_code` value. Consequently, no after-list exit code is recorded here; `status=ok` is reported only as the terminal status that was supplied.

## Sprint 4 decision and limits

MIGraphX is the primary candidate for Sprint 4 isolated inference validation. DirectML remains the fallback and comparison candidate. Sprint 4 must establish the required inference correctness, output validation, and performance evidence before either provider is claimed usable for model inference.

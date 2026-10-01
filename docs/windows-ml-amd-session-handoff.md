<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>

SPDX-License-Identifier: GPL-3.0-or-later
-->

# Windows ML AMD project handoff

Snapshot date: 2026-10-02

This document is the durable entry point for continuing the Windows ML AMD work in a new session. Verify remote branch heads, CI, and artifact availability before treating the evidence below as current. Sprint 5 source review, exact-head Windows CI, package inspection, and owner OBS CPU acceptance have passed. This documentation-only `GPU` handoff records the accepted feature and artifact durably; no Sprint 6 implementation is authorized by that acceptance.

## Repository and branch state

- Repository: `goncalogoncalves02/obs-backgroundremoval`
- Durable documentation branch: `GPU`
- Implementation branch: `feature/windows-ml-amd`
- Draft pull request: <https://github.com/goncalogoncalves02/obs-backgroundremoval/pull/1>
- Tested and accepted feature head: `74b0498d197288e54f932b818bb9e857b984db7f`. Keep this exact head unchanged while recording acceptance on `GPU`.
- Before this handoff commit, local and remote `GPU` were at `9683cb4a895e2140bc035a46dfa49754a5118062`.
- `main` has not received this feature. No merge, release, or deployment is implied.
- PR #1 remains draft with `windows-only-ci` and `upload-artifacts`. Native Linux/macOS CI was not run in this iteration; a later full-matrix merge/release gate still requires removal of the quota-conservation label.
- This `GPU` update must contain only this handoff document, without copying or merging feature implementation files.
- Preserve the owner's untracked `obs-backgroundremoval-amd-windows-ml-roadmap.md`; do not stage, rewrite, or delete it.

## How the work is organized

Architectural work is divided into independently reviewable sprints. Each implementation task gets a task-scoped implementer and an independent review, followed by a whole-sprint review and exact-head verification. Durable plans and specifications live under `docs/superpowers/`; transient agent reports, downloaded dependencies, logs, artifacts, and receipts live under the ignored `/.superpowers/` directory.

Read these documents in order on `GPU`:

1. `docs/superpowers/specs/2026-08-24-windows-ml-amd-design.md`
2. `docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md` — historical roadmap; later sprint decisions supersede its initial provider priority and dependency assumptions.
3. `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-1.md`
4. `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-2.md`
5. `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-3.md`
6. `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-4.md`
7. `docs/windows-ml-baseline.md`
8. `docs/windows-ml-provider-discovery.md`
9. `docs/windows-ml-inference-comparison.md`

Sprint 5 specification and implementation plan are on the feature branch, not `GPU`. Read their immutable tested versions:

- [Sprint 5 build and packaging specification](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/74b0498d197288e54f932b818bb9e857b984db7f/docs/superpowers/specs/2026-08-30-windows-ml-amd-sprint-5-design.md)
- [Sprint 5 implementation plan](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/74b0498d197288e54f932b818bb9e857b984db7f/docs/superpowers/plans/2026-08-30-windows-ml-amd-sprint-5.md)

Do not copy those files or feature code to `GPU` merely to follow the links. The contribution, maintainer, signing, and DCO prerequisites were explicitly confirmed for this execution. Every project commit uses the owner's identity, signing key `460B18400D17462FF3714A2BDCED30418A1C06FC`, and DCO sign-off, with no assistant/co-author attribution.

## Completed source work

### Sprint 1 — baseline and isolation

Established the baseline, platform boundaries, CI isolation, and reviewable structure for the Windows-only work.

### Sprint 2 — Windows ML CPU smoke test

Integrated Windows ML 2.2.12 into the standalone smoke tool and validated CPU inference with MediaPipe, without changing production plugin inference.

### Sprint 3 — provider discovery and preparation

Implemented standalone provider discovery/preparation diagnostics and confirmed that `MIGraphXExecutionProvider` can be prepared for the AMD Radeon RX 9070 XT.

### Sprint 4 — deterministic GPU comparison

Added 100-iteration inference and CPU-versus-provider comparisons, disabled CPU fallback for explicit GPU tests, fixed a Windows ML metadata lifetime bug, and validated both MIGraphX and DirectML. Relevant feature commits include:

- `c3c6f19` — retain Windows ML metadata owners and cover 64 sequential sessions.
- `bbd46cf` — finalize the Sprint 4 documentation contract.

### Sprint 5 — production build and packaging integration

The production Windows plugin now links `WindowsML::Api` and `WindowsML::OnnxRuntime` from the pinned self-contained Windows ML 2.2.12 package through the internal `OnnxRuntimeBackend` interface. Windows standalone-ORT checkout/build/cache/reduced-operator/triplet machinery was removed, while the standalone smoke tool remains operational. The package deploys the Windows ML API DLL and ONNX Runtime DLL beside the plugin and includes the package's license and third-party notices.

Package discovery fails closed on missing, mismatched, redirected, or unvalidated package state. Fully validated canonical configuration state is persisted through an INTERNAL schema marker so ordinary reconfigure and build-system regeneration work without accepting legacy or invalid cache state. Actual root-module inclusion is covered, and only the two Windows ML imported targets clear the inherited RelWithDebInfo-to-Release mapping because the official package exposes configuration-less DLL/import-library locations.

The install-tree and ZIP verifier enforces required non-empty DLLs, adjacency, unique package-origin ONNX Runtime, exact-origin API/legal files, absence of `DirectML.dll`, safe archive paths, and rejection of symlinks/reparse points. Expected ZIP CRC/decompression/I/O failures produce one actionable `package-contract-error` line rather than a traceback.

Linux/macOS standalone package discovery, aliases, provider-symbol detection, workflows, reduced operators, and legal/install semantics were retained. The final whole-sprint review found and resolved an ordinary-INTERFACE-target regression in non-Windows compile probes: those probes still use imported `onnxruntime::onnxruntime`, while the plugin links `OnnxRuntimeBackend`. Real linked-symbol fixtures cover CUDA-only, ROCm-only, both, and neither. The binding specification's unchanged non-Windows semantics supersede the plan's probe-target wording.

Task-scoped reviews and a final whole-sprint source review, followed by scoped reviews of all corrections, are clean at `74b0498d197288e54f932b818bb9e857b984db7f`. Portable fixture path assertions were repaired for native Windows canonical/short paths without weakening required package or error checks. Native Windows CI then proved actual plugin configure/link/install/ZIP behavior. This source review and fixture coverage do not constitute native Linux/macOS matrix execution.

No production inference, model, filter lifecycle, rendering, provider UI, provider session selection, provider acquisition, or DLL loader/search-path changes were introduced. `src/DelayLoad.cpp` remains uncompiled. DirectML remains unintegrated in the production plugin, and `DirectML.dll` is absent from the plugin ZIP.

## Historical Sprint 4 hardware comparison

The following measurements are historical standalone-smoke evidence from Sprint 4. They are not Sprint 5 production-plugin GPU results and have not been rerun on the newer driver/Windows build used for CPU acceptance.

Historical test hardware:

- AMD Radeon RX 9070 XT, vendor ID `0x1002`, device ID `0x7550`
- Driver `32.0.31041.1004`
- Windows build `10.0.26200` / 25H2 revision 9168, x64

MIGraphX comparison with CPU fallback disabled:

- CPU average: 2.809828 ms
- MIGraphX average: 2.207812 ms
- Speedup: 1.272675x
- Normalized MAE: 0.000005
- Foreground IoU: 1.000000
- Accuracy and performance gates passed

DirectML comparison with CPU fallback disabled:

- CPU average: 2.789580 ms
- DirectML average: 0.504340 ms
- Speedup: 5.531150x
- Normalized MAE: 0.000008
- Foreground IoU: 1.000000
- Accuracy and performance gates passed

These results established DirectML as the selected primary direction for later production GPU integration, with MIGraphX a validated secondary provider. Sprint 5 deliberately limited production integration to the runtime/build/package and CPU compatibility; Sprint 6 still requires a reviewed design and owner approval before implementation.

Historical exact-head evidence for Sprint 4 feature head `bbd46cf7f1e3f3125dec85850534f47760b89790`:

- Check CI: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849461>
- PR Check: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543>
- Smoke artifact: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543/artifacts/9545182351>
  - SHA-256: `51b7fc856e36f3e6182a9e83998e034a91db25e62262cabbbd13d68e420e2a10`
- Plugin artifact: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543/artifacts/9545266577>
  - SHA-256: `78d359cfa4ca8d3b0adc2d985759627993745dd6fa7c38792a9512f5e5230ec9`

That historical plugin artifact predates Sprint 5 and uses standalone Windows ONNX Runtime. Use the Sprint 5 artifact below for the accepted Windows ML-integrated CPU package.

## Sprint 5 exact-head CI and artifact evidence

Accepted feature SHA: `74b0498d197288e54f932b818bb9e857b984db7f`.

- Check CI run `36936741004`: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/36936741004> — success, including formatting and REUSE.
- PR Check run `36936741423`: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/36936741423> — success.
- Native Windows job `110618655260`, `build-windows-x64 / build`: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/36936741423/job/110618655260> — success.
- Windows job interval: `2026-10-01T22:43:17Z` to `2026-10-01T22:49:06Z`.
- Native Windows contract tests, smoke configure/build/inference tests, production plugin configure/build/install, ZIP verification, and artifact upload passed at that SHA.
- Platform scope: Windows-only iteration; native Linux/macOS matrix not executed.

Accepted plugin artifact:

- Artifact ID: `11197544185`
- Name: `obs-backgroundremoval_1.4.1.dll.zip`
- URL: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/36936741423/artifacts/11197544185>
- Size: `49584619` bytes; ZIP entries: `44`.
- GitHub digest: `sha256:8c7c6ef8626d25ebbfb1a73cb03e78baf8a06bdfc46f47f76e02d097bb2d2cf1`
- Independently downloaded ZIP SHA-256: `8c7c6ef8626d25ebbfb1a73cb03e78baf8a06bdfc46f47f76e02d097bb2d2cf1` — equals the GitHub digest.
- Artifact was not expired at verification. GitHub retention is finite; refresh availability before downloading in a future session.

Dependency source: self-contained `Microsoft.Windows.AI.MachineLearning` 2.2.12 from the pinned NuGet URL:

<https://api.nuget.org/v3-flatcontainer/microsoft.windows.ai.machinelearning/2.2.12/microsoft.windows.ai.machinelearning.2.2.12.nupkg>

Pinned and independently verified NuGet SHA-256: `9cb60543337e6e4eac2a95c2fcb9650a880697ae7190d15499d801c907849da3`.

The downloaded ZIP was safely extracted and independently checked with `scripts/verify_windows_ml_package.py` against that hash-verified package. Exit code `0`: both install tree and ZIP passed, exactly one ONNX Runtime is adjacent to the plugin, both dependency DLLs and both legal files match the pinned package, and no `DirectML.dll` is present.

SHA-256 of accepted extracted files:

| Relative path | SHA-256 |
| --- | --- |
| `obs-backgroundremoval/bin/64bit/Microsoft.Windows.AI.MachineLearning.dll` | `e8ff278a34a53932b2d5e1b4878ca9c3776e97dd05eb90ad464a47be7230f56d` |
| `obs-backgroundremoval/bin/64bit/obs-backgroundremoval.dll` | `4118b1d99a09378adb3e6fa9b3b6df2461fadf39d18c39790906d6af1c46cc72` |
| `obs-backgroundremoval/bin/64bit/onnxruntime.dll` | `122590fa90552d727dc2310c692d38c6fefe2a71c1f82a5aa3da5f9a03ae4170` |
| `obs-backgroundremoval/licenses/windows-ml-license.txt` | `66395f8cb219087fae2bd025010bd9076b736c14f03b48f20295471c0c376814` |
| `obs-backgroundremoval/licenses/windows-ml-third-party-notices.txt` | `fb0af774b4d7cffc5b9d046f2aaeade2f37df2f80abf8033c95dfffcc77a8866` |

## Owner OBS CPU acceptance — 2026-10-02

The owner installed the supplied exact Sprint 5 artifact using the provided installation procedure, reported visibly functional MediaPipe CPU background removal, and explicitly confirmed removal/recreation without a crash: “Sim, recriei e funcionou sem crash”.

Acceptance environment:

- OBS Studio `32.2.2`, Windows x64
- Windows version reported by OBS: `10.0 Build 26300`, release `26H2`, revision `9550`
- AMD Ryzen 7 5800X3D, 8 physical / 16 logical cores
- AMD Radeon RX 9070 XT; driver `32.0.31041.3013`
- Owner-provided active-log interval: `2026-10-02 00:38:10` to `00:38:46` local time, Europe/Lisbon

Minimal sanitized plugin evidence from that log:

```text
00:38:10.790: [obs-backgroundremoval] Plugin loaded successfully (version 1.4.1)
00:38:24.153: [obs-backgroundremoval] Background filter created
00:38:24.197: [obs-backgroundremoval] Model models/mediapipe.onnx input 0: name input_1:0 shape (4 dim) 1 x 144 x 256 x 3
00:38:24.197: [obs-backgroundremoval] Model models/mediapipe.onnx output 0: name segment:0 shape (4 dim) 1 x 144 x 256 x 2
00:38:24.200: [obs-backgroundremoval]   Inference Device: cpu
00:38:46.399: [obs-backgroundremoval] Background filter destroyed
00:38:46.400: [obs-backgroundremoval] Background removal filter destructor called
```

The supplied log shows successful plugin load and normal model/tensor initialization without plugin DLL-load, ONNX Runtime, model initialization, or session creation errors. It ends at filter removal; successful recreation and the working mask after recreation are evidenced by the owner's explicit confirmation rather than a second log excerpt. Two initial empty-mask render messages precede normal operation; the visible working mask does not indicate a persistent empty-mask failure. The options dump's `Disabled: true` is printed before the update function clears that state and is not evidence of a disabled filter throughout the test.

The optional PowerShell log-copy step failed because `$work` was null in that shell session. The owner supplied the OBS log directly, so acceptance evidence remained available. Corrected local log-collection instructions use a defined destination; this scripting issue is separate from plugin operation.

Only the sanitized plugin lines and necessary hardware/version facts are retained here. The full owner log, filesystem paths, device identifiers, browser/widget URLs, and unrelated module messages are not durable evidence. This is a MediaPipe CPU compatibility acceptance on the owner's machine; it does not establish production GPU acceleration, a clean-machine deployment result, a long-duration stability result, or native Linux/macOS acceptance.

## Sprint 6 entry decision and next session

The accepted runtime/package is ready for Sprint 6 design work. DirectML is the previously selected primary direction, but the production-provider design, acquisition/deployment policy, lifecycle/fallback behavior, and final scope still require review and owner approval before implementation. Do not begin implementing a provider, loader, acquisition flow, or UI merely because Sprint 5 CPU acceptance passed.

1. Verify publication/readback of this signed documentation-only `GPU` commit, exact local/remote feature SHA, PR draft state, and preserved labels. Keep the tested feature head at `74b0498d197288e54f932b818bb9e857b984db7f`.
2. Reconcile `AGENTS.md`, any applicable local instructions, this handoff, the ignored sprint ledger, worktrees, and current diffs before new work. Preserve the owner's untracked roadmap.
3. Read the approved Sprint 5 documents at the immutable feature links above. Use the historical Sprint 4 comparison to inform the next design, with its older environment clearly distinguished.
4. Explore and document the Sprint 6 provider design, revalidate current version-specific official documentation, and bring one material decision at a time to the owner. Obtain explicit design/implementation approval before code generation for the next sprint.
5. Keep production CPU compatibility intact. DirectML remains unintegrated and `DirectML.dll` remains absent until the next approved integration. No provider acquisition, UI, inference-session, or loader changes are authorized by this handoff.
6. Before a later merge or release, run the full native platform matrix. The historical roadmap (`docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md`, final release checklist) separately requires GPU/model verification, provider-switch and filter-lifetime cycles, clean-machine installation, and long-duration stability/recording tests for eventual release. A green Sprint 5 Windows job and this CPU test do not replace those established release gates.

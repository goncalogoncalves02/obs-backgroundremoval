<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>

SPDX-License-Identifier: GPL-3.0-or-later
-->

# Windows ML AMD project handoff

Snapshot date: 2026-10-03

This document is the durable entry point for continuing the Windows ML AMD work. Sprint 6 source reviews, exact-head Windows CI, independent package inspection, production-core CPU/DirectML/MIGraphX hardware checks, and the owner's combined CPU → DirectML → CPU OBS acceptance have passed. GPU image processing is also accepted on the owner's tested RX 9070 XT setup and published as a Windows preview. Verify live branches, CI and download availability before resuming; stable release and remaining verification gates remain separate.

## Current repository and branch state

- Repository: `goncalogoncalves02/obs-backgroundremoval`.
- Current integration branch: this fork's `main`, verified at `35c923155680ae17552f35b6b16aa74a5d6d920d` before this documentation update.
- Current documentation work: `docs/english-amd-documentation`, based on that merged `main`; documentation-only PR requested by the owner.
- Historical documentation branch: `GPU`, verified at `1899a7de87949f0a68a4d5eb31c5e7f890e1f374`; its acceptance/publication records are brought into this documentation PR without copying runtime changes.
- Accepted Sprint 6 implementation: `feature/windows-ml-amd-sprint-6`, source `a390fcb4f2f6cdd9beba14e3024edc41b7860cc0`.
- [PR #2](https://github.com/goncalogoncalves02/obs-backgroundremoval/pull/2) is merged/closed as of 2026-10-03; its implementation is included in the merged PR #3 source.
- Production-core hardware evidence uses `5f1b52fcb521825014e5dff5d5289ee82a8c66e4`; the later accepted source adds the bounded CPU/DirectML selector/status and Portuguese locale packaging without changing that backend.
- Accepted Sprint 5 remains on `feature/windows-ml-amd` at `74b0498d197288e54f932b818bb9e857b984db7f`; [PR #1](https://github.com/goncalogoncalves02/obs-backgroundremoval/pull/1) is merged/closed as of 2026-10-03.
- The owner merged [PR #3](https://github.com/goncalogoncalves02/obs-backgroundremoval/pull/3) into this fork's `main` at `35c923155680ae17552f35b6b16aa74a5d6d920d` on `2026-10-03T21:00:10Z`. The feature is now integrated in the fork. The [Windows preview](windows-ml-amd-release.md) retains the original accepted source and bytes; no upstream integration or stable release is recorded.
- This English-documentation branch contains documentation only. Preserve the owner's untracked files, including `.aws`; do not inspect, stage, rewrite or delete them.

## GPU image processing delivery and owner acceptance

The owner approved the two-stage Windows + MediaPipe + DirectML design and implementation plan on 2026-10-03. The software implementation and controlled comparison delivery are verified, and the owner has now supplied the real OBS visual/lifecycle and whole-OBS CPU result. This extends the accepted inference route with GPU input downscaling and GPU mask resizing/smoothing/expansion/feather. Initial contours remain on the small CPU mask; direct OBS–DirectML memory sharing is deferred. Image Similarity enabled still requires full-size CPU readback.

The [sanitized owner acceptance record](windows-ml-gpu-image-processing-acceptance.md) reports all four valid OFF → ON → ON → OFF blocks with effective DirectML: mean process CPU **7.744624% OFF → 1.683161% ON**, a **6.061463 percentage-point / 78.266721% relative reduction**, with Image Similarity OFF. Mask/hair/movement, switching, resize and filter recreation were explicitly accepted without a crash. This is a sequential observation on the owner's RX 9070 XT scene, without statistical confidence or a general livestream performance guarantee. After the test the owner separately observed CPU rise with Image Similarity enabled; that unquantified observation is consistent with its retained full-size CPU readback/comparison and is not part of the measured reduction.

- Feature branch: `feature/windows-ml-amd-gpu-image-processing`.
- Final signed source: `8da27a2557c3b89854f48df702888c94557c720d`.
- [Approved design](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/8da27a2557c3b89854f48df702888c94557c720d/docs/superpowers/specs/2026-10-03-windows-ml-amd-gpu-image-processing-design.md), [implementation plan](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/8da27a2557c3b89854f48df702888c94557c720d/docs/superpowers/plans/2026-10-03-windows-ml-amd-gpu-image-processing.md) and [usage/evidence limits](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/8da27a2557c3b89854f48df702888c94557c720d/docs/windows-ml-gpu-image-processing.md).
- Independent task reviews, one whole-source review, its consolidated correction review and a narrowly declared terminal-log-boundary follow-up review are complete. No open source finding remains. Ignored reports and all original/failed receipts are preserved in the existing feature worktree under `.superpowers/sdd/2026-10-03-windows-ml-amd-gpu-image-processing`; do not restart completed tasks or discard evidence.
- Exact [Windows run 37146350991/job111270993063](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37146350991/job/111270993063) and [Check CI 37146350682](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37146350682) passed.
- Native verification includes actual plugin compile/link, policy/packet/shared-comparator tests, software D3D11 graphics, 48 model/portrait quality comparisons, 64 actual filter callback boundaries and four destruction cycles. Windows PowerShell 5.1 executes measurement/collector/real-ZIP transaction faults, including all 17 collector boundary/acquisition cases; generator and exact-source renderer also pass.
- Across the 48 quality cases: maximum input MAE `0.000970215`, maximum final-mask MAE `0.000119955`, minimum physical retained-alpha IoU `0.999153`, minimum positive-background IoU `0.999823`. Both IoU gates require at least `0.98`; both MAE gates require at most `0.01`. The original model saturation remains unchanged.
- Eligible full-filter DirectML allocation-failure branches were explicitly unexecuted on the software runner after an actual CPU fallback. These native results do not establish RX 9070 XT execution, hair/motion acceptance or any CPU saving.

For installation, prefer the [published Windows preview](https://github.com/goncalogoncalves02/obs-backgroundremoval/releases/tag/1.4.1-amd-directml-preview.1) and the [English usage guide](windows-ml-gpu-image-processing.md). The original delivery record identifies exactly one [reviewed handoff ZIP](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37146350991/artifacts/11283076308): `obs-backgroundremoval_gpu-image-processing_x64.zip`, artifact `11283076308`, `59255402` bytes, SHA-256 `f2f474090400a0d8b6ecf8af7c996355806a331d355fb5f72b01bb6d5fda2741`. It contains the unchanged original plugin ZIP and the concrete installer plus comparison script. Original plugin artifact `11282342195`: `59193409` bytes, SHA-256 `19797093a9acc24475d2d2eda7e3fded2aac8e0c7462a88d8b60bd18c7ca91bf`. Both actual downloads match GitHub digests and exact source/run metadata; extracted model/effect/script bytes and the rendered manifest match immutable Git/package bytes. One Windows ML 2.2.12 ONNX Runtime and its API/DirectML/legal files retain their pinned origins. Artifact retention is finite; refresh availability before a future download.

Close OBS and run the extracted installer in administrator PowerShell. It preserves the actual previous tree and receipt outside OBS search paths; `-Acao Restaurar` restores that prior version. Reopen OBS with MediaPipe + GPU DirectML, then run the extracted comparison script. The saved GPU image-processing checkbox defaults OFF; OFF and ON both retain DirectML inference. The script guides OFF → ON → ON → OFF, with 5 seconds settling and 15 seconds actual process-CPU measurement per block, plus bounded post-end log observation separately disclosed. CPU reads use actual acquisition timestamps and logical-processor normalization; late/slow reads, missing closing logs, fallback, changed settings/source/process or incomplete records make the comparison inconclusive. Host timing and OBS telemetry intervals are disclosed separately; unavailable network-drop counters are not invented. No statistically established or substantial saving is promised.

Owner comparison and visual/motion/lifecycle acceptance are now recorded; keep complete logs local. The tested usage configuration is MediaPipe + GPU DirectML, GPU image processing ON, Image Similarity OFF. Further similarity optimization needs a separately agreed scope. The owner subsequently requested fork PR #3 and a downloadable Windows preview, now published with the same verified ZIP and instructions; see the [publication record](windows-ml-amd-release.md), including the completed PR #3 native matrix and the inherited automatic release tag-validation failure. The owner subsequently merged PR #3; this documentation update itself changes no runtime code or installation. Explicit eligible DirectML allocation-failure testing, long-duration streaming and broader AMD/Windows compatibility remain separate verification limits.

## Current acceptance and scope

The accepted Sprint 6 selector build (before GPU image processing) passed the owner's fresh-install live OBS test: CPU → DirectML → CPU with MediaPipe, visible working mask and GPU status, then filter removal/recreation without a crash. The collector reported 3 CPU sessions, 9 DirectML sessions, 0 CPU fallbacks, 2 destructions and 0 relevant errors. Complete owner logs remain local; this record uses the supplied sanitized collector summary and explicit visual answers. At that earlier stage the owner observed no substantial visible reduction in overall CPU percentage; it was not a controlled measurement. The later GPU image-processing acceptance above establishes a separate measured whole-OBS CPU result. See the [DirectML guide](windows-ml-directml-ui.md) and [hardware acceptance](windows-ml-gpu-image-processing-acceptance.md).

The owner approved the bounded Background Removal CPU/DirectML selector and effective-session status on 2026-10-02 (“Sim, implementa assim”). That approval supersedes the original Sprint 6/Sprint 7 UI boundary only for this extension, and the combined live test replaces the separate CPU-only OBS gate. It does not authorize a full next sprint. CPU remains the default and alternative; MIGraphX remains an optional implemented backend without a UI choice. Enhancement UI, model expansion, adapter selection, provider acquisition in OBS and loader/search-path hooks are not implemented by this extension.

## How the work is organized

Architectural work uses independently reviewable sprints, task-scoped implementers and independent reviews, followed by whole-sprint review and exact-head verification. Durable plans/specifications live under `docs/superpowers/`; transient reports, logs, artifacts and receipts stay under ignored `/.superpowers/`.

Current immutable feature documents:

- [Sprint 6 approved session design](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/a390fcb4f2f6cdd9beba14e3024edc41b7860cc0/docs/superpowers/specs/2026-10-02-windows-ml-amd-sprint-6-design.md).
- [Sprint 6 implementation plan](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/a390fcb4f2f6cdd9beba14e3024edc41b7860cc0/docs/superpowers/plans/2026-10-02-windows-ml-amd-sprint-6.md).
- [Bounded DirectML UI usage and owner approval](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/a390fcb4f2f6cdd9beba14e3024edc41b7860cc0/docs/windows-ml-directml-ui.md).

The plan retains its original pre-execution status, and both documents retain the original UI boundary wording; the explicit bounded approval and accepted combined gate above supersede those historical statements. Do not copy feature code or these feature documents onto `GPU` to follow the links.

Historical foundation references on `GPU`: `docs/superpowers/specs/2026-08-24-windows-ml-amd-design.md`, [historical roadmap](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/1899a7de87949f0a68a4d5eb31c5e7f890e1f374/docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md), Sprint 1–4 plans, `docs/windows-ml-baseline.md`, `docs/windows-ml-provider-discovery.md` and `docs/windows-ml-inference-comparison.md`. Later approved decisions supersede the initial roadmap's provider priority and dependency assumptions.

Historical immutable Sprint 5 documents:

- [Build and packaging specification](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/74b0498d197288e54f932b818bb9e857b984db7f/docs/superpowers/specs/2026-08-30-windows-ml-amd-sprint-5-design.md).
- [Implementation plan](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/74b0498d197288e54f932b818bb9e857b984db7f/docs/superpowers/plans/2026-08-30-windows-ml-amd-sprint-5.md).

Contribution/maintainer, signing and DCO prerequisites were explicitly confirmed for this execution. Project commits use the owner's identity, signing key `460B18400D17462FF3714A2BDCED30418A1C06FC` and DCO, without assistant/co-author attribution.

## Sprint 6 completed source and acceptance

Production Windows sessions share the tested provider module and session core. CPU and both exact GPU routes have truthful diagnostics; eligible GPU initialization prohibits node-level CPU fallback, while initialization failure may create one fresh, explicitly reported CPU session. Unknown identifiers fail, unsupported GPU models report CPU fallback, and failed metadata/allocation leaves no stale usable session. Model-lock serialization and session-before-environment destruction are preserved.

The plugin ZIP now requires pinned `DirectML.dll` alongside the Windows ML API and its single ONNX Runtime. The bounded Windows Background Removal selector exposes CPU and DirectML, recreates sessions through existing serialized initialization and displays the completed effective provider or failure. No provider acquisition runs in OBS. Non-Windows standalone ONNX Runtime behavior remains unchanged; the later PR #3 matrix verified native Windows, Linux and macOS builds at the complete image-processing source; its macOS Intel package scenario was skipped by workflow.

Independent task/whole-sprint source reviews and the scoped UI review passed. Exact source `a390fcb4f2f6cdd9beba14e3024edc41b7860cc0` passed Check CI `37070839666` and PR Check `37070840029` / Windows job `111049640434`, including actual `/WX` plugin compilation, 64 ordered adapter lifecycles, native tests and package verification. Both GPU routes and CPU passed the same production-core hardware harness; the new exact plugin separately passed the owner's combined OBS gate. The historical session limits are described in the DirectML usage guide; the later image-processing acceptance and publication records above cover the current build.

## Historical completed source work — Sprints 1–5

The following sections preserve earlier accepted evidence. Sprint 5's absence of DirectML and provider UI describes that historical source/package only; Sprint 6 above supersedes that production state.

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

These results established DirectML as the selected primary direction for later production GPU integration, with MIGraphX a validated secondary provider. Sprint 5 deliberately limited production integration to the runtime/build/package and CPU compatibility. That historical decision was implemented and accepted in Sprint 6; see the current integration record.

Historical exact-head evidence for Sprint 4 feature head `bbd46cf7f1e3f3125dec85850534f47760b89790`:

- Check CI: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849461>
- PR Check: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543>
- Smoke artifact: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543/artifacts/9545182351>
  - SHA-256: `51b7fc856e36f3e6182a9e83998e034a91db25e62262cabbbd13d68e420e2a10`
- Plugin artifact: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543/artifacts/9545266577>
  - SHA-256: `78d359cfa4ca8d3b0adc2d985759627993745dd6fa7c38792a9512f5e5230ec9`

That historical plugin artifact predates Sprint 5 and uses standalone Windows ONNX Runtime. The Sprint 5 artifact below records the historical Windows ML CPU acceptance; use the current Sprint 6 integration record for the accepted DirectML package.

## Historical Sprint 5 exact-head CI and artifact evidence

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

## Historical Sprint 5 owner OBS CPU acceptance — 2026-10-02

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

## Next session and remaining gates

1. Reconcile `AGENTS.md`, this handoff, the current GPU image-processing ledger, worktrees and diffs. Preserve owner files, accepted Sprint 5/Sprint 6 heads and the accepted image-processing source/results; do not restart completed implementation or historical hardware checks.
2. Verify the English-documentation PR, current fork `main`, retained accepted sources and published preview assets. Refresh live CI and release metadata; Actions artifact retention is finite.
3. Continue using MediaPipe with DirectML or CPU on the accepted setup. Consult the DirectML usage guide and hardware acceptance record when diagnosing effective-provider status; MIGraphX remains optional without a selector.
4. Obtain new owner direction and any required design approval before implementing the next sprint or expanding UI/models. The owner authorized fork PR #3 and Windows preview publication, then merged PR #3 themselves. This update requests a documentation-only PR; it does not authorize an agent merge, stable release or broader deployment.
5. PR #3's full native matrix completed successfully at source `8da27a2557c3b89854f48df702888c94557c720d`: eight platform builds and five package scenario jobs passed; the macOS Intel package scenario was skipped by workflow. Clean-machine installation, long-duration stability, recording, broader AMD/Windows compatibility and explicit eligible GPU fault tests remain separate verification limits. Recheck the current candidate before a future stable release; the Windows preview does not establish those remaining results.

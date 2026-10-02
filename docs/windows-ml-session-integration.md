<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Windows ML session integration and acceptance

Snapshot: 2026-10-03, Europe/Lisbon. Sprint 6 and the separately approved bounded CPU/DirectML UI have passed source review, exact-head native Windows CI, independent artifact verification, production-core hardware checks and the owner's combined live OBS gate. This establishes MediaPipe DirectML use on the tested RX 9070 XT setup; full platform and release acceptance remain pending.

## Accepted scope and source

- Accepted plugin source: `a390fcb4f2f6cdd9beba14e3024edc41b7860cc0`, `feature/windows-ml-amd-sprint-6`, [draft PR #2](https://github.com/goncalogoncalves02/obs-backgroundremoval/pull/2).
- Hardware harness source: `5f1b52fcb521825014e5dff5d5289ee82a8c66e4`. The later source changes the bounded selector/status and locale packaging; the accepted session backend is unchanged.
- [Approved Sprint 6 design](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/a390fcb4f2f6cdd9beba14e3024edc41b7860cc0/docs/superpowers/specs/2026-10-02-windows-ml-amd-sprint-6-design.md), [implementation plan](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/a390fcb4f2f6cdd9beba14e3024edc41b7860cc0/docs/superpowers/plans/2026-10-02-windows-ml-amd-sprint-6.md) and [DirectML UI usage](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/a390fcb4f2f6cdd9beba14e3024edc41b7860cc0/docs/windows-ml-directml-ui.md).

The owner approved the bounded Background Removal CPU/DirectML selector and completed-session status (“Sim, implementa assim”). That approval supersedes the original documents' Sprint 7 UI reservation only for this extension. The combined CPU → DirectML → CPU live test replaces the separate CPU-only OBS acceptance gate.

CPU remains the default and alternative. MediaPipe is the first supported GPU model. DirectML requests for other recognized models use an explicitly reported CPU fallback; GPU initialization failures likewise permit one fresh CPU session. GPU-labelled sessions prohibit node-level CPU fallback. Unknown Windows identifiers fail, and later metadata/allocation failures clear session state rather than claiming a ready provider. Session replacement/inference remains serialized, with session destruction before its environment.

MIGraphX remains an optional implemented backend without a UI choice. Enhancement UI, model expansion, adapter selection, runtime loader/search-path hooks and automatic GPU cascades were not added. Provider acquisition/preparation is an explicit external operation; OBS does not download or install providers. Linux/macOS retain their standalone ONNX Runtime behavior.

## Exact-head Windows CI and verified artifacts

At accepted source `a390fcb4f2f6cdd9beba14e3024edc41b7860cc0`:

- [Check CI `37070839666`](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37070839666): success.
- [PR Check `37070840029`](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37070840029): success; [Windows job `111049640434`](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37070840029/job/111049640434): success.
- Portable tests: 6/6; native core tests: 3/3; native Python/PowerShell fixtures: 25 passed, no skips, 27.978 seconds. Actual plugin `/WX` compile, install and ZIP checks passed. The native plugin adapter completed 64 ordered lifecycle cycles in 3.19 seconds.
- Independent task, whole-sprint and scoped correction/UI source reviews passed. Generic CI proves CPU and controlled failure paths, not AMD hardware success.

| Artifact | ID and immutable-run download | SHA-256 |
| --- | --- | --- |
| `obs-backgroundremoval_1.4.1.dll.zip` | [11253834312](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37070840029/artifacts/11253834312) | `d5363d7f66959aeaf851cf11d188659d66ba8aaa957a87c8d9fc5f80dbe547fc` |
| `windows-ml-session-check_2.2.12_x64.zip` | [11255061355](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37070840029/artifacts/11255061355) | `f58460b9833f16176da3d49c5e70599d5e15aa1b71491f19251d4837e40c080f` |
| `windows-ml-smoke_2.2.12_x64.zip` | [11254263600](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37070840029/artifacts/11254263600) | `8e476991234725945251e52642f53efba34648e0f2a893eb997d6d447f36bce9` |

Downloaded ZIP hashes matched GitHub digests and source receipts. The plugin ZIP has 46 entries; plugin DLL SHA-256 is `ce79f556c48db19090bc198e4868094ce3f74739d7c2c36f3969355bd1347d83`. Independent install-tree/archive checks passed safe paths, unique adjacent runtime DLLs, exact pinned DLL/legal origins, tracked MediaPipe and English/Portuguese locales. Locale comparison normalized checkout line endings only; DLL/model/legal comparisons were byte exact.

Windows uses the self-contained Windows ML 2.2.12 package with embedded ONNX Runtime 1.25.2. The [pinned NuGet package](https://api.nuget.org/v3-flatcontainer/microsoft.windows.ai.machinelearning/2.2.12/microsoft.windows.ai.machinelearning.2.2.12.nupkg) independently matched SHA-256 `9cb60543337e6e4eac2a95c2fcb9650a880697ae7190d15499d801c907849da3`. The plugin contains one ONNX Runtime, plus adjacent `Microsoft.Windows.AI.MachineLearning.dll` and `DirectML.dll` from that package and its license/notices. Tool manifests/source, x64 executables, model and dependency origins were independently verified. Artifact retention is finite; check availability before future downloads.

## Production-core RX 9070 XT hardware acceptance

Owner-provided sanitized summaries report CPU, DirectML and MIGraphX passing the same production core used by the plugin at source `5f1b52fcb521825014e5dff5d5289ee82a8c66e4`. Each provider completed three independent session cycles, each with 10 warm-ups and 100 timed inference calls, exit 0 and the exact requested effective provider. Both GPU routes selected RX 9070 XT, vendor `0x1002`, device `0x7550`, with no passing CPU fallback.

The tracked MediaPipe model used input `1x144x256x3` and output `1x144x256x2`, with finite output. All cycles passed normalized MAE ≤ 0.05 and binary-mask IoU ≥ 0.95:

| Provider | Mean inference latency | Normalized MAE | Foreground IoU |
| --- | --- | --- | --- |
| CPU | 2.504173 ms | 0 | 1.000000 |
| DirectML, fresh comparison | 0.399778 ms | 0.00000766 | 1.000000 |
| MIGraphX, fresh comparison | 1.091339 ms | 0.00000722 | 1.000000 |

DirectML was 2.73× faster than MIGraphX in the fresh sequential comparison on the same harness. Against the CPU mean from an earlier invocation, DirectML was 6.26× and MIGraphX 2.29× faster. These are model-inference timings, not whole-OBS CPU utilization, FPS or recording performance. The fresh complete MIGraphX stage took 172.3 seconds and DirectML 2.2 seconds; these wall times include startup, sessions, CPU references and teardown, and do not isolate compilation/initialization cost. An earlier 180-second runner timeout was superseded by a passing targeted test; its internal cause remains unproven.

Hardware environment reported by the owner: AMD Radeon RX 9070 XT, driver `32.0.31041.3013`, Windows `26300.9550` / `26H2`; the earlier MIGraphX timeout diagnostic reported Windows PowerShell `5.1.26100.9549`. The final benchmark/OBS summaries did not supply an exact shell version. Complete benchmark logs remain on the owner's machine; the durable record is based on the supplied summaries.

Recorded harness invocations from a hash/manifest-verified extracted directory containing its model and runtime files; they document the completed acceptance contract, with no repeat requested:

```powershell
foreach ($provider in @('cpu', 'winml-directml', 'winml-migraphx')) {
    & .\windows-ml-session-check.exe --provider $provider --model .\mediapipe.onnx --iterations 100 --cycles 3 --require-effective
    $nativeExit = $LASTEXITCODE
    if ($nativeExit -ne 0) { throw "$provider failed: exit $nativeExit" }
}
```

Expected: exit 0, three successful cycles, exact effective provider, shapes/finite/correctness gates passed. MIGraphX preparation, when required, is run separately from OBS in the verified smoke directory using `windows-ml-smoke.exe --prepare-provider MIGraphXExecutionProvider`; its catalog/preparation checks passed in the owner run. DirectML used its visible-device route without requiring catalog registration. These commands document the accepted harness contract; current-source artifacts were verified in CI but the hardware benchmarks were not rerun after the bounded UI addition.

## Exact plugin combined live OBS acceptance

The owner freshly installed the verified plugin from source `a390fcb4f2f6cdd9beba14e3024edc41b7860cc0`, used MediaPipe, switched CPU → DirectML → CPU, and removed/recreated the filter. The reviewed collector verified the installation receipt/source and plugin DLL, then read a fresh OBS log and matched DirectML success on the exact RX 9070 XT vendor/device IDs. Visual mask, displayed GPU status, recreation and crash outcome are the owner's four explicit answers.

Collector command used in the new terminal, with the supplied reviewed `instalar-directml.ps1` in Downloads:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\instalar-directml.ps1" -Acao Recolher
if ($LASTEXITCODE -ne 0) { throw "OBS collection failed: exit $LASTEXITCODE" }
```

Script SHA-256: `b6a3e435e5fb02537d819bde0c2a660fc98b4fdfbcf476b8b5a17f262d5e2f14`. It is retained with the ignored execution evidence, not published as part of this documentation branch.

Owner-supplied collector summary:

| Check | Result |
| --- | --- |
| Source | `a390fcb4f2f6cdd9beba14e3024edc41b7860cc0` |
| Overall / mask / GPU status / recreation | PASS / True / True / True |
| Crash | False |
| Ready sessions | CPU: 3; DirectML: 9 |
| DirectML fallback to CPU | 0 |
| Filter destructions / relevant errors | 2 / 0 |
| OBS | 32.2.2, Windows 64-bit |
| Log marker / collection timestamp | 23:48:48.581 / 2026-10-02 23:52:33, Europe/Lisbon |

For this fresh installation the owner reported no existing plugin version to back up. No previous-version backup was reported; the exact script backup directory was not shared. Do not invent a restoration location from the summary. The installation script keeps its receipt and any backup outside OBS search paths; a future restore must use the actual local receipt.

The owner also observed no substantial visible reduction in overall CPU percentage when switching to DirectML. This is an owner observation, not a controlled CPU-utilization measurement. Camera handling, input preparation and mask processing still include CPU work; no dominant bottleneck was measured. Effective DirectML GPU use and the visual gate passed, but a material reduction in whole-OBS CPU use was not demonstrated; the 6.26× inference result does not establish that benefit.

Only the sanitized collector summary was shared. Raw OBS logs, account paths, widget/browser URLs and unrelated identifiers remain local and are not published here. Session counts describe the collected log, not a timed stress test.

## Remaining integration and release gates

Native Linux/macOS execution, the full platform matrix, clean-machine installation, long-duration stability and recording tests remain pending for integration/release. Broader GPU/model coverage and any further UI need new direction/design approval. Accepted Sprint 5 source/PR #1, `GPU` and `main` remain separate; no merge, public release or deployment is implied. The owner can use MediaPipe DirectML now on the accepted setup, with CPU as the alternative.

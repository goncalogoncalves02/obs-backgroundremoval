<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# GPU image processing — owner OBS acceptance

Acceptance date: 2026-10-03. Tested implementation: `8da27a2557c3b89854f48df702888c94557c720d` on `feature/windows-ml-amd-gpu-image-processing`. This record uses the owner's supplied terminal summary and explicit visual answers; complete logs and samples remain local. It supplements the [delivery handoff](windows-ml-amd-session-handoff.md), without changing feature code. The subsequent preview publication and owner merge of PR #3 are recorded in the [publication record](windows-ml-amd-release.md).

## Environment and comparison

- OBS 32.2.2, Windows 10.0.26300.0 as reported by the collector.
- AMD Ryzen 7 5800X3D, 16 logical processors; AMD Radeon RX 9070 XT, driver 32.0.31041.3013.
- Source 1280×720, MediaPipe input 256×144, OBS 60/1 FPS.
- MediaPipe with `DmlExecutionProvider` in all four blocks, including image processing OFF.
- GPU image processing OFF → ON → ON → OFF; Image Similarity OFF throughout the measured comparison.
- The owner reports moving their head from side to side throughout the test.

CPU percentages refer to the entire OBS process, normalized over the 16 logical processors. Each block has five seconds settling and fifteen seconds measurement. Log observation and rendering telemetry have separately disclosed boundaries.

| GPU image processing | Mean OBS CPU | Range of the two block means |
| --- | --- | --- |
| OFF | 7.744624% | 7.467349–8.021899% |
| ON | 1.683161% | 1.645502–1.720819% |

The reduction is **6.061463 percentage points**, or **78.266721% relative to OFF**, in this sequential comparison. The collector classified it `ObservedImprovement`: the two ON block means are below both OFF block means. This is an observed result on this scene/hardware, without statistical confidence or a promise for other scenes, games or streams. It compares GPU image processing OFF/ON with DirectML inference in both modes, not CPU inference against DirectML.

All four blocks were valid, retained the same OBS process and reported effective DirectML. OFF reported processing state `Off`, with both GPU processing stages inactive; ON reported `Active`, with both stages active. Maximum CPU-query acquisition time across the blocks was 8.3818 ms, below the script's 50 ms gate. Similarity readback pixels were zero throughout.

For complete telemetry intervals inside the CPU windows:

| Block | Mode | Captured | Processed | Input readback pixels | Similarity readback pixels |
| --- | --- | --- | --- | --- | --- |
| 1 | OFF | 1202 | 601 | 1107763200 | 0 |
| 2 | ON | 1202 | 601 | 44310528 | 0 |
| 3 | ON | 1204 | 602 | 44384256 | 0 |
| 4 | OFF | 1204 | 602 | 1109606400 | 0 |

Readback per captured frame falls from 921600 pixels to 36864 pixels: **25 times fewer pixels (96% reduction)**, consistent with downscaling 1280×720 to 256×144 before CPU readback. Skipped/stale frames in these reported complete intervals were zero.

## Visual and lifecycle acceptance

The owner answered yes to all four prompts:

- Mask, hair/edges and movement retained quality and response without new flicker or halos.
- Checkbox ON/OFF and CPU → DirectML → CPU switching worked without a crash.
- Source-size changes retained mask alignment.
- Removing and recreating the filter restored the working mask.

Reported rendering intervals had zero lagged frames in every block. Their elapsed intervals were approximately 15.033, 10.034, 15.050 and 10.033 seconds respectively; they do not establish zero rendering lag for the entire exact CPU windows. Dropped/network-frame evidence was unavailable. The accepted result is a short live visual/lifecycle and process-CPU comparison, not a long-duration livestream, fault-injection or full platform-matrix acceptance.

## Separate observation: Image Similarity

After the measured test, the owner enabled **Skip image based on similarity** together with GPU image processing and observed CPU percentage rise. No controlled numerical result was supplied for this combination; do not attach the 78.27% reduction to it.

This observation is consistent with the current compatibility path: Image Similarity requires an additional full-size source readback, CPU comparison against the previous image, and history copying when the image is not skipped. Motion can limit the inference work avoided by that check. This is an explanation from the implementation, not a measured attribution of the owner's additional CPU cost or evidence of CPU inference fallback.

For the configuration measured here, use MediaPipe + GPU DirectML, GPU image processing ON and Image Similarity OFF. Optimizing the similarity algorithm or moving it to the GPU requires a separately agreed scope; no such implementation is recorded by this acceptance.

## Remaining limits

The reviewed source, exact Windows CI and verified artifact/package evidence remain recorded in the delivery handoff. This hardware result establishes the normal active DirectML image-processing path and the reported user operations. It does not execute the unavailable native-runner DirectML allocation-failure branches, direct OBS–DirectML memory sharing, clean-machine deployment, or long-duration streaming tests. The subsequent PR #3 native Windows/Linux/macOS matrix passed, with the macOS Intel package scenario skipped; that is separate CI evidence rather than owner hardware coverage. Previous signed feature sources and local backups/receipts are preserved.

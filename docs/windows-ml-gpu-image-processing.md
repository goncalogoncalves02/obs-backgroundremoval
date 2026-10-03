<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Windows DirectML and GPU image processing

The Windows implementation is available in this fork's `main` after [PR #3](https://github.com/goncalogoncalves02/obs-backgroundremoval/pull/3) was merged on 2026-10-03. The downloadable [Windows x64 preview](https://github.com/goncalogoncalves02/obs-backgroundremoval/releases/tag/1.4.1-amd-directml-preview.1) contains the exact source build tested on the owner's Radeon RX 9070 XT. See the [hardware acceptance](windows-ml-gpu-image-processing-acceptance.md) and [publication record](windows-ml-amd-release.md) for evidence and limits.

## Install the Windows preview

1. Download **obs-backgroundremoval_gpu-image-processing_x64.zip** from the release's Assets. This is the installation bundle, rather than GitHub's automatically generated source-code ZIP.
2. Extract the three files into **Downloads\obs-br-gpu**. Keep the embedded **obs-backgroundremoval_1.4.1.dll.zip** intact.
3. Close OBS. Open PowerShell as administrator and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\obs-br-gpu\instalar-processamento-gpu.ps1"
```

The installer verifies the ZIP and each installed file, and preserves the previous plugin tree and receipt outside OBS plugin search paths. It does not acquire providers. The script names and action parameters remain Portuguese because they belong to the already verified package; keep them exactly as shown.

To restore the previous installation, close OBS and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\obs-br-gpu\instalar-processamento-gpu.ps1" -Acao Restaurar
```

## Enable the accepted configuration

Open OBS and add **Background Removal** to the webcam source's filters. Open advanced settings to select **MediaPipe**. Set **Inference device** to **GPU - DirectML**, enable **GPU image processing**, and disable **Skip image based on similarity?** for the configuration measured below.

The inference status should show **Processor: GPU - DirectML**. GPU image processing has its own status: **GPU image processing: input and mask active** confirms both stages. CPU is the default inference choice, and the GPU image-processing checkbox defaults OFF. Choosing DirectML alone does not enable GPU image processing.

The checkbox moves source-image downscaling and mask resizing/smoothing/expansion/feather to GPU processing. Initial contour filtering remains on the small CPU mask. Both checkbox OFF and ON can use DirectML inference; OFF retains the earlier CPU image-processing path.

GPU image processing requires Windows, MediaPipe and a completed effective DirectML session. Another model or unavailable inference provider can produce an explicitly displayed CPU inference fallback. A GPU **image-processing** failure can keep DirectML inference while using CPU image processing until reinitialization. Read both status messages rather than assuming the requested device is active. See [DirectML session selection](windows-ml-directml-ui.md).

## Why leave image similarity OFF?

**Skip image based on similarity?** still reads the complete source image back to the CPU and compares it with image history. With movement, that work can cost more than the inference it avoids. The owner separately observed CPU usage rise after re-enabling it, but supplied no controlled numeric result for that combination.

For the accepted result, similarity was OFF in both modes. Moving similarity comparison to the GPU and direct OBS–DirectML memory sharing are future changes, not features of this preview.

## Optional CPU comparison

The feature is usable without running the comparison script. To measure your own scene, keep OBS open with MediaPipe + GPU - DirectML selected and run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\obs-br-gpu\testar-processamento-gpu.ps1"
```

The script prompts you to set the checkbox **OFF → ON → ON → OFF**. Change it manually, then press Enter in the terminal. Each block has five seconds settling followed by fifteen seconds CPU measurement. It observes logs for up to 6.2 additional seconds to cover the measurement end and catch late failures; that observation does not extend the CPU window. Keep the source, lighting, movement, resolution/FPS, mask settings and other workload comparable.

The default comparison asks you to disable image similarity equally in both modes and reminds you to restore its previous setting. To test your usual similarity setting separately, add **-ManterSimilaridade**. Do not combine that result with the similarity-OFF comparison.

Normalized CPU is `100 × CPU-seconds delta / (elapsed monotonic seconds × logical processors)`. The result measures the whole OBS process, including other sources and scene work. Actual OS reads are timestamped; acquisition longer than 50 ms or endpoint drift beyond 50 ms invalidates a block.

The summary reports OFF/ON means, ranges, percentage-point difference and relative change. Separated block ranges indicate an observed improvement in this sequential comparison, without statistical confidence. Overlapping ranges, OBS restart, changed source/settings, missing or incomplete logs, or fallback can make the result inconclusive. Component timings are host elapsed time, including waits, rather than GPU execution time. Rendering telemetry has separately disclosed boundaries; unavailable network/drop counters are not estimated.

Quality, movement, switching, source resize and filter recreation are confirmed separately. Share only the terminal summary; complete logs, samples, backup receipts and personal paths stay local. When reporting a problem, include the OBS version, GPU/driver, requested/effective processor and exact error.

## Accepted result and current limits

On OBS 32.2.2, Ryzen 7 5800X3D (16 logical processors), RX 9070 XT driver 32.0.31041.3013 and Windows build 26300, the four valid blocks reported:

| GPU image processing | Mean OBS CPU | Range of the two block means |
| --- | --- | --- |
| OFF | 7.74% | 7.47–8.02% |
| ON | 1.68% | 1.65–1.72% |

This is **6.06 percentage points / 78.27% relative reduction** on that scene, with DirectML inference and similarity OFF in both modes. Source readback fell from 1280×720 to 256×144: 25 times fewer pixels per captured frame. The owner confirmed mask/hair/edge quality while moving their head, switching without crashes, resize alignment and filter recreation. It is not a performance guarantee for other machines, games or scenes.

| Verification | Result |
| --- | --- |
| Signed implementation and source reviews | Complete at `8da27a2557c3b89854f48df702888c94557c720d` |
| Exact Windows CI, PowerShell 5.1 and package origins | Passed |
| Preview downloads and hashes | Verified publicly without authentication |
| RX 9070 XT quality and short lifecycle test | Accepted |
| Controlled CPU OFF/ON comparison, similarity OFF | Observed improvement |
| Full PR #3 matrix | Successful; macOS Intel package scenario skipped by workflow |
| Similarity-ON controlled performance | Not measured |
| Rendering/dropped-frame evidence | Zero lag in reported intervals; network/drop evidence unavailable |
| Long-duration livestreams, broader AMD/Windows compatibility, eligible GPU fault tests | Not established |

Linux/macOS retain the existing standalone ONNX Runtime path. This release distributes Windows x64 files only. The original automatic Release CD rejected the preview tag suffix before build/upload; the manual release and its downloads are verified. Its recorded failure must not be mistaken for a failed plugin build. See the [publication record](windows-ml-amd-release.md).

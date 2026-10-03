<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# GPU image processing for Windows MediaPipe

**Date:** 2026-10-03

**Status:** Written specification approved by the owner on 2026-10-03 (“Aprovo a especificação”) for implementation planning. The owner also approved the written plan on 2026-10-03 (“Aprovo, podes avançar”); scoped implementation is authorized.

## Purpose and accepted scope

Reduce the CPU cost surrounding MediaPipe inference, so the owner can use background removal during livestreams with more CPU capacity available. DirectML inference already works, but the owner observed no substantial change in overall CPU percentage. Success requires measuring the complete filter/process, preserving usable masks and avoiding a material rendering regression; faster inference alone is insufficient.

The owner approved two stages: GPU downscaling before CPU readback, then GPU mask resizing, edge smoothing, expansion/shrink and feather. Small-mask contour filtering initially stays on CPU. An on/off comparison uses DirectML in both modes, with one PowerShell script organizing the measurement. Direct OBS–DirectML GPU-memory sharing is deferred. The approval was “Concordo perfeitamente”, followed by “Aprovo os dois primeiros assim”.

Baseline: accepted source `a390fcb4f2f6cdd9beba14e3024edc41b7860cc0`, isolated new branch `feature/windows-ml-amd-gpu-image-processing`. Preserve the existing Sprint 6 head and installed artifact. The [acceptance record](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/c10f53d2cbd12b3543d64ae07cfe7a0100c3e90f/docs/windows-ml-session-integration.md) remains the hardware baseline. Windows ML 2.2.12 and its single ONNX Runtime remain pinned; CPU/provider fallback contracts remain unchanged.

## Approach and boundaries

Implement the two stages sequentially and review each independently. The first reduces full-image copies and CPU preprocessing; the second removes full-resolution mask processing from CPU. This targets existing image-processing work without changing provider integration.

A full GPU-resident pipeline would additionally need compatible graphics/DirectML resources and synchronization. Generic ONNX Runtime I/O binding does not establish compatibility with the pinned Windows ML provider route. It is a separate future design, with no D3D12 interop, new allocator, dependency upgrade or loader hook added here.

Production eligibility is Windows, exact MediaPipe model and a completed effective DirectML session. CPU, other models, MIGraphX and Linux/macOS keep the existing path. No model expansion, provider acquisition or enhancement-filter changes. These two stages do not complete the remaining roadmap UI/release work.

## Stage 1: smaller image before readback

Keep the original-resolution OBS texture for final compositing and existing background blur. Render a separate inference texture at the actual validated MediaPipe input size, currently 256×144, using GPU sampling before `gs_stage_texture` and `gs_stagesurface_map`. The small staging surface supplies owned BGRA pixels to the existing CPU input preparation and Windows ML session.

Do not repurpose the shared full-resolution texture used by compositing/blur. Track source dimensions and inference dimensions separately: a 720p source remains 720p on screen. Color/channel handling, alpha handling, interpolation coordinates and quantization must be checked against the current CPU preparation. CPU color conversion and float/tensor packing operate on the reduced image; this is not a promise of wholly GPU preprocessing.

The reduced route must not silently reinterpret the image-similarity threshold. Preserve current full-image PSNR skip decisions, initially retaining that compatibility readback only when Image Similarity is enabled. Show that qualification in the optimization tooltip/status and diagnostic summary. The controlled performance comparison disables Image Similarity in both modes so the small-readback benefit can be measured; the owner's saved setting is never silently changed. GPU similarity/reduction is outside this first scope.

Frame skipping retains its settings and visible behavior. Publish copied input with a frame identifier, source/input dimensions and configuration generation; never pair pixels with metadata from another source size, model or processing mode.

## Stage 2: mask operations in shaders

CPU receives the small MediaPipe probability output as today. Preserve initial inversion/threshold, temporal smoothing and contour-area filtering on the small mask, including their current ordering and quantization. Keeping this small CPU block avoids an extra mask readback merely to run contour extraction.

Upload the small mask into a persistent texture. Move the subsequent edge smoothing, resize to source dimensions, post-smoothing threshold, erosion/dilation and feather to GPU effect passes. Preserve existing parameter units, border behavior and conditions: operations currently conditional on thresholding remain conditional; expansion still refers to source pixels. A different blur algorithm must not replace stack-blur semantics merely because an existing background-blur shader is available.

The final shader mask feeds existing alpha compositing and background blur. Reuse allocated textures between frames and recreate only when dimensions/format change. Keep previous accepted output during intentionally skipped frames. This stage must not enlarge the image sent to CPU or upload a newly generated full-resolution CPU mask each render.

## Controls, status and failure handling

Add one saved Windows Background Removal checkbox, **Processamento de imagem na GPU**, visible near the current processor choice. Default off preserves existing scenes. It is actionable only for eligible MediaPipe/effective DirectML sessions; switching to CPU disables the optimized route while retaining the user's saved preference for a future eligible session.

On/off controls both delivered stages without changing the inference provider. During stage-by-stage development, status/diagnostics identify which stage is actually active. Display processing state separately from the existing effective inference-provider status: requested, active, unavailable or compatibility fallback. A checked box alone never claims processing is active. Image Similarity compatibility readback is disclosed when applicable.

If GPU effect/resource preparation fails, retain the current DirectML inference session and use the existing CPU image/mask processing, with a visible reason and one diagnostic per transition. Do not relabel that as CPU inference. Per-frame failure retains the last valid mask and transitions processing to a controlled fallback; avoid repeated allocation attempts/log spam until explicit reinitialization. Failure of the underlying session continues to follow the existing session contract.

## Lifetime and threading

GPU resource operations run in the OBS graphics context. Inference stays serialized by the existing model mutex; the same DirectML session never receives concurrent runs. GPU passes must not hold that mutex while waiting for inference or readback.

Use a published immutable configuration snapshot/generation for render and inference. A provider/model switch, checkbox change or source resize invalidates incompatible input, masks, temporal history and resource dimensions. Discard late outputs from old generations. Preserve intentional frame skipping within a generation, and initialize a valid first-frame state without showing an old source's mask.

Own CPU pixel copies beyond staging unmap and own all mask data passed across threads. Destroy resources in the graphics context, and release sessions before their environment. Removal/recreation, failed initialization, toggling while frames run and OBS shutdown must remain safe. Graphics/inference locks require an explicit non-cyclic order in the implementation plan.

## Quality and correctness checks

Compare the new preparation and mask stages against the current path using identical input frames, dimensions, settings and model. Cover odd source dimensions, 720p/1080p, channel/alpha patterns, empty/full masks, small islands, thin edges, motion and every relevant mask control. Include threshold disabled, zero/max smoothing/feather, positive/negative expansion, similarity enabled and skipped frames.

GPU preprocessing normalized mean absolute error against the legacy prepared input must be at most 0.01. Final normalized mask MAE must be at most 0.01 and binary foreground IoU at least 0.98, with matching dimensions, bounded finite values and explicit handling of empty/full synthetic cases. Validate both individual stages and the combined path; investigate failures instead of silently weakening these gates. Numeric agreement does not replace a visual check for halos, lost hair/edges, flicker or motion lag.

Portable tests cover eligibility, state/generation transitions and controlled failures. Native Windows tests compile and execute the actual effects and resource lifecycle; software graphics results prove correctness only. Exact-head Windows plugin build/package checks must pass, including packaged effect files and the existing one-ORT/pinned-origin contract. Actual AMD execution and performance require the owner's RX 9070 XT.

## Performance measurement and owner delivery

Measure complete processing alongside component counters: source/inference dimensions, readback pixel count, frames processed/skipped, active stages, mode transitions and fallback. Host elapsed time for submission/wait is labelled as such; it must not be called GPU execution time without actual GPU timestamp measurement. Aggregate diagnostics rather than logging each frame.

One PowerShell 5.1-compatible script validates the installed artifact/receipt and collects four 20-second blocks in OFF → ON → ON → OFF order. Each block has five seconds of settling and fifteen seconds of measurement. DirectML, camera resolution/FPS, scene, lighting and mask settings stay the same; Image Similarity is off in both modes. The script prompts for the checkbox changes and verifies the logged effective processing/provider state; it does not pretend to control OBS automatically.

Collect the same OBS process's CPU time deltas, normalized as `100 * delta_processor_seconds / (elapsed_seconds * logical_processor_count)`. Record processor count, process identity, settings and elapsed intervals; a restarted process or unexpected state/fallback invalidates that block. Report CPU percentage points and variability between blocks, not a predicted saving or inference-speedup ratio. Keep camera movement/background workload comparable and record the limitation that this is a live sequential comparison. If the owner normally uses Image Similarity, offer the same comparison with that setting preserved and report it separately; an improvement measured only with similarity disabled is not evidence of the same gain in their usual configuration.

Acceptance requires quality/lifecycle passes and a repeatable improvement in whole-OBS CPU use under that controlled comparison, without material worsening of rendering lag, dropped frames or mask response. Report the actual size of the gain; if it is within variation, performance remains inconclusive rather than accepted as a substantial saving. No target percentage is promised before measurement.

Deliver a reviewed exact-SHA plugin ZIP, a simple installer/restore path and the single comparison script. The owner checks checkbox on/off, CPU/DirectML switching, mask quality, source resize and filter removal/recreation. Complete logs remain local; only the concise sanitized summary is shared. Preserve the previous accepted build/receipt as the restoration reference.

## Evidence, review and next gate

Current source observations come from `src/obs-utils/obs-utils.cpp`, `src/background-filter.cpp`, `src/ort-utils/ort-session-utils.cpp` and existing effect files at the accepted baseline. Current documentation was queried through Context7; [OBS graphics](https://docs.obsproject.com/graphics) supports shader effects/context ownership, while [ORT I/O binding](https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html) explains device-buffer transfers generally. Implementation must verify APIs against the project's pinned headers; examples from newer upstream documentation are not a reason to change the provider route or dependencies.

The implementation plan must specify stage boundaries, lock order, effect reference tests, controlled failures, exact-head native/package checks and the owner comparison. Use task-scoped implementation and independent reviews, then a whole-change review. Source acceptance and measured CPU reduction are separate outcomes.

Owner review of this written specification permits preparing the implementation plan. Review/approval of that written plan and its execution method precede product code. All commits use the owner's signature and DCO; `GPU`, `main` and accepted feature heads are preserved. No merge or release is authorized by this scope.

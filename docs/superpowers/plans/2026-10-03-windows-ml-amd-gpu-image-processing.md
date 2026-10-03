<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# GPU Image Processing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce CPU work around MediaPipe DirectML inference through GPU input downscaling and mask processing, with measured whole-OBS results and preserved mask quality.

**Architecture:** Preserve the full source texture; read back a separate model-sized texture. Keep threshold/inversion, temporal smoothing and contours on the small CPU mask, then run the remaining mask operations as GPU effect passes. Publish immutable configuration/frame generations and expose one saved processing checkbox independent of inference-provider status.

**Tech Stack:** C++20, existing libobs graphics/effects, existing OpenCV, Windows ML 2.2.12, its embedded ONNX Runtime, CMake/CTest, Python unittest, Windows PowerShell 5.1-compatible owner scripts.

**Spec:** [Approved GPU image-processing specification](../specs/2026-10-03-windows-ml-amd-gpu-image-processing-design.md).

**Status:** Approved by the owner on 2026-10-03 (“Aprovo, podes avançar”). Implementation may proceed with the approved task-scoped implementer/independent-review method; hardware acceptance and measured CPU reduction remain separate gates.

## Global Constraints

- Source baseline `a390fcb4f2f6cdd9beba14e3024edc41b7860cc0`; approved spec commit `39bafbdb5b7e2244fb999d8b011765f8aa7b8da2`; work only on `feature/windows-ml-amd-gpu-image-processing` in its existing isolated worktree.
- Preserve `GPU`, `main`, accepted Sprint 5/Sprint 6 heads, owner files and the installed accepted build/receipt. Do not merge or release.
- Optimize only Windows Background Removal + exact MediaPipe + completed effective DirectML. Default checkbox off; CPU, other models, MIGraphX, enhancement and Linux/macOS retain their current processing/runtime behavior.
- Keep Windows ML **2.2.12**, one package-origin ONNX Runtime and existing provider/fallback contracts. No acquisition in OBS, dependency upgrade, D3D12 interoperability, direct GPU tensor binding or loader/search-path change.
- GPU input is actual validated model dimensions, currently **256×144**. Output remains source resolution. Similarity enabled retains full-resolution CPU PSNR/readback; never change its saved setting or interpret its threshold at another resolution.
- Input normalized MAE **≤0.01**; final-mask normalized MAE **≤0.01**, binary foreground IoU **≥0.98**, finite/bounded values and correct dimensions. Do not weaken failed gates without new direction.
- One owner comparison script: **OFF → ON → ON → OFF**, four **20-second** blocks, **5-second** settling + **15-second** measurement, same DirectML provider and settings. CPU percentage is `100 * delta_processor_seconds / (elapsed_seconds * logical_processor_count)`.
- Component timers measure host elapsed time, including waits; do not call them GPU execution time. Report whole-OBS percentage points/variation; no minimum CPU saving is promised.
- Fresh task-scoped implementer, fresh independent review per task, then whole-change review. TDD for changed behavior. Scratch under ignored `.superpowers/`; owner-signed DCO commits with key `460B18400D17462FF3714A2BDCED30418A1C06FC`, no co-author attribution.
- Native Windows is the verification environment for graphics. When unavailable locally, reviewed signed test-only checkpoints may be pushed on this new branch to obtain expected RED evidence before implementation; these are incomplete checkpoints, not accepted deliverables. Each task still requires its actual native PASS before completion.

## Review Focus

1. Source dimensions change during inference: reject old-generation output rather than stretching an old mask over the new source. Task 1/4 tests pin this.
2. Checked optimization with an effective CPU fallback: processor remains truthful, optimized processing stays inactive and the saved request survives. Task 1/4 tests pin this.
3. Borders, thin islands and threshold-disabled masks: GPU processing preserves current conditional operations, parameter units and OpenCV reference behavior. Task 3/5 tests pin this.
4. Similarity/intentional frame skipping versus late output: reuse valid current-generation masks only, preserve full-image PSNR decisions and disclose compatibility readback. Task 2/4 tests pin this.
5. Restarted OBS, mixed settings or failed processing during a timed block: invalidate the comparison rather than reporting a CPU saving. Task 6 tests pin this.

## File map and contracts

Create `src/obs-utils/gpu-image-policy.hpp/.cpp` for OBS/OpenCV-free eligibility, stamps and processing decisions. Create `gpu-image-pipeline.hpp/.cpp` beside it for owned frame/mask packets, configuration snapshots and synchronization. Create `gpu-input-preprocessor.hpp/.cpp` and `gpu-mask-processor.hpp/.cpp` for graphics resources. Create `background-mask-cpu.hpp/.cpp` for the retained CPU mask block and unchanged legacy postprocessing used as the reference.

Create `data/effects/input_downscale.effect` and `gpu_mask_processing.effect`. Modify `src/background-filter.cpp` only at configuration/properties, render/tick publication, fallback and cleanup boundaries. The shared legacy `src/obs-utils/obs-utils.cpp` and enhancement path remain unchanged. Add sources/effects only under Windows in root `CMakeLists.txt`, with native tests under `tests/GpuImageProcessing/`; a standalone policy-only CMake mode permits host tests without OBS/OpenCV/Windows ML.

All new contracts below live in namespace `gpu_image`:

- `Dimensions { uint32_t width, height; }`; `FrameStamp { uint64_t generation, frame_id; Dimensions source, input; }`.
- `MaskSettings { bool enable_threshold; float threshold, temporal_smooth_factor, contour_filter, smooth_contour, feather; int mask_expansion; }` copies current meanings without new units.
- `PipelineConfig { uint64_t generation; Dimensions source, input; bool requested, windows, mediapipe, session_ready, effective_directml, image_similarity; uint32_t mask_every_x_frames; double similarity_threshold; MaskSettings mask; }`.
- `ProcessingDecision { bool eligible, small_readback, full_similarity_readback; }`; `ProcessingState { Off, Pending, PreprocessOnly, Active, Unavailable, CpuProcessingFallback }`.
- `FramePacket { FrameStamp stamp; cv::Mat input_bgra, similarity_bgra; }` and `MaskPacket { FrameStamp stamp; cv::Mat mask; bool gpu_postprocess; }` own their data; publishing never retains a staging pointer or mutable caller buffer. `MaskPacket.gpu_postprocess=true` carries an input-sized `CV_8UC1` prepared mask for shader Stage2; `false` carries the finished legacy `CV_8UC1` mask at source dimensions only when threshold is enabled, otherwise at input dimensions.
- `ProcessingSnapshot { bool requested; ProcessingState state; uint64_t generation; Dimensions source, input; bool preprocess_active, mask_active, similarity_full_readback; std::string reason; }`. Session diagnostics remain the existing independent inference truth.

### Task 1: processing policy, generations and packet publication

**Files:** Create policy/pipeline files above; create `tests/GpuImageProcessing/CMakeLists.txt`, `policy-test.cpp`, `pipeline-test.cpp`. Add Windows targets to root/test CMake as needed, with policy-only standalone support. Create `.github/workflows/gpu-image-processing-check.yml`; modify `.github/workflows/check.yml` and `build-windows.yml` for this task's native gate.

**Interfaces:** `evaluate_processing_request(const PipelineConfig&) -> ProcessingDecision`; `accept_frame(const FrameStamp&, const PipelineConfig&) -> bool`. `ImagePipeline::configure(PipelineConfig) -> uint64_t`, `snapshot() -> PipelineConfig`, `publish_frame(FramePacket) -> bool`, `latest_frame() -> std::optional<FramePacket>`, `publish_mask(MaskPacket) -> bool`, `latest_mask() -> std::optional<MaskPacket>`, `set_processing_state(uint64_t, ProcessingState, std::string) -> bool`, `processing_snapshot() -> ProcessingSnapshot`, `invalidate() -> void`.

- [ ] Write `default_off_and_eligibility`, `cpu_fallback_is_not_gpu_processing`, `stale_generation_or_dimensions_rejected`, `similarity_requires_full_readback`, `late_status_cannot_overwrite_new_generation` and `packets_own_their_pixels`. Assert, for example, `evaluate_processing_request(cpu_config).eligible == false`, and an old stamp fails after `configure` changes source from `{1280,720}` to `{1920,1080}`.
- [ ] Run `cmake -S tests/GpuImageProcessing -B .superpowers/build/gpu-policy -DGPU_IMAGE_PROCESSING_POLICY_ONLY=ON`, then build/CTest there with `--no-tests=error`; confirm RED for the missing policy. Native `pipeline-test` must also fail before packet ownership/publication is implemented.
- [ ] Implement the policy and generation-controlled mailbox; advance generation for relevant setting/model/provider/source changes and fallback-route transitions, never for an unchanged configure call. Clear incompatible packets/history; saved requested preference is not effective state. Readers receive immutable owned snapshots.
- [ ] Add a Windows-only workflow triggered by pushes to `feature/windows-ml-amd-gpu-image-processing`, calling the existing reusable build with `upload-artifacts: true`, `attest-artifacts: false`, `runs-on: windows-2025-vs2026`, and read-only contents permission. Retain its immutable `github.sha` checkout/receipt. Extend Check CI's push branch list for formatting/REUSE. Run native policy/packet CTest before packaging; later tasks extend this gate. This needs neither a new PR nor a dispatcher on `main`.
- [ ] Run policy CTest plus native `ctest --test-dir build -C RelWithDebInfo -R '^gpu-image-(policy|pipeline)$' --output-on-failure --no-tests=error`; require exit 0 and all selected tests passing. Verify failed/stale publications cannot alter current state and invalidation is terminal for that generation.
- [ ] Independently review this policy boundary; commit only its files and necessary CMake wiring, signed/DCO, subject `Add GPU image processing policy and frame generations`.

### Task 2: GPU input downscale with the full-resolution source preserved

**Files:** Create `gpu-input-preprocessor.hpp/.cpp`, `data/effects/input_downscale.effect`, `tests/GpuImageProcessing/input-test.cpp`, `native-main.cpp`, `stage-graphics-runtime.ps1`; modify Windows-only source/effect registration, native-test CMake and `.github/workflows/build-windows.yml`. Consume Task 1 types; do not change enhancement/shared legacy capture.

**Interfaces:** `GpuInputPreprocessor::prepare(const PipelineConfig&, const char* effect_path) -> bool`, `capture(gs_texture_t* full_source, FrameStamp, bool copy_similarity) -> std::optional<FramePacket>`, `release() -> void`, `failure_reason() -> std::string`. Resource operations run only inside the graphics context; `capture` owns pixels before staging unmap and returns failure without publishing Ready. Introduce executable/CTest `gpu-image-processing-native --effect-root <path> --model <mediapipe-path>`; Task 3 adds mask cases and Task 5 adds combined quality cases.

- [ ] Write `reduced_readback_preserves_full_texture`, `odd_dimensions_and_bgra_alpha_match_cpu_reference`, `similarity_uses_full_image`, `resource_failure_is_controlled`, `resize_and_repeated_release`. Use 1280×720, 1920×1080 and 641×359 deterministic inputs; assert reduced pixels are `256*144`, compatibility pixels are source-sized only when enabled, original texture dimensions unchanged and prepared-input MAE ≤0.01.

```cpp
CHECK(packet.input_bgra.cols == 256);
CHECK(packet.input_bgra.rows == 144);
CHECK(prepared_input_normalized_mae <= 0.01);
CHECK(packet.similarity_bgra.empty() == !copy_similarity);
```

- [ ] Stage the actual pinned libobs graphics module and proven dependency imports beside this native fixture, outside the plugin ZIP, using `stage-graphics-runtime.ps1` with explicit source/destination roots and origin checks. Create a real graphics context, emit case/progress markers, set CTest timeout 300 seconds and run it in the Task 1 Windows workflow before packaging. Graphics deployment failure is an unmet gate, not a passing skip; capture it without loader/PATH workarounds. Run the input cases to obtain RED before implementing the effect.
- [ ] Implement separate model-sized texrender/staging resources and effect sampling. Preserve legacy BGRA/alpha/color interpretation and CPU RGB/float packing on the reduced input. Obtain full-image PSNR pixels from the existing full texture only when requested; never calculate similarity on the smaller packet.
- [ ] Run `ctest --test-dir build -C RelWithDebInfo -R '^gpu-image-processing-native$' --output-on-failure --no-tests=error --verbose --interactive-debug-mode 0`; require exit 0, actual shader/readback reference and allocation/map-failure cases passing. Verify no retained mapped pointers, no per-frame reallocation at fixed dimensions and no mutation of the source texture used for blur/compositing.
- [ ] Independently review Stage 1; signed/DCO commit `Downscale MediaPipe input on GPU before readback`. Integrate it visibly as `PreprocessOnly` in Task 4; do not claim Stage 2 before its resources are active.

### Task 3: GPU mask processing with an unchanged CPU reference

**Files:** Create CPU mask helper, GPU mask processor and `data/effects/gpu_mask_processing.effect`; create `tests/GpuImageProcessing/mask-test.cpp` and `mask-reference-test.cpp`; register Windows sources/effects. Current statements in `background-filter.cpp:541-716` are the behavior to preserve; Task 4 connects the helpers to callbacks.

**Interfaces:** `prepare_small_mask(const cv::Mat& probability_u8, const cv::Mat& previous, const MaskSettings&) -> SmallMaskPreparation { cv::Mat mask; cv::Mat temporal_history; } (independently owned; history precedes contours)`; `finish_mask_cpu(const cv::Mat& small_mask, Dimensions source, const MaskSettings&) -> cv::Mat`. `GpuMaskProcessor::prepare(Dimensions input, Dimensions source, const char* effect_path) -> bool`, `process(const MaskPacket&, const MaskSettings&) -> gs_texture_t*`, `release() -> void`, `failure_reason() -> std::string`; returned texture is borrowed until the next process/prepare/release, only within graphics ownership.

- [ ] Write reference tests for initial invert/threshold, temporal rounding/history and small contour-area removal. Write GPU tests `thin_edges_and_border_impulses`, `zero_max_smoothing_feather`, `expansion_both_signs`, `threshold_disabled_preserves_conditions`, `same_size_reuses_resources`. Compare final normalized MAE ≤0.01 and IoU ≥0.98; both empty binary sets have IoU 1, exactly one empty set has IoU 0. Test known empty/full masks explicitly rather than passing degenerate output through ordinary scene tests.

```cpp
CHECK(final_mask.size() == reference_mask.size());
CHECK(final_mask_normalized_mae <= 0.01);
CHECK(foreground_iou >= 0.98);
```

- [ ] Obtain RED for missing helpers/effects. Pin the old CPU behavior against fixed expected synthetic outputs before comparing it to GPU; changing both reference and candidate together cannot establish correctness.
- [ ] Extract the CPU block without changing ordering/parameters. Implement small-mask upload and effect passes in order: stack-blur edge smoothing at model dimensions; source-size linear resize; `>128` threshold when smoothing requires it; current-sign erosion/dilation in source pixels; feather's dilation and normalized box filter. Keep all existing threshold-dependent conditions. Preserve OpenCV border/rounding semantics using pinned-source/native-reference evidence; no substitute Kawase/Gaussian blur. Use persistent ping-pong textures and release partial resources on failure.
- [ ] Run `ctest --test-dir build -C RelWithDebInfo -R '^(gpu-image-mask-reference|gpu-image-processing-native)$' --output-on-failure --no-tests=error --verbose`; require exit 0 and CPU reference/real effect cases passing. Compare temporal and quantization boundaries, including values around both initial threshold and 128; verify rendering can consume a borrowed output only during its valid lifetime.
- [ ] Independently review Stage 2; signed/DCO commit `Process MediaPipe mask borders and resizing on GPU`.

### Task 4: filter wiring, truthful controls, lock order and telemetry

**Files:** Modify `src/background-filter.cpp`, new pipeline files, `data/locale/en-US.ini`, `data/locale/pt-PT.ini`; create `tests/GpuImageProcessing/filter-pipeline-test.cpp`, `status-test.cpp`, `src/obs-utils/gpu-image-status.hpp`. Include a filter-lifecycle native fixture without mocking away graphics/session execution; boundary log/module-path helpers may remain test-only.

**Interfaces:** Inline `processing_status_text_key(const ProcessingSnapshot&) -> std::string_view` in the new status header; saved key `gpu_image_processing`, default `false`. Log keys: `filter_id`, `settings_fingerprint`, `generation`, `frame_id`, `requested`, `state`, `effective_inference`, `preprocess_active`, `mask_active`, `similarity_full_readback`, `source_width/height`, `input_width/height`, `reason`, processed/skipped/stale counts, readback-pixel and host-elapsed aggregates. Assign a stable filter-lifetime identifier; fingerprint all comparison-relevant filter settings except the optimization checkbox, and log those values at configuration transitions. Emit `GPUImageProcessing state ...` on transitions and `GPUImageProcessing stats ...` every five seconds; include `processing_version=1` and a monotonic configuration generation for the collector.

- [ ] Write RED tests for saved checkbox/default/locales, pending-versus-active status, effective CPU fallback, switching CPU→DirectML→CPU, checkbox toggling without recreating a valid DirectML session, resize while an inference is blocked, similarity skip/no history advance, mask-every-X reuse, failed GPU preparation/map, removal/recreation and a late callback after invalidation. Assert `processing_snapshot().mask_active == false` after CPU fallback and `publish_mask(old_generation_packet) == false` after source resize.
- [ ] Implement callbacks using immutable settings/stamps. Render the full source first, then eligible Stage 1 capture; tick uses full similarity packet for the unchanged PSNR decision and the reduced input for inference. Publish the small prepared mask for Stage 2; legacy mode uses the reference CPU finish. Render uploads/processes the current mask only when its generation/source dimensions match, otherwise use a safe initialized state.
- [ ] Apply this lock graph: graphics context → brief pipeline-state mutex; model mutex → brief pipeline-state mutex. No path holds pipeline-state while acquiring graphics/model; graphics callbacks never acquire model mutex; tick never enters graphics. Copy/release state before expensive calls, check generation under the allowed model→state order before and after inference, and refresh OBS properties after all locks/context are released. Update publishes model/provider/input-size truth after session initialization, then prepares graphics outside model/state locks. Source resize advances generation in render without consulting live model objects.
- [ ] Ensure only initialization/provider changes recreate the session. GPU processing failure advances/invalidate incompatible input and selects legacy CPU image/mask work while retaining DirectML inference; show a reason once and retry only on explicit reinitialization. Retain an immutable last accepted same-source display mask explicitly during recovery, without accepting old-generation worker output; clear it on source/model/provider changes or removal. Removal marks invalid/disabled before graphics release; callbacks recheck state in graphics context and own a shared lifetime reference.
- [ ] Run `ctest --test-dir build -C RelWithDebInfo -R '^gpu-image-' --output-on-failure --no-tests=error --verbose`; require exit 0, at least 64 mixed toggle/resize/remove/recreate boundaries, and no stale masks, concurrent DirectML `Run`, deadlock or misleading Ready. Assert full-image compatibility status/tooltip without rewriting saved similarity settings. Review and signed/DCO commit `Expose GPU image processing controls and integrate safe filter switching`.

### Task 5: native graphics quality gate, CI and exact packages

**Files:** Finish `tests/GpuImageProcessing/CMakeLists.txt`, extend `native-main.cpp`/`stage-graphics-runtime.ps1`, create `quality-test.cpp`; modify `tests/CMakeLists.txt`, `.github/workflows/build-windows.yml`, `scripts/verify_windows_ml_package.py`, `tests/WindowsMlBuild/test_windows_ml_package.py`; keep the existing adapter tests/staging assertions.

**Interfaces:** Extend native executable/CTest `gpu-image-processing-native`, arguments `--effect-root <path> --model <mediapipe-path>`; it runs actual libobs graphics effects plus the existing model/session adapter. CI uses CPU inference as a common reference to isolate shader/preparation differences, not as production eligibility or an AMD GPU claim. The fixture calls the same GPU modules directly; no production policy bypass is added. Keep the 300-second timeout and explicit graphics/main/case markers. Package verifier requires `--effects-source-root <data/effects>` for this contract and byte-checks both new required packaged effects against that source directory; update every caller/fixture, never silently omit effect checks.

- [ ] Write RED missing/empty/changed shader install-tree/ZIP tests, source-registration tests, runtime-staging tests and an actual graphics-context smoke case. Add full-model deterministic quality comparisons for Stage 1 alone, Stage 2 alone and combined, using the same captured fixture frames/settings/model and spec thresholds; no tautological shader-string-only quality tests.
- [ ] Build native fixtures using pinned libobs `0052d024fd6a5ff1aa04c76cbdffd3085a5dfacc`, existing OpenCV and Windows ML. Stage only proven graphics-module/OBS-dependency imports beside tests with pin/origin checks; never ship test DLLs in the plugin. If native graphics deployment fails, capture import/context failure before a separately scoped repair; no loader/PATH workaround or weakened test. Use software graphics when supported and label it accurately; unavailable graphics is an unmet gate, not a passing skip.
- [ ] Add the required executable/effects and fail-stop execution to Windows CI before packaging. Run `ctest --test-dir build -C RelWithDebInfo -R '^(gpu-image-processing-native|windows-ml-plugin-session)$' --output-on-failure --no-tests=error --verbose --interactive-debug-mode 0`; run `python -m unittest tests/WindowsMlBuild/test_windows_ml_package.py -v`. Preserve existing package legal/origin/one-ORT and platform configuration tests, `/WX`, locale installation and approved safe ZIP checks.
- [ ] Independently review the whole source change, fix/re-review affected findings, then push the signed exact source within existing authorization. Verify exact-head Check CI plus GPU Image Processing Check/Windows build, and downloaded ZIP/source/DLL/model/new-effect hashes and package origins. Document native shader quality versus software/AMD hardware limits; do not repeat the unchanged CPU/DirectML/MIGraphX session benchmark as a new requirement.
- [ ] Signed/DCO commit `Validate GPU image processing effects and Windows packaging`; save immutable source/run/artifact receipts in ignored execution evidence. A new PR is prepared only if separately authorized; keep accepted PR #2/source intact.

### Task 6: one owner script, visual acceptance and measured CPU result

**Files:** Create `scripts/testar-processamento-gpu.ps1`, `tests/GpuImageProcessing/measurement-test.ps1`, `docs/windows-ml-gpu-image-processing.md`. Adapt the previously reviewed simple installer/restore script in ignored `.superpowers/` to the actual new artifact/receipt; no guessed hashes/download links and no long pasted setup guide.

**Interfaces:** Owner entry point `powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\testar-processamento-gpu.ps1"`; optional `-ManterSimilaridade` preserves/reports the owner's normal similarity mode separately. Pure PowerShell helpers `Get-NormalizedCpuPercentage([double]CpuDeltaSeconds,[double]ElapsedSeconds,[int]LogicalProcessors) -> [double]`, `Test-GpuProcessingBlock([pscustomobject]Block) -> [bool]`, `Compare-GpuProcessingBlocks([object[]]Blocks) -> [pscustomobject]`. Tests load these function definitions from the production script AST without executing interactive orchestration or requiring Pester/new dependencies.

- [ ] Write RED tests: `CpuDelta1Second_Elapsed10_Logical16` asserts **0.625%**; negative deltas/zero denominator fail; exactly 15 measured seconds exclude settling; restarted PID/process start time, changed filter identity/source/settings/generation, late log lines, fallback or wrong provider invalidate a block; whitespace/incomplete logs never fabricate readiness. Multiple active filters require choosing one identified filter or an inconclusive result, never interleave their records. Overlapping ON/OFF block ranges report inconclusive; min OFF greater than max ON reports observed improvement without statistical-confidence claims. Record differences in percentage points separately from relative percent.
- [ ] Implement one self-contained PS5.1-compatible script. Verify exact installed receipt/DLL/source first; prompts guide the checkbox and shared settings, never pretend to automate OBS. Record current settings and request similarity off equally for the default comparison; `-ManterSimilaridade` validates its preserved value. Sample the same process once per second using cumulative process CPU and elapsed monotonic time; four blocks exclude their first five seconds. Require fresh effective DirectML, actual stage flags, expected readback sizes, stable configuration/generation and no fallback/errors throughout measured intervals.
- [ ] Aggregate OFF/ON means, block ranges, process identity, dimensions/FPS/settings, version/driver facts and processing telemetry. Preserve raw logs locally and print only sanitized summary. Compare rendering-lag/dropped-frame deltas from OBS counters/fresh log statistics; if those counters are unavailable, mark that evidence incomplete instead of deriving them from inference timings. Ask the owner to confirm visual quality, response, switching, resize and remove/recreate, using concise prompts.
- [ ] Run `powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/GpuImageProcessing/measurement-test.ps1` in native CI; require exit 0. Independently review final script/installer with real downloaded ZIP filesystem/fault fixtures and synthetic collector timing/status/restart cases before owner delivery. Restoration keeps the actual accepted prior build/receipt outside OBS search paths; never overwrite it with a partial installation.
- [ ] Signed/DCO commit the reviewed script/tests as `Add single-script OBS GPU processing comparison`, push and require the final exact-head Windows/Check CI and package gates. Adapt installer hashes/receipts to those actual artifacts, run its filesystem/fault fixtures and complete the final whole-change/script review before delivery. Leave the acceptance document pending until real owner results exist.
- [ ] Deliver one reviewed new ZIP, simple install command and the single comparison command. Await owner summary, investigate only failed/new gates, and record functional/quality acceptance separately from measured performance. Similarity-enabled gains must be reported separately; no substantial saving is claimed if within observed variability or accompanied by worse render/mask behavior.
- [ ] After owner results, independently review the sanitized documentation and signed/DCO commit `Record GPU image processing acceptance and CPU measurements`. Publish docs-only acceptance on `GPU` without copying feature code; verify remote bytes/refs. Keep the feature, raw evidence, backups and earlier accepted heads intact; no merge/release.

## Verification and execution handoff

Each task's implementer gets this plan, the approved spec, exact baseline and its interface/files/tests; its reviewer checks the immutable task diff and actual evidence before the next task. Preserve RED evidence and verification boundaries; local policy tests do not prove shader execution, generic Windows CI does not prove AMD hardware performance, and an active GPU label does not prove a whole-OBS CPU saving.

Formatting/REUSE/whitespace checks cover each change. Existing native/session/package gates remain; broaden testing only for new regressions or touched contracts. Native graphics reference and final whole-change review precede artifact delivery. Signed identity, fingerprint, DCO, clean scoped worktree and protected refs are verified before every push.

The owner approved this written plan on 2026-10-03 (“Aprovo, podes avançar”). Use `superpowers:subagent-driven-development` and start Task 1 in the existing worktree, preserving task-scoped implementers and independent reviews followed by whole-change review. Product code is authorized within this scope; owner hardware results, merge and release are separate gates.

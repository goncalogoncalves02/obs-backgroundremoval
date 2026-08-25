# OBS Background Removal — AMD / Windows ML Implementation Plan

> **For agentic workers:** Execute this plan task-by-task. Use a fresh review checkpoint after every milestone. Do not combine milestones unless the previous milestone has passed all acceptance criteria.

**Goal:** Create a maintainable Windows fork of `royshil/obs-backgroundremoval` based on tag `1.4.1` that can run background-removal inference on an AMD Radeon GPU on Windows, prioritising Windows ML + MIGraphX, with DirectML as a controlled fallback and CPU as the guaranteed safe fallback.

**Architecture:** Preserve the plugin's existing `Ort::Session`-based model and inference pipeline. Add a small Windows-only provider-discovery/registration layer around ONNX Runtime rather than rewriting model preprocessing, postprocessing, or the OBS rendering path. Prove hardware acceleration in an isolated smoke-test executable before integrating it into the OBS plugin.

**Tech Stack:** C++20, CMake, OBS Studio plugin API, ONNX Runtime, Windows ML, Windows App SDK / `Microsoft.Windows.AI.MachineLearning`, MIGraphX Execution Provider, DirectML, OpenCV, PowerShell, GitHub Actions.

**Baseline repository:** `https://github.com/royshil/obs-backgroundremoval/tree/1.4.1`

**Primary target hardware:** AMD Radeon RX 9070 XT on Windows 11.

**Plan location when copied into the fork:** `docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md`

---

## 1. Executive summary

The existing `obs-backgroundremoval` 1.4.1 architecture is already based on ONNX Runtime and creates a normal `Ort::Session`. The problem on Windows is not the segmentation models themselves; it is that the official Windows build does not provide a usable AMD GPU Execution Provider.

The most important implementation principle is therefore:

> **Do not rewrite background removal. Replace or extend the Windows ONNX Runtime/provider layer while keeping the existing model classes and inference flow intact.**

The recommended provider priority for this fork is:

1. **Windows ML + MIGraphX** — first choice for AMD.
2. **Windows ML + DirectML** — fallback/compatibility path.
3. **CPU** — always available and must remain functional.

As of August 2026, Windows ML exposes `MIGraphXExecutionProvider` for AMD and also includes a legacy DirectML provider. Windows ML can provide ONNX Runtime itself, and its C++ surface remains compatible with normal ONNX Runtime concepts such as `Ort::Env`, `Ort::SessionOptions`, and `Ort::Session`.

A particularly useful compatibility path is to start the proof-of-concept with the stable Windows ML 1.8.x line because it uses ONNX Runtime 1.23.x, while `obs-backgroundremoval` 1.4.1 currently pins ONNX Runtime `v1.23.2`. This minimises the number of variables changed during the first experiment.

Do **not** begin by modifying the OBS filter UI. First prove that the exact MediaPipe `.ort` model supplied by the plugin can execute through Windows ML on the RX 9070 XT.

---

## 2. Definition of success

The project is considered successful when all of the following are true:

- The unmodified 1.4.1 fork builds and loads in OBS on Windows.
- The existing CPU mode still behaves identically.
- A standalone C++ smoke test can load one of the plugin's existing `.ort` models using Windows ML.
- The smoke test can discover an AMD-compatible Windows ML Execution Provider.
- The smoke test can create an `Ort::Session` with the selected GPU provider.
- The smoke test can run repeated MediaPipe inference without crashes.
- The plugin can expose a Windows GPU inference option in OBS.
- When that option is selected, logs explicitly identify the provider and device being used.
- GPU inference falls back safely to CPU if the provider cannot be registered or session creation fails.
- Linux and macOS behaviour remains unchanged.
- A release build can be installed on a clean Windows machine without requiring the developer environment.
- A 30-minute OBS test with the filter active does not leak memory, crash, freeze the mask, or cause repeated provider downloads.
- GPU mode demonstrates a meaningful performance benefit on the RX 9070 XT relative to CPU mode.

### Performance success criteria

Use the same OBS scene, webcam, resolution, FPS, segmentation model, and filter settings for CPU and GPU measurements.

Record:

- OBS total CPU usage.
- OBS render time / missed frames.
- Average inference time.
- p95 inference time.
- GPU utilisation on an appropriate Compute/AI engine.
- Working set memory.
- GPU memory usage.

The first usable release should satisfy:

- No regression in visual quality that is obvious in normal motion.
- No persistent OBS rendering lag caused by the provider.
- GPU mode has lower average inference latency than CPU mode on the RX 9070 XT.
- GPU mode reduces CPU load during active inference.
- No crash or frozen mask during a 30-minute continuous test.

Do not optimise before these correctness requirements pass.

---

## 3. Important facts about the upstream code

### Existing ONNX Runtime version

`buildspec.props` in tag `1.4.1` pins:

```text
onnxruntime_git_tag=v1.23.2
onnxruntime_git_commit=a83fc4d58cb48eb68890dd689f94f28288cf2278
```

This matters because the models distributed by the plugin use the ONNX Runtime `.ort` format rather than plain `.onnx`.

### Existing runtime state

`src/ort-utils/ORTModelData.hpp` owns:

```cpp
std::unique_ptr<Ort::Session> session;
std::unique_ptr<Ort::Env> env;
```

The rest of the model pipeline already talks to a normal `Ort::Session`.

### Existing session creation

The important integration point is:

```text
src/ort-utils/ort-session-utils.cpp
```

`createOrtSession(filter_data *tf)` creates `Ort::SessionOptions`, configures an Execution Provider, then constructs the `Ort::Session`.

This is the primary seam for Windows ML integration.

### Existing model execution

`src/models/Model.hpp` ultimately executes:

```cpp
session->Run(...)
```

This should remain unchanged if the Windows ML transition is implemented cleanly.

### Existing GPU selection

`src/background-filter.cpp` builds the OBS `Inference Device` list using compile-time macros such as:

```text
HAVE_ONNXRUNTIME_CUDA_EP
HAVE_ONNXRUNTIME_ROCM_EP
HAVE_ONNXRUNTIME_MIGRAPHX_EP
HAVE_ONNXRUNTIME_TENSORRT_EP
```

This design assumes provider availability is known at build time. Windows ML makes runtime provider discovery possible, so the Windows path should gradually become runtime-driven.

---

## 4. Non-goals for the first working version

Do not attempt these until AMD GPU inference is proven stable:

- Rewriting the segmentation models.
- Rewriting the OBS rendering path.
- Converting the plugin into a different ML framework.
- Supporting every Windows GPU vendor at once.
- Adding a complicated provider-management UI.
- Automatic background downloading of every available provider from inside the OBS render thread.
- Refactoring unrelated upstream code.
- Changing Linux ROCm/MIGraphX support.
- Changing macOS CoreML behaviour.
- Optimising zero-copy GPU textures between OBS and ONNX Runtime.
- Replacing OpenCV preprocessing.
- Supporting all segmentation models in the first GPU milestone.
- Removing the CPU implementation.

The first target is deliberately narrow:

> **MediaPipe model → Windows ML → AMD GPU → stable mask in OBS.**

---

## 5. Architectural target

Keep the existing high-level pipeline:

```text
OBS frame
   |
   v
OpenCV preprocessing
   |
   v
Existing Model class
   |
   v
Ort::Session
   |
   +-------------------------------+
   |                               |
   v                               v
CPU EP                    Windows ML registered EP
                                   |
                         +---------+---------+
                         |                   |
                         v                   v
                   MIGraphX AMD          DirectML
```

Add only a Windows-specific provider management layer.

Recommended new files:

```text
src/ort-utils/windows-ml-provider.hpp
src/ort-utils/windows-ml-provider.cpp
```

Optional smoke-test files:

```text
tools/windows-ml-smoke/CMakeLists.txt
tools/windows-ml-smoke/main.cpp
```

Do not add a broad `InferenceBackend` class hierarchy unless the implementation proves that it is necessary. The current `Ort::Session` abstraction already provides most of the backend abstraction required.

### Proposed internal interface

The new Windows-only helper should expose a small API owned by the plugin, independent of Microsoft API details:

```cpp
struct WindowsMlProviderInfo {
    std::string name;
    std::string device_type;
    std::string device_name;
    bool available;
};

std::vector<WindowsMlProviderInfo> discoverWindowsMlProviders();

bool configureWindowsMlProvider(
    Ort::Env& env,
    Ort::SessionOptions& sessionOptions,
    const std::string& requestedProvider,
    std::string& selectedProvider,
    std::string& errorMessage);
```

The exact Windows ML calls inside these functions must be based on the installed stable Windows ML headers. The rest of the plugin should depend only on this small wrapper.

---

# PHASE A — Repository control and baseline

## Milestone A1 — Create the fork and protected baseline

### Objective

Create a fork whose first commit is functionally identical to upstream tag `1.4.1`.

### Steps

- [ ] Fork `royshil/obs-backgroundremoval`.
- [ ] Clone the fork.
- [ ] Create a branch from tag `1.4.1`, for example:

```powershell
git fetch --tags
git checkout -b feature/windows-ml-amd 1.4.1
```

- [ ] Add the upstream remote:

```powershell
git remote add upstream https://github.com/royshil/obs-backgroundremoval.git
git remote -v
```

- [ ] Record the starting commit:

```powershell
git rev-parse HEAD
git status
```

- [ ] Copy this roadmap into:

```text
docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md
```

- [ ] Commit the roadmap separately from code changes.

### Acceptance criteria

- `git status` is clean.
- Branch points at the 1.4.1 code.
- Upstream remote exists.
- Roadmap is committed.
- No functional source changes have been made.

### Suggested Codex prompt

```text
Read docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md.

We are starting from upstream tag 1.4.1.
Do not implement Windows ML yet.

Inspect the repository and report:
1. Windows build entry points.
2. How ONNX Runtime is built and linked.
3. Where Ort::Env is created.
4. Where Ort::Session is created.
5. How the Inference Device UI is populated.
6. Which files would need to change for a minimal Windows-only provider integration.

Do not modify files.
Return concrete file paths and function names.
```

---

## Milestone A2 — Build 1.4.1 unchanged

### Objective

Prove that the local machine can build the project before introducing Windows ML.

### Steps

- [ ] Install the build prerequisites expected by the repository.
- [ ] Run the existing Windows setup/bootstrap procedure from the repository.
- [ ] Build the unchanged plugin using the repository's Windows CMake preset/workflow.
- [ ] Package/install it locally.
- [ ] Start OBS.
- [ ] Add `Background Removal` to a webcam source.
- [ ] Confirm CPU inference works.
- [ ] Save the OBS log from this run.

### What Codex must not do

Codex must not "fix" build warnings by broadly modifying source files. If the baseline build fails, isolate the environmental/build failure first.

### Acceptance criteria

- Release/RelWithDebInfo build succeeds.
- OBS loads `obs-backgroundremoval.dll`.
- CPU background removal works.
- No new crash occurs.
- Baseline OBS CPU usage and filter behaviour are recorded.

### Commit

No code commit is necessary if no source files changed.

---

# PHASE B — Compatibility research in code, not assumptions

## Milestone B1 — Map ONNX Runtime compatibility

### Objective

Determine whether the plugin's existing `.ort` model files can be loaded by the chosen Windows ML ONNX Runtime.

### Recommended first package line

Start by evaluating the stable Windows ML **1.8.x** line because current 1.8.x releases use ONNX Runtime 1.23.x, close to the plugin's pinned 1.23.2 runtime.

Do not permanently lock the project to 1.8.x until the smoke test passes.

### Steps

- [ ] Record the exact Windows ML package version selected for the spike.
- [ ] Record the ONNX Runtime version contained by that package.
- [ ] Verify that the package supports C++ and the selected deployment mode.
- [ ] Build a trivial executable that only initialises Windows ML's ONNX Runtime.
- [ ] Attempt to load:

```text
data/models/mediapipe.with_runtime_opt.ort
```

- [ ] Create a CPU session first.
- [ ] Query model inputs and outputs.
- [ ] Exit successfully.

### Failure branch

If the `.ort` model fails to load:

1. Capture the exact ONNX Runtime error.
2. Test another plugin `.ort` model to distinguish model-specific from format-wide failure.
3. Test the same model with the original plugin's ORT 1.23.2 runtime.
4. Compare the ORT versions.
5. Only if incompatibility is confirmed, create a separate model-regeneration task.

Do not immediately convert every model.

### Acceptance criteria

The Windows ML-provided runtime can create a CPU `Ort::Session` from the exact MediaPipe `.ort` file distributed by the plugin.

---

# PHASE C — Standalone Windows ML smoke test

## Milestone C1 — Add a standalone C++ smoke-test target

### Objective

Remove OBS from the debugging equation.

### Files

Create:

```text
tools/windows-ml-smoke/CMakeLists.txt
tools/windows-ml-smoke/main.cpp
```

Modify only the minimum parent CMake necessary to opt into building the tool.

The smoke tool must not depend on OBS rendering.

### Required smoke-test behaviour

On startup it must print:

- Windows version/build.
- Architecture.
- Windows ML package/runtime version if obtainable.
- ONNX Runtime version.
- Available Windows ML providers.
- Provider ready state.
- ORT EP devices visible after registration.
- Selected provider.
- Selected device name/type.
- Model path.
- Session creation result.
- Input/output tensor shapes.

### First mode

```text
windows-ml-smoke.exe --provider cpu --model <path>
```

This must work before testing AMD.

### Second mode

```text
windows-ml-smoke.exe --list-providers
```

This must enumerate available Windows ML EPs without loading OBS.

### Acceptance criteria

- Tool compiles in x64 Release mode.
- CPU model session succeeds.
- Provider discovery runs without crashing.
- Tool output is sufficiently detailed that a bug report can be diagnosed from its console log alone.

### Suggested Codex prompt

```text
Implement Milestone C1 only.

Create a small C++20 CMake smoke-test executable under tools/windows-ml-smoke.
It must use Windows ML's ONNX Runtime and must not depend on OBS rendering.

Required CLI:
  --list-providers
  --provider cpu
  --model <path>

For this milestone, provider=cpu is enough.
Print runtime/version/provider/device/model/session information.
Keep the code small and Windows-only.

Build it and show the exact build command and output.
Do not modify the OBS filter yet.
```

---

## Milestone C2 — Discover and prepare MIGraphX

### Objective

Determine whether Windows ML exposes `MIGraphXExecutionProvider` on the RX 9070 XT.

### Important constraint

Do not assume that presence in Microsoft documentation guarantees compatibility with the installed GPU driver.

The tool must query the actual device.

### Provider flow

Use Windows ML's provider catalog to:

1. Find providers compatible with the machine.
2. Locate `MIGraphXExecutionProvider`.
3. Record its ready state.
4. If it is already ready, register it.
5. If it is not present, prepare it through a deliberate user-run bootstrap path.
6. Re-enumerate ORT EP devices after registration.

### Avoid provider downloads inside OBS

For the initial fork, keep provider installation out of the OBS process.

Preferred design:

```text
windows-ml-smoke.exe --prepare-provider MIGraphXExecutionProvider
```

This command may trigger Windows ML provider acquisition.

The OBS plugin should later consume a provider that is already available.

This avoids:

- Blocking OBS startup.
- Blocking an OBS UI/render thread.
- Unexpected network access during streaming.
- First-run delays that appear as OBS freezes.

### Acceptance criteria

One of these outcomes must be captured clearly:

**Success:**

```text
MIGraphXExecutionProvider
Ready/registered
AMD GPU device visible
```

**Controlled incompatibility:**

```text
MIGraphXExecutionProvider unavailable
Reason/status recorded
```

A controlled incompatibility is still a valid milestone result because it decides whether DirectML becomes the primary path on this machine.

---

## Milestone C3 — Run MediaPipe inference on MIGraphX

### Objective

Prove actual model computation, not merely provider enumeration.

### Required work

Extend the smoke test to:

- Load the plugin's MediaPipe model.
- Allocate valid input and output tensors.
- Run a warm-up.
- Run at least 100 inference iterations.
- Measure per-inference latency.
- Print average, median, p95, minimum, and maximum latency.
- Confirm outputs contain finite values.
- Confirm output dimensions are correct.

Use a deterministic test image if practical.

### Correctness comparison

Run the same input through:

```text
CPU
MIGraphX
```

Compare output masks.

Required checks:

- identical shape;
- no NaN/Inf;
- values within expected postprocessing range;
- normalised mean absolute error <= 0.05;
- binary-mask intersection-over-union >= 0.95 using threshold 0.5.

If the numeric difference exceeds these limits, save both output masks for inspection and do not integrate into OBS yet.

### Acceptance criteria

- 100 consecutive GPU inference calls complete.
- No crash.
- No frozen output.
- Output is numerically plausible.
- CPU/GPU output comparison passes.
- Latency statistics are recorded.

### Commit

```text
test(winml): validate mediapipe inference on AMD provider
```

---

# PHASE D — DirectML fallback spike

## Milestone D1 — Prove DirectML separately

### Objective

Have a fallback path if MIGraphX is unavailable or unstable.

Windows ML includes DirectML, but previous upstream plugin versions had DirectML stability problems. Treat it as a separately validated backend, not an assumed solution.

### Steps

Using the same smoke test:

```text
--provider DirectML
```

Run the exact test matrix from C3.

### Acceptance criteria

- Session creation succeeds.
- 100 iterations complete.
- CPU/DirectML output comparison passes.
- Latency is recorded.
- No process crash or hang.

### Decision rule

After C3 and D1, select the default AMD provider:

```text
If MIGraphX passes -> default AMD provider = MIGraphX
Else if DirectML passes -> default AMD provider = DirectML
Else -> do not integrate GPU mode yet
```

Do not hide a failed MIGraphX attempt by silently using DirectML in the smoke tool. Make the fallback explicit in logs.

---

# PHASE E — Introduce Windows ML into the plugin build

## Milestone E1 — Replace the Windows-only ONNX Runtime dependency path

### Objective

Make the Windows plugin build against a Windows ML-compatible ONNX Runtime without changing Linux/macOS.

### Likely files

```text
CMakeLists.txt
scripts/BuildOnnxRuntime.psm1
.github/workflows/plugin-build-windows.yml
buildspec.props
```

Not every file necessarily needs to remain modified after the final design. Prefer deleting obsolete Windows-only ONNX Runtime build complexity if Windows ML fully replaces it, while retaining non-Windows behaviour.

### Rules

- Wrap Windows-specific changes in the existing `MSVC` / `_WIN32` platform conditions.
- Do not change Apple CoreML code.
- Do not change Linux CUDA/ROCm/MIGraphX/TensorRT behaviour.
- Do not carry two ONNX Runtime copies in the same plugin process unless a compatibility problem forces it.
- If a temporary dual-runtime spike is used, remove it before the first releasable build unless there is a documented reason to retain it.

### Preferred link model

Use the stable Windows ML CMake targets supplied by its package rather than manually copying arbitrary headers/libraries.

The current Microsoft documentation exposes targets in the form:

```text
WindowsML::Api
WindowsML::OnnxRuntime
WindowsML::DirectML
```

Codex must validate target names against the exact selected stable package before committing the build change.

### Acceptance criteria

- Windows plugin builds.
- Linux/macOS CMake configuration is not semantically altered.
- Windows output contains the required runtime DLLs/dependencies.
- OBS can load the plugin.
- CPU inference still works.

---

# PHASE F — Add the Windows provider helper

## Milestone F1 — Create `windows-ml-provider`

### Objective

Keep Windows ML API details out of `background-filter.cpp` and out of model classes.

### Files

Create:

```text
src/ort-utils/windows-ml-provider.hpp
src/ort-utils/windows-ml-provider.cpp
```

Modify:

```text
CMakeLists.txt
```

### Responsibilities

The helper owns:

- Windows ML provider discovery.
- Registration of already-installed provider libraries.
- ORT EP-device enumeration.
- Selection by provider name.
- SessionOptions provider attachment.
- Human-readable diagnostics.

It does **not** own:

- Frame processing.
- OBS rendering.
- Model preprocessing.
- Model postprocessing.
- UI widgets.
- Network downloading during inference.

### Error policy

Every failure returns a structured error to the caller and logs enough context to diagnose:

```text
requested provider
provider discovery state
registration result
device enumeration result
session attachment result
fallback decision
```

### Acceptance criteria

- Helper can be compiled into the plugin.
- Existing CPU mode remains functional.
- Provider discovery can run from the plugin without creating a GPU session.
- No Windows ML details leak into `Model.hpp`.

---

# PHASE G — Integrate provider selection into session creation

## Milestone G1 — Add provider identifiers

### Objective

Represent Windows ML providers without reusing misleading old provider names.

### File

Modify:

```text
src/consts.h
```

Recommended logical identifiers:

```text
cpu
winml-migraphx
winml-directml
```

Keep existing Linux/macOS identifiers unchanged.

Do not call Windows ML MIGraphX simply `rocm`.

### Acceptance criteria

Provider strings are unique and platform semantics are clear.

---

## Milestone G2 — Configure the Windows GPU provider in `createOrtSession`

### Objective

Make the smallest possible change to the existing session lifecycle.

### File

Modify:

```text
src/ort-utils/ort-session-utils.cpp
```

### Required behaviour

Pseudo-flow:

```text
create SessionOptions

if CPU:
    configure CPU thread counts
else:
    disable memory pattern
    sequential execution

if Windows + winml-migraphx:
    ask windows-ml-provider to append MIGraphX EP
if Windows + winml-directml:
    ask windows-ml-provider to append DirectML EP

create Ort::Session exactly as before
populate names/shapes exactly as before
allocate buffers exactly as before
```

The existing `Model` classes should remain unaware of the provider.

### Fallback policy

If a requested GPU provider fails:

1. Log the exact failure.
2. Destroy/reset the failed partial session state.
3. Create a new CPU `SessionOptions`.
4. Create a CPU session.
5. Store/report that the effective device is CPU.
6. Keep the filter alive.

The UI may continue to display the requested setting, but the plugin log and a future status field must state the effective provider.

### Acceptance criteria

- CPU selection works.
- MIGraphX selection creates a GPU session when available.
- DirectML selection creates a GPU session when available.
- Provider failure does not crash OBS.
- Provider failure falls back to CPU.
- Existing tensor/model code remains unchanged unless a proven compatibility issue requires a narrowly scoped change.

---

# PHASE H — OBS UI integration

## Milestone H1 — Populate Windows inference choices safely

### Objective

Expose only sensible Windows options.

### File

Modify:

```text
src/background-filter.cpp
```

### Initial UI policy

When `Advanced` settings are enabled, show:

```text
CPU
GPU - AMD (Windows ML / MIGraphX)
GPU - Windows ML / DirectML
```

Only show MIGraphX if discovery says it is usable or installed.

DirectML may be shown if the Windows ML runtime supports it.

If runtime discovery is too expensive for the property-construction path, cache provider availability during plugin initialisation.

### Do not

- Download providers when opening filter properties.
- Block the UI waiting for provider installation.
- run expensive inference to populate the combo box.

### Acceptance criteria

- CPU always appears.
- Available Windows GPU providers appear.
- Unavailable providers do not cause errors.
- Switching provider causes the model session to reinitialise through the existing update path.
- The filter remains functional after switching CPU → GPU → CPU repeatedly.

---

## Milestone H2 — Add effective-provider logging

### Objective

Make it impossible to think GPU mode is active when inference actually fell back to CPU.

Log a line after successful session creation, for example:

```text
[obs-backgroundremoval] Requested inference provider: winml-migraphx
[obs-backgroundremoval] Effective inference provider: MIGraphXExecutionProvider
[obs-backgroundremoval] EP device: <reported device name>
```

On fallback:

```text
[obs-backgroundremoval] Requested GPU provider failed: <reason>
[obs-backgroundremoval] Falling back to CPU
[obs-backgroundremoval] Effective inference provider: CPU
```

### Acceptance criteria

One OBS log is enough to determine which provider really executed the model.

---

# PHASE I — MediaPipe-only integrated validation

## Milestone I1 — Lock the first GPU integration to MediaPipe

### Objective

Reduce debugging surface.

During early integration, if Windows GPU mode is selected and a non-MediaPipe model is selected, either:

- fall back to CPU for that model; or
- clearly mark other models as experimental only after separate validation.

Do not claim support for all models because one model works.

### Test matrix

Run:

```text
Provider: CPU
Model: MediaPipe

Provider: MIGraphX
Model: MediaPipe

Provider: DirectML
Model: MediaPipe
```

For each:

- Start OBS.
- Activate source.
- Move continuously in frame.
- Hide/show source.
- Switch scenes.
- Disable/enable filter.
- Switch inference device.
- Stop/start virtual camera if used.
- Start/stop recording.
- Run for 10 minutes.

### Acceptance criteria

- Mask updates continuously.
- No one-frame freeze.
- No corrupted silhouette.
- No crash.
- No growing memory trend.
- Switching providers works repeatedly.

---

# PHASE J — Validate every segmentation model independently

## Milestone J1 — Model compatibility matrix

Test each model separately:

```text
SINet
MediaPipe
Selfie Segmentation
Selfie Multiclass
PPHumanSeg
Robust Video Matting
TCMonoDepth
```

Create a compatibility table in project documentation:

| Model | CPU | MIGraphX | DirectML | Notes |
|---|---|---|---|---|
| MediaPipe | Pass | Test | Test | Baseline model |
| SINet | Pass | Test | Test | |
| Selfie Segmentation | Pass | Test | Test | |
| Selfie Multiclass | Pass | Test | Test | |
| PPHumanSeg | Pass | Test | Test | |
| Robust Video Matting | Pass | Test | Test | Stateful/temporal behaviour |
| TCMonoDepth | Pass | Test | Test | Depth path |

For each provider/model combination:

- create session;
- run at least 100 smoke-test inferences;
- run at least 5 minutes in OBS;
- confirm the mask/output changes with movement;
- inspect logs for provider fallback.

### Important RVM rule

Treat Robust Video Matting separately because it has temporal/stateful behaviour. A successful single inference is not sufficient.

### Acceptance criteria

Only combinations that pass are advertised as supported.

---

# PHASE K — Threading and lifecycle hardening

## Milestone K1 — Verify provider work is not on critical OBS threads

### Objective

Prevent provider preparation or runtime initialisation from freezing OBS.

Audit:

```text
background_filter_update
createOrtSession
background_removal_thread
plugin initialisation
properties callbacks
```

Rules:

- No provider download from render/inference loop.
- No provider catalogue acquisition on every frame.
- No repeated registration per frame.
- Session recreation occurs only when required.
- Long provider preparation stays in the external smoke/bootstrap tool for the first release.

### Acceptance criteria

OBS remains responsive while adding/removing the filter and switching scenes.

---

## Milestone K2 — Session teardown stress test

### Objective

Find lifetime bugs before release.

Automate or manually repeat at least 50 cycles:

```text
CPU -> MIGraphX -> CPU -> MIGraphX
```

Also repeat:

```text
add filter -> remove filter
```

Check:

- crashes;
- deadlocks;
- VRAM growth;
- RAM growth;
- stale mask;
- failed session recreation.

### Acceptance criteria

No crash/deadlock and memory stabilises rather than increasing monotonically across cycles.

---

# PHASE L — Performance instrumentation

## Milestone L1 — Add debug inference timing

Add debug-only or rate-limited timing around:

```text
tf->model->runNetworkInference(...)
```

Do not print once per frame indefinitely.

Recommended reporting interval:

```text
every 300 inference calls
```

Report:

- provider;
- average;
- p95 if tracked;
- model name;
- inference count.

### Acceptance criteria

Performance data can be obtained without flooding OBS logs.

---

## Milestone L2 — Benchmark RX 9070 XT

Use a fixed test scene.

Recommended first benchmark:

```text
OBS canvas: 1920x1080
Output: 1920x1080
FPS: 60
Model: MediaPipe
Calculate mask every X frame: 1
Same camera/source for all runs
```

Run separate 5-minute captures:

```text
CPU
MIGraphX
DirectML
```

Record results in:

```text
docs/windows-ml-benchmarks.md
```

### Decision

Prefer MIGraphX when:

- stable;
- output quality passes;
- inference latency beats CPU;
- no major render regression.

Keep DirectML primarily as fallback if it is stable but slower.

---

# PHASE M — Packaging

## Milestone M1 — Decide deployment strategy deliberately

Windows ML supports different deployment approaches. The fork must choose one based on OBS plugin constraints.

Evaluate:

### Option 1 — Self-contained

Advantages:

- predictable runtime;
- does not depend on a separately installed shared Windows ML runtime;
- suitable for an unpackaged C++ application/plugin scenario.

Costs:

- larger plugin package;
- plugin maintainer owns runtime updates.

### Option 2 — Framework-dependent

Advantages:

- smaller plugin distribution;
- serviced runtime.

Costs:

- runtime installation/dependency requirements;
- more complexity when the DLL is loaded by another host application.

### Recommendation for first distributable fork

Prefer **self-contained** unless testing proves framework-dependent deployment inside OBS is simpler and reliable.

OBS is the host process, so predictable plugin-local dependencies are valuable.

### Acceptance criteria

A clean machine can install the fork and load OBS without Visual Studio, developer environment variables, or source checkout.

---

## Milestone M2 — Clean-machine installation test

Use Windows Sandbox, a VM, or another Windows 11 PC.

Test:

1. Install OBS.
2. Install the fork package.
3. Prepare the AMD provider if the design requires the bootstrap helper.
4. Start OBS.
5. Add webcam.
6. Add filter.
7. Select GPU mode.
8. Verify effective provider in log.
9. Record for 10 minutes.

### Acceptance criteria

No missing DLL error and no developer-only dependency.

---

# PHASE N — CI and regression protection

## Milestone N1 — Preserve upstream build coverage

Modify the Windows workflow only as required.

CI must at minimum:

- configure CMake;
- compile Release/RelWithDebInfo;
- package;
- fail on missing Windows ML dependencies.

Keep Linux/macOS workflows operational.

### Acceptance criteria

No platform is silently broken by Windows-only integration.

---

## Milestone N2 — Add smoke-test CI mode

CI hardware will probably not provide the target AMD GPU.

Therefore split testing:

### CI

- compile smoke tool;
- run CPU session;
- validate model loading;
- test provider-wrapper error handling where possible.

### Physical RX 9070 XT machine

- MIGraphX functional test;
- DirectML functional test;
- performance benchmark;
- OBS stability test.

Do not fake GPU success in CI.

---

# PHASE O — Failure handling and user experience

## Milestone O1 — Define fallback states

The plugin must distinguish:

```text
Requested provider
Available provider
Effective provider
Fallback reason
```

Examples:

```text
Requested: winml-migraphx
Available: no
Effective: cpu
Reason: provider not present
```

```text
Requested: winml-migraphx
Available: yes
Effective: cpu
Reason: session creation failed
```

```text
Requested: winml-migraphx
Available: yes
Effective: MIGraphXExecutionProvider
Reason: none
```

### Acceptance criteria

No silent fallback.

---

## Milestone O2 — Useful errors

Errors should include actions, not vague failures.

Bad:

```text
GPU failed
```

Good:

```text
MIGraphXExecutionProvider was not available in the Windows ML provider catalogue.
Run the provider preparation tool and verify the AMD driver supported by the installed Windows ML EP.
Falling back to CPU.
```

Do not make the OBS filter unusable because acceleration failed.

---

# PHASE P — Release candidate validation

## Milestone P1 — Full stress matrix

Run all of the following on the RX 9070 XT:

### Session lifetime

- 50 CPU/GPU switches.
- 50 filter add/remove cycles.
- 20 OBS scene switches.
- OBS restart with GPU setting persisted.

### Runtime

- 30 minutes idle camera.
- 30 minutes continuous movement.
- 30 minutes recording.
- Optional 30 minutes streaming if available.

### Source lifecycle

- camera disconnect/reconnect;
- source hide/show;
- scene inactive/active;
- OBS minimise/restore;
- display sleep/wake if practical.

### Provider failure

- provider unavailable;
- unsupported provider requested from old config;
- corrupted/missing model;
- session creation error.

### Acceptance criteria

No reproducible crash, deadlock, permanent frozen mask, or silent CPU fallback.

---

# PHASE Q — Documentation for the fork

## Milestone Q1 — Write Windows AMD instructions

Update README or add:

```text
docs/windows-amd-gpu.md
```

It must explain:

- supported Windows version;
- supported provider paths;
- provider priority;
- how to prepare/install a Windows ML EP;
- how to confirm the effective provider from OBS logs;
- how CPU fallback behaves;
- known model compatibility;
- benchmark results;
- how to collect a useful bug report.

Avoid claiming generic "AMD GPU support" until multiple cards are tested.

For the first release, phrase support as tested hardware, for example:

```text
Tested: Radeon RX 9070 XT
Provider: MIGraphXExecutionProvider
```

---

# PHASE R — Upstream strategy

## Milestone R1 — Keep the patch reviewable

Before proposing anything upstream:

```powershell
git diff 1.4.1...HEAD --stat
git log --oneline --decorate 1.4.1..HEAD
```

Check that commits tell a coherent story.

Recommended commit sequence:

```text
docs: add Windows ML AMD implementation plan
test: add standalone Windows ML smoke tool
build(windows): integrate Windows ML runtime
feat(winml): add provider discovery and registration
feat(winml): add MIGraphX session selection
feat(winml): add DirectML fallback
feat(obs): expose Windows ML inference devices
fix(winml): harden session fallback and lifecycle
test: add Windows ML model compatibility checks
docs: document AMD GPU support and benchmarks
```

### Upstream PR characteristics

A strong upstream PR should:

- cite the existing AMD/Windows issues;
- explain why Windows ML has changed the feasibility since older discussions;
- keep the implementation Windows-only;
- preserve CPU fallback;
- avoid unrelated refactors;
- include real RX 9070 XT benchmark/stability evidence;
- explicitly mention provider acquisition/deployment requirements.

Do not begin by arguing that maintainers were wrong. Their earlier DirectML concerns were valid for the architecture and ecosystem at that time. Present Windows ML/MIGraphX as a new technical path with evidence.

---

# 6. Codex operating rules

Use these rules at the top of every implementation session.

```text
You are working on a fork of royshil/obs-backgroundremoval based on tag 1.4.1.

Read docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md before changing code.

Rules:
1. Work on exactly one milestone at a time.
2. Inspect existing code before editing.
3. Do not rewrite model preprocessing/postprocessing unless a failing test proves it is necessary.
4. Do not change Linux or macOS behaviour.
5. Keep CPU inference as a working fallback at every commit.
6. Never silently fall back from GPU to CPU; log requested and effective providers.
7. Do not download Windows ML providers from the OBS render/inference thread.
8. Prefer a small Windows-only provider wrapper over a broad architecture rewrite.
9. Build after every meaningful source change.
10. Run the milestone's verification before claiming success.
11. If a build or runtime test fails, diagnose the failure before making speculative changes.
12. Show the exact files changed and why.
13. Show the exact verification commands and their output summary.
14. Make a small commit only after the milestone passes.
15. Stop after the requested milestone and report the result.
```

---

# 7. Recommended prompts, in order

## Prompt 1 — Repository audit

```text
Read the roadmap.
Perform Milestone A1 repository audit only.
Do not modify code.
Report exact Windows build paths, ORT lifecycle, provider UI code, and the smallest integration seam.
```

## Prompt 2 — Baseline build

```text
Perform Milestone A2 only.
Build the project unchanged on Windows.
Do not alter functionality to make the build easier.
If it fails, diagnose the build environment.
```

## Prompt 3 — Windows ML CPU compatibility spike

```text
Perform Milestone B1.
Use a stable Windows ML package compatible with C++.
Prefer the stable 1.8.x line for the first compatibility test because the plugin uses ORT 1.23.2.
Prove that data/models/mediapipe.with_runtime_opt.ort can create a CPU Ort::Session.
Do not modify the OBS filter.
```

## Prompt 4 — Smoke tool

```text
Perform Milestone C1 only.
Create the Windows-only smoke tool.
It must enumerate runtime details and load the MediaPipe model on CPU.
Keep changes isolated.
```

## Prompt 5 — AMD provider discovery

```text
Perform Milestone C2 only.
Add Windows ML provider discovery and provider preparation support to the smoke tool.
Target MIGraphXExecutionProvider.
Do not assume the provider is available; report its real ReadyState and device information.
Do not touch the OBS filter.
```

## Prompt 6 — GPU inference

```text
Perform Milestone C3 only.
Run the actual MediaPipe model through MIGraphX.
Compare GPU output against CPU output and benchmark 100 iterations.
Do not integrate with OBS unless all acceptance criteria pass.
```

## Prompt 7 — DirectML fallback

```text
Perform Milestone D1 only.
Test Windows ML DirectML with the same model, correctness metrics, and benchmark.
Keep MIGraphX results separate.
```

## Prompt 8 — Build integration

```text
Perform Milestone E1 only.
Integrate Windows ML into the Windows plugin build while preserving Linux and macOS.
CPU mode must still build and run.
Do not expose GPU in OBS UI yet.
```

## Prompt 9 — Provider wrapper

```text
Perform Milestone F1 only.
Create src/ort-utils/windows-ml-provider.hpp/.cpp.
Move Windows ML discovery/registration/device-selection details into this wrapper.
Do not modify Model.hpp.
```

## Prompt 10 — Session integration

```text
Perform Milestones G1 and G2 only if G1 passes first.
Add winml-migraphx and winml-directml provider identifiers.
Integrate the wrapper into createOrtSession().
Implement explicit CPU fallback with logs.
Do not modify the OBS UI yet.
```

## Prompt 11 — UI

```text
Perform Milestones H1 and H2.
Expose only available Windows ML inference choices.
Do not download providers from the properties UI.
Log requested and effective providers.
```

## Prompt 12 — MediaPipe validation

```text
Perform Milestone I1.
Test CPU, MIGraphX, and DirectML with MediaPipe in OBS.
Do not expand support to other models yet.
Report failures before changing architecture.
```

## Prompt 13 — Model matrix

```text
Perform Milestone J1.
Validate each segmentation model independently.
Produce the compatibility matrix.
Do not mark a combination supported unless the smoke test and OBS test both pass.
```

## Prompt 14 — Hardening

```text
Perform Phase K.
Audit threading/lifecycle and run provider-switch/filter-lifetime stress tests.
Fix only reproducible lifecycle defects.
```

## Prompt 15 — Benchmark

```text
Perform Phase L.
Add rate-limited inference timing and benchmark CPU vs MIGraphX vs DirectML on the RX 9070 XT.
Write docs/windows-ml-benchmarks.md.
```

## Prompt 16 — Packaging

```text
Perform Phase M.
Evaluate self-contained vs framework-dependent Windows ML deployment in the context of an unpackaged OBS plugin DLL.
Prefer self-contained for the first release unless testing proves another path is more reliable.
Validate on a clean Windows environment.
```

## Prompt 17 — Release review

```text
Review the complete implementation against the roadmap.
Do not add new features.

Check:
- Windows build
- CPU fallback
- provider logging
- provider lifecycle
- model compatibility
- clean-machine installation
- Linux/macOS isolation
- documentation
- benchmark evidence

List any unmet acceptance criterion.
```

---

# 8. Debugging decision tree

## OBS does not load the plugin

Check in this order:

```text
missing runtime DLL
architecture mismatch
Windows ML deployment/bootstrap issue
C++ runtime dependency
ONNX Runtime duplicate/conflict
plugin binary dependency search path
```

Use a dependency inspection tool before modifying inference code.

---

## Windows ML works in smoke tool but not OBS

Suspect:

```text
DLL search path inside obs64.exe
Windows App SDK initialisation/deployment context
duplicate onnxruntime.dll loaded in process
provider library registration path
provider registration occurring too late
different working directory
```

Compare loaded modules and absolute library paths.

Do not rewrite model code.

---

## Provider appears but session creation fails

Capture:

```text
provider name
provider library path
device list
model path
ORT version
exact exception/error
```

Then test:

```text
same provider + trivial known-good ONNX model
same provider + MediaPipe model
CPU + MediaPipe model
```

This separates provider failure from model/operator incompatibility.

---

## GPU session succeeds but CPU usage remains high

Remember that GPU inference does not eliminate:

```text
OpenCV colour conversion
resize
tensor upload/copy
mask postprocessing
OBS rendering
camera capture
encoding
```

Prove acceleration using:

- selected provider log;
- inference-time measurement;
- GPU compute utilisation.

Do not use total CPU usage alone as proof that GPU inference failed.

---

## Mask freezes after first frame

Check:

```text
session Run errors
stateful model inputs/outputs
output tensor ownership
thread synchronisation
provider-specific unsupported operation
session recreation
maskEveryXFrames logic
```

Reproduce first with MediaPipe. If MediaPipe is stable but RVM freezes, treat it as a model-specific issue.

---

# 9. Security and reliability rules

- Do not download DLLs from random GitHub comments or third-party mirrors.
- Prefer official Microsoft/AMD/provider distribution mechanisms.
- Pin dependency versions in reproducible builds.
- Record hashes where the existing project does so.
- Do not execute provider downloads silently during a stream.
- Do not swallow ONNX Runtime exceptions.
- Do not log private filesystem information unnecessarily in release builds.
- Keep provider acquisition separate from per-frame inference.
- Never remove CPU fallback.
- Avoid loading two different `onnxruntime.dll` copies into `obs64.exe` without a deliberate compatibility design.

---

# 10. Git strategy

Recommended branch structure:

```text
main
  |
  +-- feature/windows-ml-amd
         |
         +-- spike/windows-ml-smoke
```

The smoke spike can be developed first and then merged/cherry-picked into the feature branch after it proves the chosen runtime/provider combination.

Before every risky milestone:

```powershell
git status
git log --oneline -10
git tag local-before-<milestone-name>
```

Keep commits reversible.

A failed experimental approach should be reverted rather than buried under more fixes.

---

# 11. Release stages

## Stage 0 — Research build

Audience:

```text
developer only
```

Capabilities:

```text
smoke tool
provider discovery
CPU model load
```

## Stage 1 — Hardware proof

Audience:

```text
developer only
```

Capabilities:

```text
MediaPipe + RX 9070 XT + MIGraphX/DirectML
```

## Stage 2 — Local OBS alpha

Audience:

```text
your machine
```

Capabilities:

```text
OBS UI
CPU fallback
MediaPipe GPU mode
diagnostic logging
```

## Stage 3 — Private beta

Audience:

```text
a few AMD Windows testers
```

Capabilities:

```text
validated model matrix
installer/package
documentation
```

## Stage 4 — Public fork release

Requirements:

```text
clean-machine install
30-minute stability test
provider diagnostics
known-issues documentation
benchmark evidence
```

## Stage 5 — Upstream proposal

Only after the fork has real-world evidence.

---

# 12. Questions the implementation must answer with evidence

By the end of the project, the repository should contain evidence-backed answers to:

1. Can Windows ML load the existing MediaPipe `.ort` model unchanged?
2. Which Windows ML package/deployment model is most reliable inside OBS?
3. Does `MIGraphXExecutionProvider` appear on the RX 9070 XT with the installed AMD driver?
4. Can it execute every operator required by MediaPipe?
5. Is it faster than CPU for this workload?
6. Does DirectML work as a stable fallback?
7. Does the provider remain stable across repeated session destruction/recreation?
8. Which plugin models are compatible with MIGraphX?
9. Which are compatible with DirectML?
10. Can a clean Windows user install the fork without a development environment?
11. Can failures always fall back to CPU without crashing OBS?
12. Can logs always reveal the effective inference provider?

Do not replace these questions with assumptions.

---

# 13. Source references

## Upstream plugin

- Repository/tag: https://github.com/royshil/obs-backgroundremoval/tree/1.4.1
- AMD RX 9070 XT issue: https://github.com/royshil/obs-backgroundremoval/issues/724
- Windows ONNX Runtime direction issue: https://github.com/royshil/obs-backgroundremoval/issues/655

Key source files:

```text
CMakeLists.txt
buildspec.props
scripts/BuildOnnxRuntime.psm1
.github/workflows/plugin-build-windows.yml
src/FilterData.hpp
src/ort-utils/ORTModelData.hpp
src/ort-utils/ort-session-utils.cpp
src/models/Model.hpp
src/background-filter.cpp
src/consts.h
```

## Microsoft Windows ML

- Get started:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/get-started
- Migrate from standalone ONNX Runtime:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/migrate-to-windows-ml
- Windows ML Execution Providers:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/supported-execution-providers
- Install/prepare Execution Providers:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/initialize-execution-providers
- Select Execution Providers:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/select-execution-providers
- ONNX Runtime versions shipped with Windows ML:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/onnx-versions
- Install/deploy Windows ML:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/distributing-your-app
- Windows ML samples:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/samples
- Bring your own EP:
  https://learn.microsoft.com/en-us/windows/ai/new-windows-ml/bring-your-own-eps

---

# 14. Final release checklist

## Build

- [ ] Windows x64 Release build passes.
- [ ] Installer/package builds.
- [ ] Clean-machine plugin load passes.
- [ ] Linux code paths remain untouched or verified.
- [ ] macOS code paths remain untouched or verified.

## Runtime

- [ ] CPU works.
- [ ] MIGraphX works on tested RX 9070 XT, or is explicitly marked unavailable.
- [ ] DirectML fallback is independently validated.
- [ ] Requested/effective provider is logged.
- [ ] Provider failure falls back to CPU.

## Models

- [ ] MediaPipe validated.
- [ ] SINet validated.
- [ ] Selfie Segmentation validated.
- [ ] Selfie Multiclass validated.
- [ ] PPHumanSeg validated.
- [ ] RVM validated.
- [ ] TCMonoDepth validated.

## Stability

- [ ] 50 provider switches pass.
- [ ] 50 filter add/remove cycles pass.
- [ ] 30-minute continuous inference passes.
- [ ] 30-minute recording passes.
- [ ] No monotonic RAM leak observed.
- [ ] No monotonic VRAM leak observed.
- [ ] No frozen mask observed.

## Performance

- [ ] CPU baseline recorded.
- [ ] MIGraphX benchmark recorded.
- [ ] DirectML benchmark recorded.
- [ ] GPU mode demonstrates real inference acceleration on target hardware.

## Documentation

- [ ] AMD Windows setup documented.
- [ ] Provider preparation documented.
- [ ] Effective-provider verification documented.
- [ ] Compatibility matrix documented.
- [ ] Known issues documented.
- [ ] Benchmark documented.

## Git

- [ ] Commits are small and reviewable.
- [ ] No unrelated refactors.
- [ ] No debug binaries committed.
- [ ] No locally downloaded provider DLLs committed accidentally.
- [ ] Diff against 1.4.1 reviewed.
- [ ] Release tag created only after clean-machine validation.

---

# 15. Recommended first action

Do **not** start by editing `background-filter.cpp`.

Start with this exact sequence:

```text
1. Fork tag 1.4.1.
2. Build it unchanged.
3. Create the standalone Windows ML smoke tool.
4. Load the exact MediaPipe .ort model with Windows ML CPU.
5. Enumerate MIGraphXExecutionProvider.
6. Prepare/register MIGraphX outside OBS.
7. Run 100 MediaPipe inferences on the RX 9070 XT.
8. Compare output with CPU.
9. Benchmark.
10. Only then touch the OBS plugin inference-device path.
```

If step 7 succeeds, the central technical risk of the entire project has been removed.

At that point the remaining work is primarily integration, lifecycle hardening, packaging, and testing rather than proving that AMD GPU inference is possible.

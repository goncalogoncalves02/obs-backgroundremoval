# Windows ML AMD baseline

## Scope

This document records the functionally unchanged baseline used for the Windows ML AMD GPU work. The source baseline is `main` commit `9772c540279cc84b8c5be5442c50ccb00b399a6e`; design and planning documentation on the feature branch do not alter runtime behaviour.

## Pinned build inputs

| Input | Baseline value | Source |
|---|---:|---|
| Plugin version | `1.4.1` | `VERSION` |
| OBS Studio | `32.2.1` | `buildspec.props` |
| ONNX Runtime | `v1.28.0` | `buildspec.props` |
| Windows SDK | `10.0.26100` | `buildspec.props` |
| CMake language level | C++20 | `CMakeLists.txt` |
| MediaPipe model | `data/models/mediapipe.onnx` | tracked model inventory |

The current `main` branch distributes ONNX models. It does not contain the `mediapipe.with_runtime_opt.ort` file named by the older roadmap, so all subsequent compatibility work uses the tracked `mediapipe.onnx` file unless a later, separately justified model-format change is approved.

## Canonical Windows build

The supported Windows build is `.github/workflows/build-windows.yml`, invoked by `.github/workflows/pr-check.yml`. It checks out `vendor/obs-studio`, `vendor/vcpkg`, and `vendor/onnxruntime`; builds OBS development files and a reduced static ONNX Runtime; configures the plugin with Ninja; builds RelWithDebInfo; installs to `build_prefix`; and packages `obs-backgroundremoval_1.4.1.dll.zip`.

The legacy `bin/setup.bat` and `bin/build.bat` are not baseline authorities. They currently refer to a `windows` CMake preset, `scripts/BuildOBS.psm1`, `scripts/BuildOnnxRuntime.psm1`, and `*_git_commit` buildspec values that are absent from `main`.

## ONNX Runtime lifecycle

| Responsibility | Current location |
|---|---|
| Per-filter runtime state | `src/ort-utils/ORTModelData.hpp` |
| `Ort::Env` creation | `background_filter_create()` in `src/background-filter.cpp`; the enhancement filter has a parallel creation path |
| Session options and EP attachment | `createOrtSession()` in `src/ort-utils/ort-session-utils.cpp` |
| `Ort::Session` construction | `createOrtSession()` in `src/ort-utils/ort-session-utils.cpp` |
| Model execution | existing model classes call `Ort::Session::Run()` through the shared session |
| Background-filter device choices | `background_filter_properties()` in `src/background-filter.cpp` |

`createOrtSession()` configures graph optimization, CPU thread counts or GPU-compatible sequential execution, attaches compile-time execution providers, creates the session, reads input/output metadata, and allocates model buffers. This is the narrow Windows ML integration seam. Model preprocessing, postprocessing, and rendering do not need a provider rewrite.

## Existing provider selection

The background-removal and enhancement filters always expose CPU. CUDA, ROCm, MIGraphX, TensorRT, and CoreML choices are controlled by compile-time definitions. Runtime provider discovery does not yet exist. Logical provider state is stored in `filter_data::useGPU`.

## Baseline validation status

Source audit: complete.

Local Linux build: not executed because the controller environment has no CMake installation and the required submodules are uninitialized. This is an environment limitation, not a passing build result.

Windows CI build: passed for tested commit `2a41a6f822375299117a448f358eb64f8082b4de`. Check CI run `32741918244` passed. PR Check run `32741919482` completed successfully with Windows job `97478151708`; the seven macOS/Linux/Arch jobs were skipped under the quota-conservation label. The sole artifact was `9526045291` (`obs-backgroundremoval_1.4.1.dll.zip`), with GitHub artifact digest `e3d4981980e8383491b4e697cf3e466dbf28b1bb5be15e6d48651d06c5691ec4`. Its extracted, non-empty DLL has SHA-256 `1122ff3576fe750a344acb209263eb8f8432ed736fc52fa5eddb9d8488090e51`.

Manual Windows OBS CPU test: passed as a bounded functional smoke test on 2026-08-24 at 16:17 using artifact `9526045291`, OBS Studio `32.2.2`, Windows build `26200`, AMD Ryzen 7 5800X3D, and AMD Radeon RX 9070 XT. The MediaPipe CPU session initialized twice, at `16:17:10.511` and `16:17:27.903`, reporting input tensor `1x144x256x3` and output tensor `1x144x256x2`; both input and output buffers were allocated on each initialization. The OBS log records successful filter destruction and recreation. The original `com.microsoft.nchwc.Conv(1)` kernel error, ONNX Runtime error code 5, and `Model is not initialized` failures are absent, and the user confirmed that the mask works.

This ruling is intentionally limited to the completed functional smoke test. The requested five-minute observation, source hide/show, and filter disable/enable sequence were not separately confirmed. They are deferred to Sprint 8 lifecycle hardening and are not represented as passed by this baseline.

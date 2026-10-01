# Windows ML AMD Support — Sprint 5 Build and Packaging Design

**Date:** 2026-08-30

**Status:** Approved for implementation planning

## Goal

Move the production Windows plugin from its reduced standalone ONNX Runtime build to the ONNX Runtime supplied by the pinned self-contained `Microsoft.Windows.AI.MachineLearning` 2.2.12 package. Produce an installable OBS plugin ZIP whose Windows ML runtime dependencies are adjacent to the plugin DLL, while preserving CPU inference and leaving Linux and macOS ONNX Runtime behaviour unchanged.

Sprint 5 changes dependency selection, build orchestration, licensing, packaging, and package verification only. It does not select a GPU execution provider in the plugin.

## Starting point

The Sprint 5 baseline is feature commit `bbd46cf7f1e3f3125dec85850534f47760b89790`, where Sprints 1–4 are complete and the production Windows plugin still links the reduced static standalone ONNX Runtime built by `.github/workflows/build-windows.yml`.

The standalone Windows ML smoke tool already downloads and links the pinned Windows ML package independently. Its CPU, MIGraphX, and DirectML evidence remains the compatibility basis for this sprint. The smoke tool must continue to build and pass after the plugin dependency changes.

## Revalidated package contract

The official self-contained package was downloaded again from the pinned NuGet URL during Sprint 5 design. Its SHA-256 is:

```text
9cb60543337e6e4eac2a95c2fcb9650a880697ae7190d15499d801c907849da3
```

This matches `windows_ml_sha256` in `buildspec.props`. The package's CMake configuration reports `WINML_VERSION` as `2.2.12` and defines:

- `WindowsML::Api`, an imported shared target for `Microsoft.Windows.AI.MachineLearning.dll`;
- `WindowsML::OnnxRuntime`, an imported shared target for `onnxruntime.dll`;
- `WindowsML::DirectML`, an interface-only target with no imported runtime DLL.

The package includes `DirectML.dll`, but its own CMake documentation requires consumers to copy that file explicitly when they use the DirectML execution provider. Sprint 5 does not use DirectML in the plugin and therefore must not copy or package `DirectML.dll`. That deployment begins in Sprint 6 together with production provider selection.

The package also contains `license.txt` and `ThirdPartyNotices.txt`. Both are required inputs to the Windows legal-notice and distributable-package paths.

## Platform dependency boundary

The root CMake build will expose one internal interface target, `OnnxRuntimeBackend`, to every production consumer that needs ONNX Runtime. Consumers link this target and do not select a platform package directly.

On Windows, the boundary will:

1. call `find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED)`;
2. require `WINML_VERSION` to equal the pinned `windows_ml_version` from `buildspec.props`;
3. require both `WindowsML::Api` and `WindowsML::OnnxRuntime` to exist;
4. link `OnnxRuntimeBackend` to those two imported targets;
5. expose the Windows ML package root and legal files to the install and legal-notice logic.

The package does not ship a CMake config-version file, so an exact version argument to `find_package` cannot enforce the pin. The explicit `WINML_VERSION` equality check is mandatory. A missing package, missing imported target, missing legal file, or version mismatch is a configuration error. Windows must never continue by searching for standalone ONNX Runtime or pkg-config as a fallback.

On Linux and macOS, the boundary retains the current standalone ONNX Runtime discovery sequence, target aliasing, headers, compile checks, and provider compile definitions. Their build workflows, reduced-operator generation, ORT build/install steps, vcpkg ORT triplets, and runtime/provider semantics remain unchanged.

The plugin target will replace its direct `onnxruntime::onnxruntime` link with `OnnxRuntimeBackend`. No inference source, model code, filter lifecycle, UI, or OBS rendering code changes in this sprint.

## Windows build integration

The canonical Windows build remains `.github/workflows/build-windows.yml`. It will use the Windows ML package already downloaded and hash-verified by `scripts/download-deps.cmake`, passing its exact `build/cmake` directory to the root plugin configuration.

The workflow will remove Windows-only standalone-ORT machinery:

- the `vendor/onnxruntime` checkout from the Windows submodule command;
- `VCPKG_ORT_TARGET_TRIPLET` and `CCACHE_ORT_DIR` from the Windows job;
- the separate vcpkg ORT dependency installation;
- ORT ccache restore, reset, save, and statistics steps;
- Windows reduced-operator generation and verification;
- standalone ORT configure, build, and install steps;
- `vcpkg_ort_installed` and `ort_installed` from the Windows plugin `CMAKE_PREFIX_PATH`;
- the now-unused `x64-windows-static-md-obs-ort` triplet.

Cross-platform assets must not be removed merely because Windows stops using them. In particular, the `vendor/onnxruntime` submodule, `onnxruntime_git_tag`, `onnxruntime_reduced_ops_config`, `src/required_operators.config`, reduced-operator tests, macOS ORT triplets, and macOS workflow steps remain because macOS continues to build standalone ORT. Linux continues to use its packaged standalone ORT.

The legacy `bin/build.bat` is not a baseline authority and is already documented as referring to absent presets/modules. Repairing or redesigning that script is outside Sprint 5; this sprint does not make new support claims for it.

## Runtime deployment and package layout

For the Windows install tree, CMake will install the runtime artifacts of `WindowsML::Api` and `WindowsML::OnnxRuntime` into the same directory as `obs-backgroundremoval.dll`:

```text
obs-backgroundremoval/
  bin/64bit/
    obs-backgroundremoval.dll
    Microsoft.Windows.AI.MachineLearning.dll
    onnxruntime.dll
  licenses/
    windows-ml-license.txt
    windows-ml-third-party-notices.txt
```

The Windows ML files must come from the exact package resolved at configure time. CMake must not locate them by filename on `PATH`, in vcpkg, or in a previous build tree.

`DirectML.dll` is prohibited from the Sprint 5 plugin install tree and ZIP. The ZIP must contain exactly one file named `onnxruntime.dll`, and it must be adjacent to the plugin DLL.

The existing Windows ML smoke artifact remains independent. It continues to use `$<TARGET_RUNTIME_DLLS:windows-ml-smoke>` and is not merged into the plugin ZIP.

## Legal notices

On Windows, the generated legal text shown by the plugin will incorporate the pinned Windows ML package's `license.txt` and `ThirdPartyNotices.txt` instead of treating the standalone `vendor/onnxruntime` checkout as the deployed runtime's legal source. The same two package files will be installed as explicit, human-readable files under `obs-backgroundremoval/licenses/`.

On Linux and macOS, the existing license collection from `vendor/onnxruntime` and dependency prefixes remains unchanged. License selection therefore follows the same platform boundary as runtime selection.

## CI package proof

After configuring, building, and installing the plugin, the Windows workflow will inspect the staging tree and completed ZIP before upload. The gate must prove all of the following:

1. `obs-backgroundremoval.dll`, `Microsoft.Windows.AI.MachineLearning.dll`, and `onnxruntime.dll` exist and are non-empty.
2. The three DLLs are in the same `obs-backgroundremoval/bin/64bit/` directory.
3. Exactly one `onnxruntime.dll` exists anywhere in the install tree and ZIP.
4. The installed `onnxruntime.dll` and `Microsoft.Windows.AI.MachineLearning.dll` hashes equal their respective source files in the pinned Windows ML package.
5. Both required Windows ML legal files exist in the install tree and ZIP and match the package inputs.
6. No `DirectML.dll` exists anywhere in the install tree or ZIP.
7. No Windows standalone-ORT build or install directory contributes to the plugin configuration.

Package verification must fail closed with an actionable message. It must inspect recursive archive entries rather than relying only on expected copy commands.

## Dependency loading policy

Sprint 5 initially relies on normal Windows resolution of dependencies adjacent to the loaded plugin DLL. `src/DelayLoad.cpp` remains uncompiled, no `/DELAYLOAD` option is added, and no `LoadLibrary`, DLL-directory mutation, or OBS-specific loader hook is introduced.

The final owner acceptance test determines whether this is sufficient in the real OBS process. If OBS cannot resolve either adjacent Windows ML DLL, implementation stops at that failure. A loading strategy must then be designed and approved separately before any hook or search-path change is added.

## Verification strategy

### Automated checks

The implementation plan must use RED/GREEN tests for the dependency boundary and package contract. At minimum, controlled fixtures must cover:

- successful Windows selection of an exact 2.2.12 package;
- clear configuration failure for a missing package, wrong package version, or missing imported target;
- unchanged standalone ONNX Runtime selection on a non-Windows fixture;
- install/archive acceptance for the two required package DLLs and legal files;
- rejection of duplicate or wrong-origin `onnxruntime.dll` files;
- rejection of `DirectML.dll` in the Sprint 5 plugin package.

Existing portable smoke-tool tests and Windows CPU smoke inference continue to pass. `Check CI`, the Windows plugin build, install, ZIP creation, and the package proof must all pass at the same exact commit.

### Cross-platform isolation review

A focused source review must compare the Linux and macOS paths before and after Sprint 5. It must confirm that their package discovery, target aliases, provider-symbol checks, build workflows, reduced-operator configuration, and install behaviour are semantically unchanged.

The quota-conservation `windows-only-ci` label may remain during Windows iteration. It does not replace the focused isolation review, and it must be removed before a later full-matrix merge or release gate.

### Owner OBS acceptance

The owner will download the plugin artifact produced by the exact reviewed commit, verify its digest, install that archive as a complete unit, and run OBS with the MediaPipe model on CPU. Acceptance requires:

- the plugin loads without a missing-DLL error;
- background removal produces a working CPU mask;
- the OBS log shows normal model/session initialization and no ONNX Runtime load or initialization failure;
- filter destruction and recreation complete without a crash;
- the tested commit, run, job, artifact identifier, artifact digest, OBS version, Windows build, and sanitized log evidence are recorded.

This is a CPU compatibility gate only. It makes no claim that the production plugin uses DirectML or MIGraphX.

## Review boundaries

The implementation should be divided into independently reviewable tasks for:

1. the platform CMake dependency boundary and its contract tests;
2. Windows runtime/legal installation and package-contract tests;
3. Windows CI removal of standalone ORT and exact-origin package verification;
4. exact-head CI, cross-platform isolation review, and owner OBS acceptance evidence.

Each task receives a task-scoped implementer and independent review. A final whole-sprint review and exact-head verification are required before Sprint 5 is complete.

## Non-goals

- Compiling the Windows provider module into the plugin.
- Selecting, registering, preparing, or downloading a GPU provider in OBS.
- Packaging `DirectML.dll`.
- Adding provider UI or provider preference persistence.
- Changing model preprocessing, postprocessing, rendering, or session fallback logic.
- Adding models or expanding production GPU claims.
- Activating `src/DelayLoad.cpp` or introducing another runtime-loading hook.
- Repairing legacy local Windows batch build scripts.
- Changing Linux or macOS ONNX Runtime behaviour.

## Completion criteria

Sprint 5 is complete only when the reviewed exact head passes repository checks, Windows smoke tests, plugin build/install, and package verification; the focused non-Windows isolation review is clean; and the owner confirms in OBS that the exact packaged plugin loads and MediaPipe CPU background removal works. Any adjacent-DLL loading failure returns the sprint to design rather than being patched with an unapproved loader mechanism.

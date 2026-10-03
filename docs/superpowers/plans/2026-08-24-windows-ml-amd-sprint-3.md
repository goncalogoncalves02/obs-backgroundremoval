# Windows ML AMD Support — Sprint 3 Implementation Plan

> **Sprint gate:** discover and deliberately prepare the Windows ML MIGraphX execution provider on the target RX 9070 XT, then produce either a ready/registered AMD GPU device or a controlled incompatibility report with an actionable reason.

## Goal

Extend the standalone Windows ML smoke tool with non-mutating provider discovery and an explicit provider-preparation command. Keep every provider acquisition action outside OBS. This sprint does not run model inference on MIGraphX; deterministic CPU/GPU inference and comparison belong to Sprint 4.

## Approved scope

The command surface added in this sprint is:

```text
windows-ml-smoke.exe --list-providers
windows-ml-smoke.exe --prepare-provider <exact-provider-name>
```

The existing CPU command remains unchanged:

```text
windows-ml-smoke.exe --provider cpu --model <path>
```

`--list-providers` must never call `WinMLEpEnsureReady`, register a provider, or trigger acquisition. `--prepare-provider` is the only command allowed to call `WinMLEpEnsureReady`; it is deliberately invoked by the user outside OBS.

## Confirmed API facts

The implementation targets the pinned `Microsoft.Windows.AI.MachineLearning` package `2.2.12`, not an assumed newer SDK surface.

- `WinMLEpCatalogCreate`, `WinMLEpCatalogEnumProviders`, and `WinMLEpCatalogFindProvider` provide catalog discovery.
- `WinMLEpInfo` exposes `name`, `version`, `packageFamilyName`, `libraryPath`, `packageRootPath`, `readyState`, and `certification`.
- Ready states are `Ready`, `NotReady`, and `NotPresent`; certification states are `Unknown`, `Certified`, and `Uncertified`.
- `WinMLEpEnsureReady` may acquire or prepare a provider and is therefore restricted to the explicit prepare command.
- Registration uses the exact discovered provider name and library path with `Ort::Env::RegisterExecutionProviderLibrary`.
- EP devices are re-enumerated with `Ort::Env::GetEpDevices()` after registration.
- In the pinned ONNX Runtime C++ header, the hardware accessor is `Ort::ConstEpDevice::Device()`, followed by `Type()`, `VendorId()`, `DeviceId()`, and `Vendor()`.
- Provider names are reported from the live catalog. The implementation must not silently rewrite a display label into an assumed registration name.

Primary references:

- Microsoft Windows AI documentation: `new-windows-ml/initialize-execution-providers.md`
- Microsoft Windows AI documentation: `new-windows-ml/register-execution-providers.md`
- Microsoft Windows AI documentation: `new-windows-ml/select-execution-providers.md`
- Pinned package header: `include/WinMLEpCatalog.h`
- Pinned package header: `include/winml/onnxruntime_cxx_api.h`

## Non-goals

- No OBS plugin linking or runtime integration.
- No provider preparation from OBS, plugin properties, filter creation, or inference.
- No MIGraphX model session or correctness/performance claim.
- No DirectML spike; that remains part of Sprint 4 after the MIGraphX result is known.
- No changes to Linux or macOS behavior.
- No claim that MIGraphX supports the RX 9070 XT unless the hardware gate proves it.

## Stable diagnostics contract

All normal output remains one `key=value` record per line. Provider and device collections use indexed keys so keys remain unique, for example:

```text
operation=list-providers
provider_count=2
provider.0.name=MIGraphXExecutionProvider
provider.0.version=...
provider.0.ready_state=ready
provider.0.certification=certified
provider.0.package_family_name=...
status=ok
```

Preparation reports the requested name, discovery result, state before and after preparation, registration result, all visible EP devices, matching device count, and a terminal status. Device diagnostics include EP name/vendor, hardware type, hardware vendor, vendor ID, and device ID.

Provider records are sorted by exact provider name and package family name. EP device records are sorted by EP name, hardware type, vendor ID, and device ID. Empty optional strings are emitted as empty values, not omitted.

Exit codes remain:

- `0`: requested operation completed successfully;
- `2`: CLI usage error;
- `3`: unsupported platform/architecture;
- `4`: model inference failure;
- `5`: provider discovery, preparation, registration, or device-visibility failure.

For a controlled incompatibility, stdout ends with `status=unavailable`, stderr contains one sanitized `error=...` line, and the process exits `5`. This is evidence, not a crash. HRESULT failures are reported as a stable hexadecimal value and a sanitized message when Windows supplies one.

## Task 1 — Extend the portable CLI contract with TDD

**Files:**

- Modify `tools/windows-ml-smoke/cli.hpp`
- Modify `tools/windows-ml-smoke/cli.cpp`
- Modify `tools/windows-ml-smoke/tests/cli-test.cpp`

1. Write failing parser tests for `--list-providers` and `--prepare-provider <name>` before changing the parser.
2. Represent the selected command explicitly; do not infer it later from unrelated optional fields.
3. Preserve the exact provider name, including case and punctuation.
4. Reject empty provider names, duplicate command options, mixed commands, model arguments attached to list/prepare, missing CPU model arguments, and unknown options.
5. Keep the existing CPU command behavior and error text stable unless the new usage string necessarily changes.
6. Run a fresh portable CLI-only configure/build/CTest cycle on Linux.

Expected local commands:

```bash
cmake -S tools/windows-ml-smoke -B /tmp/obs-br-winml-sprint3-cli \
  -DWINDOWS_ML_SMOKE_CLI_TESTS_ONLY=ON
cmake --build /tmp/obs-br-winml-sprint3-cli
ctest --test-dir /tmp/obs-br-winml-sprint3-cli --output-on-failure
```

## Task 2 — Add the OBS-independent Windows ML provider module

**Files:**

- Create `src/ort-utils/windows-ml-provider.hpp`
- Create `src/ort-utils/windows-ml-provider.cpp`
- Modify `tools/windows-ml-smoke/CMakeLists.txt`

1. Define structured provider, device, and operation-result types that do not depend on OBS logging or UI types.
2. Wrap the catalog handle with deterministic release through `WinMLEpCatalogRelease`.
3. Enumerate all catalog providers without mutating their state. Copy all required strings while the catalog/callback data is valid.
4. Map every known ready-state, certification, and hardware-device enum to stable lowercase diagnostic strings. Unknown enum values remain reportable as `unknown(<integer>)`.
5. Implement explicit prepare/register logic:
   - find the exact provider name;
   - record its initial state;
   - call `WinMLEpEnsureReady` only when it is not already ready;
   - re-read the ready state and require `Ready`;
   - retrieve and validate the library path;
   - register it with the provided `Ort::Env` under the exact discovered name;
   - re-enumerate EP devices and copy their diagnostic data;
   - identify devices whose `EpName()` exactly matches the registered provider.
6. Do not cache an environment-specific registration globally. The caller owns `Ort::Env` lifetime.
7. Do not log, terminate the process, prepare every certified provider, or select a device in this module.
8. Link the smoke target to both `WindowsML::Api` and `WindowsML::OnnxRuntime`; retain the existing runtime-DLL copy behavior.

The real API adapter is Windows-only and is accepted by compilation plus black-box Windows tests. Portable tests cover CLI state and deterministic report formatting; do not add brittle source-text assertions as a substitute for executing the adapter.

## Task 3 — Integrate discovery and preparation into the smoke tool

**Files:**

- Modify `tools/windows-ml-smoke/main.cpp`
- Create `tools/windows-ml-smoke/provider-report.hpp`
- Create `tools/windows-ml-smoke/provider-report.cpp`
- Create `tools/windows-ml-smoke/tests/provider-report-test.cpp`
- Modify `tools/windows-ml-smoke/CMakeLists.txt`

1. Write failing portable tests for deterministic indexed provider/device reports and terminal status ordering.
2. Keep report formatting separate from the Windows catalog adapter so it can be tested on the controller host.
3. Dispatch commands before touching model paths:
   - CPU inference retains the Sprint 2 flow;
   - list creates a catalog, emits a sorted snapshot, and exits without preparation or registration;
   - prepare creates an `Ort::Env`, invokes the explicit prepare/register operation, emits state/device diagnostics, and returns `0` or `5` according to the structured result.
4. A missing provider is reported as `provider_found=false`, terminal `status=unavailable`, and exit `5`; it must not call `WinMLEpEnsureReady`.
5. A successful prepare command requires at least one exact-name EP device after registration. Hardware acceptance additionally checks for a GPU device with AMD vendor ID `0x1002`; generic CI does not assert AMD hardware.
6. Sanitize all exception/error text to a single line. Preserve `status=ok` as the final stdout line on successful operations.
7. Build both portable tests in CLI-only mode. The provider-report test must not require Windows headers or libraries.

## Task 4 — Add Windows black-box coverage and a hardware-test artifact

**Files:**

- Modify `tests/WindowsMlSmoke/test_windows_ml_smoke.py`
- Modify `.github/workflows/build-windows.yml`

1. Add Windows black-box assertions for:
   - `--list-providers` succeeds, emits a unique-key indexed report, and ends in `status=ok`;
   - `--prepare-provider __obs_backgroundremoval_missing_provider__` returns `5`, reports `provider_found=false`, ends in `status=unavailable`, and cannot trigger acquisition;
   - the existing CPU inference contract still passes unchanged.
2. CI must never prepare a real provider.
3. Run list discovery before the expensive OBS/standalone-ORT build so API or runtime regressions fail early.
4. Package the built smoke executable and all adjacent runtime DLLs as a dedicated x64 artifact suitable for the user's RX 9070 XT test. Keep the existing plugin DLL artifact unchanged.
5. Use the existing `windows-only-ci` pull-request label while this sprint is under review to conserve the user's GitHub Student quota.

Required CI gate for the exact reviewed commit:

- Check CI: REUSE, gersemi, and clang-format all pass.
- PR Check: `build-windows-x64 / build` passes.
- CLI CTest and provider-report CTest pass.
- Windows black-box CPU, list, and controlled-missing-provider tests pass.
- Both plugin and smoke-tool artifacts exist and have matching GitHub/raw SHA-256 digests.

## Task 5 — RX 9070 XT acceptance and durable evidence

**Files:**

- Create `docs/windows-ml-provider-discovery.md` only after the hardware result exists.
- Add the document to `REUSE.toml` with `GPL-3.0-or-later` metadata.
- Store raw logs under ignored `.superpowers/sdd/2026-08-24-windows-ml-amd-sprint-3/`.

Run from the extracted smoke-tool artifact directory in PowerShell 7:

```powershell
& .\windows-ml-smoke.exe --list-providers 2>&1 |
  Tee-Object -FilePath .\windows-ml-providers-before.log
$listExit = $LASTEXITCODE
"list_exit_code=$listExit"

& .\windows-ml-smoke.exe --prepare-provider MIGraphXExecutionProvider 2>&1 |
  Tee-Object -FilePath .\windows-ml-migraphx-prepare.log
$prepareExit = $LASTEXITCODE
"prepare_exit_code=$prepareExit"

& .\windows-ml-smoke.exe --list-providers 2>&1 |
  Tee-Object -FilePath .\windows-ml-providers-after.log
$afterExit = $LASTEXITCODE
"after_exit_code=$afterExit"
```

If the first catalog output reports a different exact MIGraphX name, rerun the prepare command with that exact value instead of guessing. Return the three logs and exit codes.

The hardware gate accepts one of two evidence-backed outcomes:

1. **Success:** MIGraphX is ready after preparation, registration succeeds, and an exact-name GPU EP device is visible with AMD vendor ID `0x1002`.
2. **Controlled incompatibility:** the provider is absent, preparation fails, registration fails, or no matching AMD GPU device appears; exact state, HRESULT/status, and sanitized reason are captured. This outcome directs Sprint 4 toward DirectML as the primary candidate.

The durable evidence document records package/runtime versions, Windows build, GPU/driver details supplied by the user log, catalog states, registration/device result, exact artifact digest, and the resulting Sprint 4 decision. It must not claim inference correctness or performance.

## Review and commit policy

1. A fresh task-scoped implementer executes Tasks 1–4 with explicit RED/GREEN evidence.
2. A different fresh reviewer checks the complete Sprint 3 diff for safety, API correctness, no hidden acquisition path, stable diagnostics, tests, licensing, and packaging.
3. Any findings are fixed and scoped re-reviewed before push.
4. All generated C/C++ files use `GPL-3.0-or-later`; Python/CMake/workflow files use the repository-compatible `Apache-2.0` policy. Documentation is covered by `REUSE.toml` as `GPL-3.0-or-later`.
5. Every commit uses the configured user identity, a cryptographic signature, and a DCO trailer (`git commit -s -S`). No assistant attribution or co-author trailer is added.
6. Sprint 3 is not complete until exact-head CI, artifact integrity, independent review, and the RX 9070 XT hardware outcome are all recorded.

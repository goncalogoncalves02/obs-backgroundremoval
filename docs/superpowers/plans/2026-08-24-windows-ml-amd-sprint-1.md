# Windows ML AMD Sprint 1 Baseline and Isolation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Record an evidence-backed baseline of the current `main` architecture and prove that the functionally unchanged Windows plugin builds, packages, loads in OBS, and runs MediaPipe on CPU before Windows ML code is introduced.

**Architecture:** This sprint changes documentation only. It treats `.github/workflows/build-windows.yml` as the canonical Windows build because the current local batch files reference removed presets and PowerShell modules. Raw CI and OBS evidence stays under ignored `.superpowers/windows-results/sprint-1/`; the durable summary is written to `docs/windows-ml-baseline.md` after the Windows gate passes.

**Tech Stack:** Git, Markdown, GitHub Actions, PowerShell 7, Windows 11, OBS Studio 32.2.1, ONNX Runtime 1.28.0.

**Spec:** `docs/superpowers/specs/2026-08-24-windows-ml-amd-design.md`

## Global Constraints

- Baseline commit is `9772c540279cc84b8c5be5442c50ccb00b399a6e` from `main`; the feature branch may additionally contain design and planning documentation but no functional source changes.
- Preserve ONNX Runtime `v1.28.0`, OBS Studio `32.2.1`, Windows SDK `10.0.26100`, and plugin version `1.4.1` during this sprint.
- The exact baseline model is `data/models/mediapipe.onnx`; the current `main` branch does not distribute a MediaPipe `.ort` file.
- Do not modify CMake, C++, workflows, build scripts, models, Linux behaviour, or macOS behaviour.
- Do not use `bin/build.bat` or `bin/setup.bat` as proof of the baseline: they reference a missing `windows` CMake preset, missing `scripts/BuildOBS.psm1` and `scripts/BuildOnnxRuntime.psm1`, and absent `*_git_commit` properties.
- Do not push, create a pull request, add labels, publish artifacts, or alter external state. The controller owns those actions and must obtain user confirmation immediately before them.
- Do not commit the baseline summary until Windows CI and manual OBS CPU evidence have both passed.
- Each Windows-focused sprint blocks only on `Check CI`, the exact `build-windows-x64 / build` job succeeding for the tested commit, the intact Windows artifact, and its required manual Windows hardware test. During the quota-conservation window, the `windows-only-ci` pull-request label skips macOS and Linux jobs; remove it before the full-matrix merge, release, or explicitly shared cross-platform acceptance point. Without the label, the full cross-platform matrix remains enabled.
- Commits use only the user's configured Git identity and contain no assistant attribution or co-author trailer.
- Preserve all changes in the original `GPU` worktree.

---

### Task 1: Create the baseline audit and Windows evidence handoff

**Files:**

- Create: `docs/windows-ml-baseline.md`
- Create (ignored): `.superpowers/windows-results/sprint-1/windows-baseline-handoff.md`
- Read only: `buildspec.props`
- Read only: `VERSION`
- Read only: `.github/workflows/build-windows.yml`
- Read only: `.github/workflows/pr-check.yml`
- Read only: `CMakeLists.txt`
- Read only: `src/FilterData.hpp`
- Read only: `src/background-filter.cpp`
- Read only: `src/ort-utils/ORTModelData.hpp`
- Read only: `src/ort-utils/ort-session-utils.cpp`

**Interfaces:**

- Consumes: the approved design, baseline commit metadata, current tracked build files, and current inference source.
- Produces: an exact architectural audit in `docs/windows-ml-baseline.md` and a copy-paste Windows/CI procedure in the ignored handoff file.

- [ ] **Step 1: Verify the sprint starts without functional source changes**

Run:

```bash
git status --short --branch
git diff main...HEAD --name-only
git diff main...HEAD -- CMakeLists.txt buildspec.props .github src data/models
```

Expected:

- Branch is `feature/windows-ml-amd`.
- Differences from `main` are limited to `.gitignore` and files under `docs/superpowers/`.
- The final command prints no diff.

- [ ] **Step 2: Create the durable baseline audit**

Create `docs/windows-ml-baseline.md` with this content:

```markdown
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

Windows CI build: awaiting the Sprint 1 external CI gate.

Manual Windows OBS CPU test: awaiting the Sprint 1 user test.
```

- [ ] **Step 3: Verify every recorded baseline value against its source**

Run:

```bash
test "$(cat VERSION)" = "1.4.1"
rg -n '^obs_studio_git_tag=32\.2\.1$|^onnxruntime_git_tag=v1\.28\.0$|^windows_sdk_version=10\.0\.26100$' buildspec.props
test "$(rg -l '^obs_studio_git_tag=32\.2\.1$' buildspec.props | wc -l)" -eq 1
test "$(rg -l '^onnxruntime_git_tag=v1\.28\.0$' buildspec.props | wc -l)" -eq 1
test "$(rg -l '^windows_sdk_version=10\.0\.26100$' buildspec.props | wc -l)" -eq 1
test -f data/models/mediapipe.onnx
test ! -e data/models/mediapipe.with_runtime_opt.ort
rg -n 'createOrtSession|new Ort::Env|Ort::Session|InferenceDevice|std::string useGPU' src
```

Expected: all `test` commands exit zero and `rg` shows the documented lifecycle symbols.

- [ ] **Step 4: Create the ignored Windows handoff**

Create `.superpowers/windows-results/sprint-1/windows-baseline-handoff.md` with this content:

````markdown
# Sprint 1 Windows baseline handoff

Run this gate only after the controller confirms that the feature branch has been pushed, a draft pull request exists, the `upload-artifacts` label is present, and `Check CI` has passed. During the quota-conservation window, `windows-only-ci` is also present to skip non-Windows jobs and must be removed before the full-matrix gate. The per-sprint PR Check requirement is the exact `build-windows-x64 / build` job for this checkout; the overall PR Check run may remain in progress while other platform jobs continue.

## 1. Prepare the Windows checkout

Open PowerShell 7 and run from the parent directory where the repository should live:

```powershell
git clone https://github.com/goncalogoncalves02/obs-backgroundremoval.git
Set-Location obs-backgroundremoval
git fetch origin feature/windows-ml-amd
git switch --track origin/feature/windows-ml-amd

$EvidenceDir = Join-Path (Get-Location) '.superpowers\windows-results\sprint-1'
New-Item -ItemType Directory -Force -Path $EvidenceDir | Out-Null
git status --short --branch | Tee-Object -FilePath (Join-Path $EvidenceDir 'git-status.txt')
$LocalHead = (git rev-parse HEAD).Trim()
$LocalHead | Tee-Object -FilePath (Join-Path $EvidenceDir 'git-head.txt')
```

Expected: the branch is `feature/windows-ml-amd`, the worktree is clean, and `git-head.txt` records the commit tested by CI.

## 2. Download the CI artifact

Install and authenticate GitHub CLI only if `gh --version` fails:

```powershell
winget install --id GitHub.cli --exact
gh auth login
```

Then run:

```powershell
$Repository = 'goncalogoncalves02/obs-backgroundremoval'
$Runs = gh api "repos/$Repository/actions/workflows/pr-check.yml/runs?branch=feature/windows-ml-amd&per_page=100" | ConvertFrom-Json
$Run = $Runs.workflow_runs | Where-Object { $_.head_sha -eq $LocalHead } | Sort-Object created_at -Descending | Select-Object -First 1
if ($null -eq $Run) { throw "No PR Check run was found for the local checkout $LocalHead." }
$Jobs = gh api "repos/$Repository/actions/runs/$($Run.id)/jobs?per_page=100" | ConvertFrom-Json
$WindowsJob = $Jobs.jobs | Where-Object { $_.name -eq 'build-windows-x64 / build' } | Select-Object -First 1
if ($null -eq $WindowsJob) { throw 'The exact PR Check Windows job was not found.' }
if ($WindowsJob.status -ne 'completed' -or $WindowsJob.conclusion -ne 'success') { throw "The Windows job is not successful: $($WindowsJob.status)/$($WindowsJob.conclusion)." }
$RunEvidence = [ordered]@{
    run_id = $Run.id
    status = $Run.status
    conclusion = $Run.conclusion
    head_sha = $Run.head_sha
    windows_job_id = $WindowsJob.id
    windows_job_name = $WindowsJob.name
    windows_job_status = $WindowsJob.status
    windows_job_conclusion = $WindowsJob.conclusion
}
$RunEvidence | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $EvidenceDir 'pr-check-run.json')

$ArtifactDir = Join-Path $EvidenceDir 'artifact'
New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null
$Artifacts = gh api "repos/$Repository/actions/runs/$($Run.id)/artifacts?per_page=100" | ConvertFrom-Json
$Artifact = $Artifacts.artifacts | Where-Object { $_.name -eq 'obs-backgroundremoval_1.4.1.dll.zip' -and -not $_.expired } | Select-Object -First 1
if ($null -eq $Artifact) { throw 'The exact Windows plugin artifact was not found for the matching PR Check run.' }
$ZipPath = Join-Path $ArtifactDir 'obs-backgroundremoval_1.4.1.dll.zip'
$ArtifactZipEndpoint = "https://api.github.com/repos/$Repository/actions/artifacts/$($Artifact.id)/zip"
$GitHubToken = gh auth token
if ([string]::IsNullOrWhiteSpace($GitHubToken)) { throw 'GitHub CLI did not return an authentication token.' }
Invoke-WebRequest -Uri $ArtifactZipEndpoint -Headers @{ Authorization = "Bearer $GitHubToken"; Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2022-11-28' } -OutFile $ZipPath -MaximumRedirection 5
if (-not (Test-Path -LiteralPath $ZipPath) -or (Get-Item -LiteralPath $ZipPath).Length -eq 0) { throw 'The raw Windows plugin artifact archive was not downloaded.' }
Get-FileHash -Algorithm SHA256 -LiteralPath $ZipPath | Format-List | Out-File -Encoding utf8 (Join-Path $EvidenceDir 'artifact-sha256.txt')
```

Expected: `pr-check-run.json` records the matching run and a successful `build-windows-x64 / build` job, the exact non-expired artifact is downloaded as a raw archive, and the zip hash is recorded. The overall PR Check conclusion is recorded but is not a per-sprint blocking condition.

## 3. Install the baseline plugin safely

Close OBS before running:

```powershell
$PackageDir = Join-Path $EvidenceDir 'package'
if (Test-Path -LiteralPath $PackageDir) { throw "Package staging directory already exists: $PackageDir" }
New-Item -ItemType Directory -Path $PackageDir | Out-Null
Expand-Archive -LiteralPath $ZipPath -DestinationPath $PackageDir -Force

$PluginSource = Join-Path $PackageDir 'obs-backgroundremoval'
$PluginDestination = Join-Path $env:ProgramData 'obs-studio\plugins\obs-backgroundremoval'
$BackupDestination = Join-Path $EvidenceDir 'previous-plugin-backup'

if (-not (Test-Path -LiteralPath $PluginSource)) { throw "Plugin folder not found: $PluginSource" }
if (Test-Path -LiteralPath $PluginDestination) {
    if (Test-Path -LiteralPath $BackupDestination) { throw "Backup destination already exists: $BackupDestination" }
    Move-Item -LiteralPath $PluginDestination -Destination $BackupDestination
}
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $PluginDestination) | Out-Null
Copy-Item -LiteralPath $PluginSource -Destination $PluginDestination -Recurse -Force
```

The package is extracted only into a fresh staging directory. The previous plugin, when present, is moved to `.superpowers\windows-results\sprint-1\previous-plugin-backup` before replacement. A pre-existing staging directory or backup stops the procedure rather than retaining stale package files or merging plugin versions.

## 4. Run the OBS CPU acceptance test

1. Start OBS Studio.
2. Add or select a webcam source.
3. Add the `Background Removal` filter.
4. Enable Advanced settings.
5. Select `CPU` as Inference Device.
6. Select `MediaPipe` as Segmentation Model.
7. Confirm the mask updates while moving for five minutes.
8. Hide and show the source once.
9. Disable and enable the filter once.
10. Stop OBS normally.

Pass requires a continuously updating mask, no crash, no frozen mask, and no new plugin startup error.

## 5. Capture evidence

```powershell
$LatestObsLog = Get-ChildItem -LiteralPath (Join-Path $env:APPDATA 'obs-studio\logs') -Filter '*.txt' | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($null -eq $LatestObsLog) { throw 'No OBS log was found.' }
Copy-Item -LiteralPath $LatestObsLog.FullName -Destination (Join-Path $EvidenceDir 'obs-baseline-cpu.log') -Force

Get-ComputerInfo | Select-Object WindowsProductName,WindowsVersion,OsBuildNumber,OsArchitecture | Format-List | Out-File -Encoding utf8 (Join-Path $EvidenceDir 'windows-info.txt')

$Result = [ordered]@{
    result = 'pass'
    provider = 'cpu'
    model = 'MediaPipe'
    duration_minutes = 5
    mask_updated = $true
    source_hide_show = 'pass'
    filter_disable_enable = 'pass'
    crash = $false
    frozen_mask = $false
}
$Result | ConvertTo-Json | Set-Content -Encoding utf8 (Join-Path $EvidenceDir 'manual-result.json')

$ArchivePath = Join-Path (Split-Path -Parent $EvidenceDir) 'sprint-1-windows-evidence.zip'
Compress-Archive -Path (Join-Path $EvidenceDir '*') -DestinationPath $ArchivePath -Force
```

Send `.superpowers\windows-results\sprint-1-windows-evidence.zip` to the controller. If any manual check fails, change `result` to `fail`, change the corresponding field, keep the OBS log, and send the evidence without attempting speculative source changes.
````

- [ ] **Step 5: Verify documentation scope and quality**

Run:

```bash
git diff --check
test -f docs/windows-ml-baseline.md
test -f .superpowers/windows-results/sprint-1/windows-baseline-handoff.md
git check-ignore -q .superpowers/windows-results/sprint-1/windows-baseline-handoff.md
rg -n '9772c540279cc84b8c5be5442c50ccb00b399a6e|v1\.28\.0|32\.2\.1|10\.0\.26100|mediapipe\.onnx|createOrtSession' docs/windows-ml-baseline.md
git diff --name-only | sed -n '1,40p'
```

Expected:

- No whitespace errors.
- The durable audit contains every required baseline fact.
- The handoff is ignored.
- The only tracked uncommitted file is `docs/windows-ml-baseline.md`.

- [ ] **Step 6: Stop for review without committing**

Return `DONE_WITH_CONCERNS`, identifying these expected constraints:

- The controller host cannot run the Windows build.
- CMake is not installed on the controller host.
- Submodules are not initialized in the isolated worktree.
- Windows CI and manual OBS evidence are still required.

Do not stage or commit. The controller dispatches code review against the uncommitted documentation diff, then owns the external CI gate.

---

### Task 2: Close the Windows baseline gate and commit the evidence summary

**Files:**

- Modify: `docs/windows-ml-baseline.md`
- Read (ignored): `.superpowers/windows-results/sprint-1/pr-check-run.json`
- Read (ignored): `.superpowers/windows-results/sprint-1/artifact-sha256.txt`
- Read (ignored): `.superpowers/windows-results/sprint-1/git-head.txt`
- Read (ignored): `.superpowers/windows-results/sprint-1/windows-info.txt`
- Read (ignored): `.superpowers/windows-results/sprint-1/manual-result.json`
- Read (ignored): `.superpowers/windows-results/sprint-1/obs-baseline-cpu.log`

**Interfaces:**

- Consumes: a successful Windows CI run, the downloaded package hash, Windows machine metadata, and a passing manual OBS CPU result produced by Task 1's handoff.
- Produces: a committed baseline audit whose validation section names the tested commit, CI conclusion, package hash, Windows build, model, provider, test duration, lifecycle checks, and OBS log filename.

- [ ] **Step 1: Validate the returned evidence before editing tracked files**

Run from the Linux controller after the controller has confirmed `Check CI` success for the tested commit and has extracted the user-provided archive into `.superpowers/windows-results/sprint-1/`:

```bash
test "$(jq -r '.windows_job_name' .superpowers/windows-results/sprint-1/pr-check-run.json)" = "build-windows-x64 / build"
test "$(jq -r '.windows_job_status' .superpowers/windows-results/sprint-1/pr-check-run.json)" = "completed"
test "$(jq -r '.windows_job_conclusion' .superpowers/windows-results/sprint-1/pr-check-run.json)" = "success"
test "$(jq -r '.head_sha' .superpowers/windows-results/sprint-1/pr-check-run.json)" = "$(cat .superpowers/windows-results/sprint-1/git-head.txt)"
test "$(jq -r '.result' .superpowers/windows-results/sprint-1/manual-result.json)" = "pass"
test "$(jq -r '.provider' .superpowers/windows-results/sprint-1/manual-result.json)" = "cpu"
test "$(jq -r '.model' .superpowers/windows-results/sprint-1/manual-result.json)" = "MediaPipe"
test "$(jq -r '.mask_updated' .superpowers/windows-results/sprint-1/manual-result.json)" = "true"
test "$(jq -r '.crash' .superpowers/windows-results/sprint-1/manual-result.json)" = "false"
test "$(jq -r '.frozen_mask' .superpowers/windows-results/sprint-1/manual-result.json)" = "false"
test -s .superpowers/windows-results/sprint-1/artifact-sha256.txt
test -s .superpowers/windows-results/sprint-1/windows-info.txt
test -s .superpowers/windows-results/sprint-1/obs-baseline-cpu.log
```

Expected: every command exits zero. If any command fails, report `BLOCKED` with the exact missing or failing evidence and do not change the tracked audit.

- [ ] **Step 2: Replace the two pending validation paragraphs with evidence**

In `docs/windows-ml-baseline.md`, keep the source-audit and Linux-environment paragraphs. Replace the Windows CI and manual test paragraphs with a Markdown table containing:

```markdown
| Validation | Evidence |
|---|---|
| Tested commit | Exact value from `git-head.txt` |
| Windows CI | Run ID and exact `build-windows-x64 / build` job ID from `pr-check-run.json`; job conclusion `success` |
| Windows package | `obs-backgroundremoval_1.4.1.dll.zip` and exact SHA-256 from `artifact-sha256.txt` |
| Windows system | Exact product, version, build, and architecture from `windows-info.txt` |
| Inference provider | `cpu` |
| Segmentation model | `MediaPipe` using `data/models/mediapipe.onnx` |
| Continuous test | Five minutes; mask updated continuously |
| Source hide/show | Pass |
| Filter disable/enable | Pass |
| Crash or frozen mask | None observed |
| OBS log evidence | `obs-baseline-cpu.log` retained in ignored Sprint 1 evidence |
```

Use the exact evidence values in the value column; do not leave the explanatory phrases `Exact value` or `Exact product` in the resulting document.

- [ ] **Step 3: Verify the completed audit**

Run:

```bash
git diff --check
if rg -n 'awaiting|Exact value|Exact product' docs/windows-ml-baseline.md; then exit 1; fi
rg -n 'Windows CI|build-windows-x64 / build|success|SHA-256|cpu|MediaPipe|Five minutes|None observed' docs/windows-ml-baseline.md
git diff --name-only
```

Expected:

- No pending or template wording remains.
- The result markers appear.
- `docs/windows-ml-baseline.md` is the only tracked file changed by Sprint 1 execution.

- [ ] **Step 4: Commit the accepted baseline**

Run:

```bash
git add docs/windows-ml-baseline.md
git commit -m "docs(winml): record main baseline"
```

Expected: one commit authored and committed with the user's configured Git identity, with no assistant attribution or co-author trailer.

- [ ] **Step 5: Verify the Sprint 1 commit**

Run:

```bash
git status --short --branch
git show --stat --oneline HEAD
git diff HEAD^ HEAD --check
git show --format= --name-only HEAD
if git log -1 --format='%B' | rg -n 'Co-authored-by|Codex|OpenAI'; then exit 1; fi
```

Expected: clean worktree; the commit changes only `docs/windows-ml-baseline.md`; no attribution prohibited by the global constraints appears in the commit message.

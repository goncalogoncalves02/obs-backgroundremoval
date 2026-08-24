# Windows ML inference comparison on AMD Radeon RX 9070 XT

## Scope and current status

This document defines the Sprint 4 isolated inference and comparison gate. Hardware execution has not yet occurred for this exact-head artifact: every result below is explicitly **pending**. No provider is approved for plugin integration, and no latency or correctness measurement is inferred from build, discovery, readiness, registration, or session-construction evidence.

MIGraphX is the primary candidate. DirectML is an independent comparison and fallback candidate. A controlled provider or inference failure is useful evidence, but it does not authorize integration of that provider.

## Exact-head artifact gate

Run the hardware sequence only after `Check CI` and `build-windows-x64 / build` succeed at the same reviewed source SHA. Download the `windows-ml-smoke_2.2.12_x64.zip` artifact, verify its GitHub artifact digest and local SHA-256, and extract it into a new directory. The ZIP must contain exactly one root `windows-ml-smoke.exe`, exactly one root `mediapipe.onnx`, and at least one adjacent runtime DLL. The extracted model SHA-256 must equal the SHA-256 of tracked `data/models/mediapipe.onnx` at that source SHA.

Do not substitute another model, executable, dependency DLL, or artifact from a different commit. Keep raw command logs under the ignored Sprint 4 evidence directory; do not commit them.

## Exact PowerShell acceptance sequence

Run these six commands from the extracted artifact directory. Each `$LASTEXITCODE` capture is deliberately the immediate statement after its executable pipeline.

```powershell
& .\windows-ml-smoke.exe --prepare-provider MIGraphXExecutionProvider 2>&1 |
  Tee-Object -FilePath .\sprint4-migraphx-prepare.log
"prepare_exit_code=$LASTEXITCODE"

& .\windows-ml-smoke.exe --provider cpu --model .\mediapipe.onnx --iterations 100 2>&1 |
  Tee-Object -FilePath .\sprint4-cpu.log
"cpu_exit_code=$LASTEXITCODE"

& .\windows-ml-smoke.exe --provider MIGraphXExecutionProvider --model .\mediapipe.onnx --iterations 100 2>&1 |
  Tee-Object -FilePath .\sprint4-migraphx.log
"migraphx_exit_code=$LASTEXITCODE"

& .\windows-ml-smoke.exe --compare cpu MIGraphXExecutionProvider --model .\mediapipe.onnx --iterations 100 2>&1 |
  Tee-Object -FilePath .\sprint4-migraphx-compare.log
"migraphx_compare_exit_code=$LASTEXITCODE"

& .\windows-ml-smoke.exe --provider DmlExecutionProvider --model .\mediapipe.onnx --iterations 100 2>&1 |
  Tee-Object -FilePath .\sprint4-directml.log
"directml_exit_code=$LASTEXITCODE"

& .\windows-ml-smoke.exe --compare cpu DmlExecutionProvider --model .\mediapipe.onnx --iterations 100 2>&1 |
  Tee-Object -FilePath .\sprint4-directml-compare.log
"directml_compare_exit_code=$LASTEXITCODE"
```

Do not paste a comment or explanatory prose after a PowerShell pipeline continuation character (`|`). The following line must remain the intended continuation, and the exit-code capture must remain immediately after the complete pipeline.

## Acceptance criteria

A candidate passes only when all of the following are established by the captured reports:

- Its comparison command exits `0`.
- The comparison completes exactly 100 timed calls for CPU and exactly 100 timed calls for the candidate, following the tool's 10 untimed warm-up calls for each provider.
- The standalone CPU and candidate runs report `finite_output_count=73728`; the comparison reports `cpu_finite_output_count=73728` and `candidate_finite_output_count=73728`.
- The full normalized output MAE reports `normalized_mae <= 0.05`.
- Foreground channel 1, thresholded at 0.5, reports `foreground_iou >= 0.95`.
- `candidate_latency_average_ms` is strictly less than `cpu_latency_average_ms`.

An exit code other than `0`, fewer than 100 timed calls for either side, a non-finite or incorrectly sized output, a failed correctness threshold, or a candidate average latency equal to or slower than CPU is a failed gate. Provider availability alone is insufficient.

## Pending hardware and artifact evidence

All fields in this section await exact-head CI, artifact audit, and user-supplied RX 9070 XT logs. Values must remain `PENDING` until supported by exact sanitized evidence.

| Evidence item | Result | Required source |
|---|---|---|
| Reviewed source commit | `PENDING` | Exact reviewed Git SHA |
| GitHub Actions run and artifact ID | `PENDING` | Same-SHA successful Windows workflow |
| GitHub artifact digest | `PENDING` | GitHub artifact metadata |
| Downloaded ZIP SHA-256 | `PENDING` | Local hash of downloaded artifact |
| Extracted `windows-ml-smoke.exe` SHA-256 | `PENDING` | Local extracted-file hash |
| Extracted `mediapipe.onnx` SHA-256 | `PENDING` | Local extracted-file hash |
| Tracked `data/models/mediapipe.onnx` SHA-256 and identity match | `PENDING` | Same reviewed source SHA |
| Windows version and build | `PENDING` | Hardware host evidence |
| GPU name, vendor ID, and device ID | `PENDING` | Hardware host and smoke-tool evidence |
| AMD GPU driver version | `PENDING` | Hardware host evidence |
| MIGraphX readiness before and after preparation | `PENDING` | Preparation report |
| MIGraphX process activation attempted/result | `PENDING` | Inference/comparison reports |
| MIGraphX registration result | `PENDING` | Preparation/inference/comparison reports |
| MIGraphX selected EP name and device ID | `PENDING` | Inference/comparison reports |
| DirectML readiness/activation/registration state | `PENDING` | Inference/comparison reports |
| DirectML selected EP name and device ID | `PENDING` | Inference/comparison reports |
| CPU latency average / p50 / p95 | `PENDING` | 100-timed-call reports |
| MIGraphX latency average / p50 / p95 | `PENDING` | 100-timed-call reports |
| DirectML latency average / p50 / p95 | `PENDING` | 100-timed-call reports |
| MIGraphX finite counts / normalized MAE / foreground IoU | `PENDING` | MIGraphX comparison report |
| DirectML finite counts / normalized MAE / foreground IoU | `PENDING` | DirectML comparison report |
| MIGraphX and DirectML speedup/performance gate summaries | `PENDING` | Comparison reports |
| `prepare_exit_code` | `PENDING` | Immediate PowerShell capture |
| `cpu_exit_code` | `PENDING` | Immediate PowerShell capture |
| `migraphx_exit_code` | `PENDING` | Immediate PowerShell capture |
| `migraphx_compare_exit_code` | `PENDING` | Immediate PowerShell capture |
| `directml_exit_code` | `PENDING` | Immediate PowerShell capture |
| `directml_compare_exit_code` | `PENDING` | Immediate PowerShell capture |
| Sprint 5 go/no-go decision and accepted provider | `PENDING` | All Sprint 4 gates and reviews |

The Sprint 5 decision stays pending until exact-head CI and artifact integrity pass, the RX 9070 XT evidence satisfies every acceptance criterion for at least one candidate, all four Sprint 4 task reviews are clean, and the final whole-sprint review is clean.

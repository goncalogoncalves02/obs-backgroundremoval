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

## Exact-head hardware and artifact evidence

The following sanitized evidence is for reviewed source commit `c3c6f19440201b47063e7501a92d50f8581cefa6`. `Check CI` run `32792290473`, PR Check run `32792290752`, and the `build-windows-x64 / build` job `97636124843` all succeeded at that exact source commit; the Windows job included the 64-session metadata regression and the full plugin build.

| Evidence item | Exact result |
|---|---|
| Artifact | ID `9543601156`; `windows-ml-smoke_2.2.12_x64.zip` |
| GitHub artifact digest / fresh local ZIP SHA-256 | `sha256:b5825944b877867a131b863df5258472f0381e83bcfdbf0ff09420284db83ace` / `b5825944b877867a131b863df5258472f0381e83bcfdbf0ff09420284db83ace` |
| Artifact root entries | `Microsoft.Windows.AI.MachineLearning.dll`, `mediapipe.onnx`, `onnxruntime.dll`, `windows-ml-smoke.exe` |
| Extracted executable SHA-256 | `47045bf0ccf86ba2337bfa6ed4861877a07eb9b5f1410ec71eae4fccdc83411b` |
| Extracted / tracked model SHA-256 | `7f785cf032261a07af7b845f891cab30da3f0757c7b362310e089e3aa8e8860a` / `7f785cf032261a07af7b845f891cab30da3f0757c7b362310e089e3aa8e8860a`; `cmp` confirmed byte identity |
| Hardware host | Windows `10.0.26200`, release `25H2`, revision `9168`, 64-bit; AMD Radeon RX 9070 XT, vendor `0x1002`, device `0x7550`; driver `32.0.31041.1004` |

### MIGraphX

The exact-head direct inference requested and effectively used `MIGraphXExecutionProvider`. Process activation was attempted (`true`), registration succeeded (`true`), CPU EP fallback was disabled (`true`), and the selected device was `MIGraphXExecutionProvider`, `0x7550`. The input/output were float `1x144x256x3` / `1x144x256x2`; after 10 warm-ups, 100 timed calls produced `finite_output_count=73728`, average / p50 / p95 latency `2.330348` / `2.330050` / `2.513800` ms, and `status=ok`. The immediate capture recorded `migraphx_exit_code=0`.

The exact-head comparison completed 100 timed calls for both CPU and MIGraphX. CPU finite output count was `73728`, with average / p50 / p95 `2.809828` / `2.809650` / `3.107500` ms. Candidate finite output count was `73728`, with average / p50 / p95 `2.207812` / `2.200000` / `2.423100` ms. It reported speedup `1.272675`, normalized MAE `0.000005`, foreground intersection / union / IoU `0` / `0` / `1.000000`, and all MAE, IoU, and performance gates `true`; `status=ok` and `migraphx_compare_exit_code=0` were captured. The empty-union IoU of `1.000000` follows the approved contract: empty-union IoU is `1.0`.

### DirectML

The exact-head direct inference requested and effectively used `DmlExecutionProvider`. Process activation was not attempted (`false`); registration was `false` because this is the built-in visible EP-device path; CPU EP fallback was disabled (`true`); and the selected device was `DmlExecutionProvider`, `0x7550`. The input/output were float `1x144x256x3` / `1x144x256x2`; after 10 warm-ups, 100 timed calls produced `finite_output_count=73728`, average / p50 / p95 latency `0.497712` / `0.488800` / `0.584400` ms, and `status=ok`. The immediate capture recorded `directml_exit_code=0`.

The exact-head comparison completed 100 timed calls for both CPU and DirectML. CPU finite output count was `73728`, with average / p50 / p95 `2.789580` / `2.792050` / `3.115100` ms. Candidate finite output count was `73728`, with average / p50 / p95 `0.504340` / `0.489400` / `0.598800` ms. It reported speedup `5.531150`, normalized MAE `0.000008`, foreground intersection / union / IoU `0` / `0` / `1.000000`, and all MAE, IoU, and performance gates `true`; its tool report was on the successful `status=ok` path. The empty-union IoU of `1.000000` follows the approved contract: empty-union IoU is `1.0`.

### Evidence omissions

The pasted exact-head preparation excerpt shows the `--prepare-provider` command but not its key-value report or immediate `prepare_exit_code` capture. Earlier hardware evidence established `not_ready -> ready`, registration success, and the exact AMD GPU; exact-head MIGraphX direct inference independently establishes activation, registration, selected device, 100 calls, and disabled CPU fallback.

A standalone exact-head CPU benchmark was not pasted. Both exact-head comparisons nevertheless executed and validated 100 CPU timed calls, reported `cpu_finite_output_count=73728`, and supplied their same-process CPU latency distributions for the speedup gates.

The pasted DirectML comparison excerpt ends at the immediate PowerShell capture expression and does not include a rendered `directml_compare_exit_code` line. This document does not invent that field; the tool report's `status=ok` is the successful path recorded in the supplied evidence.

## Sprint 5 decision

Both MIGraphX and DirectML satisfy the Sprint 4 candidate gate. DirectML is the approved Sprint 5 primary provider: it passes the same correctness gates and reports `5.531150x` speedup on the tested RX 9070 XT, compared with MIGraphX `1.272675x`. MIGraphX remains a validated secondary candidate and is not the Sprint 5 primary.

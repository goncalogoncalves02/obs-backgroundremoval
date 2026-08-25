# Windows ML AMD project handoff

Snapshot date: 2026-08-25

This document is the durable entry point for continuing the Windows ML AMD work in a new session. Verify remote branch heads and CI before treating URLs, commit IDs, or artifacts as current.

## Repository and branch state

- Repository: `goncalogoncalves02/obs-backgroundremoval`
- Durable staging branch: `GPU`
- Implementation branch: `feature/windows-ml-amd`
- Draft pull request: <https://github.com/goncalogoncalves02/obs-backgroundremoval/pull/1>
- `main` has not received this feature.
- At this snapshot, `GPU` was based on `c305d6ec29990b022f4e93c6ed66e65c4eb5f47f` before this documentation commit.
- The tested feature head was `bbd46cf7f1e3f3125dec85850534f47760b89790`.
- Sprint 5 implementation has not started. Its architecture and acceptance flow are approved, but the formal Sprint 5 specification and implementation plan still need to be written.

## How the work is organized

Architectural work is divided into independently reviewable sprints. Each implementation task gets a task-scoped implementer and an independent review, followed by a whole-sprint review and exact-head verification. Durable plans and specifications live under `docs/superpowers/`; transient agent files live under the ignored `/.superpowers/` directory.

Read these documents in order:

1. `docs/superpowers/specs/2026-08-24-windows-ml-amd-design.md`
2. `docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md`
3. `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-1.md`
4. `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-2.md`
5. `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-3.md`
6. `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-4.md`
7. `docs/windows-ml-baseline.md`
8. `docs/windows-ml-provider-discovery.md`
9. `docs/windows-ml-inference-comparison.md`

## Completed work

### Sprint 1 — baseline and isolation

Established the baseline, platform boundaries, CI isolation, and reviewable structure for the Windows-only work.

### Sprint 2 — Windows ML CPU smoke test

Integrated Windows ML 2.2.12 into the standalone smoke tool and validated CPU inference with the MediaPipe model, without changing production plugin inference.

### Sprint 3 — provider discovery and preparation

Implemented provider discovery/preparation diagnostics and confirmed that `MIGraphXExecutionProvider` can be prepared for the AMD Radeon RX 9070 XT.

### Sprint 4 — deterministic GPU comparison

Added 100-iteration inference and CPU-versus-provider comparisons, disabled CPU fallback for explicit GPU tests, fixed a Windows ML metadata lifetime bug, and validated both MIGraphX and DirectML. Relevant feature commits include:

- `c3c6f19` — retain Windows ML metadata owners and cover 64 sequential sessions.
- `bbd46cf` — finalize the Sprint 4 documentation contract.

## Validated hardware and results

Test hardware:

- AMD Radeon RX 9070 XT
- Vendor ID `0x1002`, device ID `0x7550`
- Driver `32.0.31041.1004`
- Windows build `10.0.26200` / 25H2 revision 9168, x64

MIGraphX comparison with CPU fallback disabled:

- CPU average: 2.809828 ms
- MIGraphX average: 2.207812 ms
- Speedup: 1.272675x
- Normalized MAE: 0.000005
- Foreground IoU: 1.000000
- Accuracy and performance gates passed

DirectML comparison with CPU fallback disabled:

- CPU average: 2.789580 ms
- DirectML average: 0.504340 ms
- Speedup: 5.531150x
- Normalized MAE: 0.000008
- Foreground IoU: 1.000000
- Accuracy and performance gates passed

DirectML is therefore the approved primary provider for Sprint 5. MIGraphX remains a validated secondary provider, not the production default.

## Exact-head CI evidence

Evidence for tested feature head `bbd46cf7f1e3f3125dec85850534f47760b89790`:

- Check CI: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849461>
- PR Check, including successful Windows job: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543>
- Smoke artifact: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543/artifacts/9545182351>
  - SHA-256: `51b7fc856e36f3e6182a9e83998e034a91db25e62262cabbbd13d68e420e2a10`
- Plugin artifact: <https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/32796849543/artifacts/9545266577>
  - SHA-256: `78d359cfa4ca8d3b0adc2d985759627993745dd6fa7c38792a9512f5e5230ec9`

The plugin artifact above predates Sprint 5. It still uses the standalone Windows ONNX Runtime and must not be described as Windows ML-integrated.

## Approved Sprint 5 architecture

The goal of Sprint 5 is build and packaging integration, while preserving production CPU behavior:

- Remove the standalone Windows ONNX Runtime build immediately; there is no transitional dual-runtime state.
- Introduce a platform abstraction target in the root CMake configuration.
- On Windows, link `WindowsML::Api` and `WindowsML::OnnxRuntime` from Windows ML 2.2.12.
- On Linux and macOS, preserve the existing standalone ONNX Runtime logic and provider semantics.
- Package `Microsoft.Windows.AI.MachineLearning.dll` and `onnxruntime.dll` adjacent to the plugin DLL.
- Do not package `DirectML.dll` in Sprint 5. That belongs to Sprint 6, when production plugin inference actually selects DirectML.
- Do not add provider UI, new models, or provider session integration in Sprint 5.
- Do not activate the existing uncompiled `DelayLoad.cpp` initially. Validate normal adjacent dependency resolution inside OBS. If it fails, stop and design the loading strategy explicitly.
- Include the required Windows ML licenses and third-party notices.
- Remove Windows CI variables, caches, reduced-operator generation, configure/build/install steps, and other machinery that exists solely for the standalone ORT build.
- Fail clearly when the Windows ML package is missing; do not hide a fallback to the former runtime.
- CI must verify that the ZIP contains exactly one `onnxruntime.dll` and that it originates from the Windows ML package.

The official package was inspected during design. Its CMake targets are:

- `WindowsML::Api`: imported shared target for `Microsoft.Windows.AI.MachineLearning.dll`
- `WindowsML::OnnxRuntime`: imported shared target for `onnxruntime.dll`
- `WindowsML::DirectML`: interface target; `DirectML.dll` is not part of its runtime DLL list

Revalidate these facts against current official, version-specific documentation before implementation.

## Approved acceptance criteria

1. Windows tests and the smoke tool continue to pass.
2. The plugin and distributable ZIP compile from the exact tested head.
3. The ZIP contains the expected DLLs and licenses, with no second ONNX Runtime.
4. The owner downloads and installs that exact plugin artifact in OBS.
5. The OBS log confirms that the plugin loads and CPU background removal still works.
6. A focused review confirms that Linux and macOS behavior remains semantically unchanged.

## Exact final design message from the previous session

The following message was explicitly approved by the project owner and is preserved verbatim:

> Ótimo — fica escolhida a abstração por plataforma.
>
> ### Desenho — build, empacotamento e validação
>
> No Windows:
>
> - O workflow continua a descarregar o pacote Windows ML 2.2.12.
> - Removemos instalação `vcpkg_ort`, geração de operadores reduzidos, cache e compilação do ORT standalone.
> - O CMake recebe diretamente a pasta `build/cmake` do Windows ML.
> - A configuração falha claramente se o pacote Windows ML não estiver disponível; não existe fallback escondido para o ORT antigo.
> - O ZIP instala `obs-backgroundremoval.dll`, `onnxruntime.dll` e `Microsoft.Windows.AI.MachineLearning.dll` na mesma pasta.
> - A pipeline verifica automaticamente que existe exatamente uma `onnxruntime.dll` e que pertence ao pacote Windows ML.
>
> Validação do Sprint 5:
>
> 1. Testes e smoke tool Windows continuam a passar.
> 2. Plugin e ZIP compilam no `HEAD` exato.
> 3. O ZIP contém as DLLs e licenças esperadas, sem uma segunda runtime.
> 4. Fazes o download e instalas o plugin no OBS.
> 5. Confirmamos no log que o plugin carrega e que a máscara funciona em CPU.
> 6. Linux/macOS permanecem semanticamente inalterados; antes do sprint terminar fazemos revisão específica desse isolamento.
>
> Se o OBS não conseguir resolver as DLLs adjacentes, o Sprint para nesse ponto e desenhamos explicitamente a estratégia de carregamento — não introduzimos silenciosamente um hook frágil.
>
> Achas correto este fluxo e estes critérios de aceitação?

The owner answered yes.

## Next session

1. Refresh branch heads, PR/CI state, and the exact Windows ML 2.2.12 CMake/package contract.
2. Write the formal Sprint 5 specification under `docs/superpowers/specs/` from the approved architecture above.
3. Self-review it and obtain owner approval for any material detail not already decided.
4. Write a task-by-task Sprint 5 implementation plan under `docs/superpowers/plans/`.
5. Execute it using the established task-scoped implementation and independent review workflow.
6. Run exact-head Windows CI and package inspection.
7. Ask the owner to perform the final OBS CPU-mask acceptance test using the exact generated artifact.

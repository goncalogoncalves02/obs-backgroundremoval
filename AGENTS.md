# Project agent guidance

These instructions apply to the entire repository.

## Purpose and communication

- The active project is adding AMD GPU inference support on Windows to the OBS Background Removal plugin.
- Communicate with the project owner in Portuguese unless asked otherwise.
- Do not present an agent or assistant as the author of commits, pull requests, or release notes.
- Start every new session by reading `CONTRIBUTING.md` and `docs/windows-ml-amd-session-handoff.md`.

## Branch strategy

- `main` is the upstream integration target and must remain untouched until the Windows ML work is accepted.
- `GPU` is the owner's durable staging and documentation branch.
- Feature implementation currently lives on `feature/windows-ml-amd` and is reviewed and tested there before it is brought to `GPU` or `main`.
- Do not copy feature code to `GPU` merely to update documentation or handoff state.
- The owner has approved pushes to the project branches, including future pushes, provided they remain within the agreed task scope.

## Development workflow

- Organize architectural work as independently reviewable sprints and tasks.
- Obtain design approval before implementing a new sprint.
- Write the approved design/specification and a detailed implementation plan under `docs/superpowers/`.
- For implementation, use a fresh, task-scoped subagent when the user requests the established sprint workflow. Review each task independently after implementation and run a final whole-sprint review.
- Use test-driven development for behavior changes and systematic debugging for failures.
- Before claiming success, run proportionate verification and report the exact evidence.
- Keep scratch files and agent work under `/.superpowers/`; that directory must stay ignored. Durable plans and specifications belong under `/docs/superpowers/` and are tracked.

## Windows ML architecture invariants

- Windows uses Windows ML; Linux and macOS retain their current standalone ONNX Runtime behavior unless a separately approved design changes it.
- Provider discovery, provider preparation, and inference must not silently fall back to CPU when a GPU provider was explicitly selected.
- The first supported production model is MediaPipe segmentation.
- OBS/plugin code must not acquire or install providers. Provider acquisition is an explicit preparation/deployment concern.
- Windows packaging must contain one ONNX Runtime only. Do not mix the former standalone Windows ORT with the runtime supplied by Windows ML.
- Preserve CPU inference as the safe baseline and fallback path where the product design explicitly permits it.
- Do not add provider UI, model expansion, or runtime loading hooks outside the approved sprint scope.

## Current project record

- The durable session handoff and next-sprint decisions are in `docs/windows-ml-amd-session-handoff.md`.
- The full roadmap is `docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md`.
- Sprint plans are in `docs/superpowers/plans/2026-08-24-windows-ml-amd-sprint-*.md`.
- The approved architecture is in `docs/superpowers/specs/2026-08-24-windows-ml-amd-design.md`.
- Hardware evidence and comparisons are in `docs/windows-ml-baseline.md`, `docs/windows-ml-provider-discovery.md`, and `docs/windows-ml-inference-comparison.md`.
- Treat the handoff as a snapshot: verify branch heads, CI state, package contents, and external documentation before relying on drift-prone facts.

## Documentation lookup

- Whenever the user asks about a library, framework, SDK, API, CLI tool, or cloud service, use Context7 before answering or changing integration code.
- Resolve the library ID first, then query current documentation using the user's full question, split by concept when necessary.
- Prefer primary, official documentation and exact version-specific material. For this project, verify Windows ML and ONNX Runtime APIs against the versions pinned by the build.

## Git and authorship

- Preserve unrelated owner changes in a dirty worktree.
- All project commits must use `Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>`.
- Every commit must be signed and include DCO: use `git commit -s -S` with signing key `460B18400D17462FF3714A2BDCED30418A1C06FC`.
- Verify the signature, `Signed-off-by` trailer, author identity, tests, and worktree state before pushing.
- Never rewrite shared history or use destructive Git commands unless the owner explicitly requests it.

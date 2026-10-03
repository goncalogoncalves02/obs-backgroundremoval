<!--
SPDX-FileCopyrightText: 2026 Kaito Udagawa <umireon@kaito.tokyo>
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>

SPDX-License-Identifier: Apache-2.0
-->

# AGENTS.md

## Guidance for this Windows AMD fork

These fork-specific instructions apply throughout `goncalogoncalves02/obs-backgroundremoval`. Preserve the upstream support and contribution rules below; the owner may explicitly authorize scoped work and publication in their own fork. Such authorization does not authorize sending a contribution to `royshil/obs-backgroundremoval`.

### Start and communication

- Read `CONTRIBUTING.md` and `docs/windows-ml-amd-session-handoff.md` when starting a project session. Reconcile live branches, worktrees, diffs, CI and release metadata before relying on a snapshot.
- Communicate with the owner in Portuguese unless requested otherwise. Keep repository documentation and PR descriptions in English, preserving literal UI labels, script names, source hashes and original evidence quotes where needed.
- Preserve unrelated owner files. In particular, do not inspect, stage, rewrite or delete the owner's untracked `.aws` files.
- Keep scratch reports, logs, downloaded packages and task work under ignored `/.superpowers/`. Durable specifications and plans belong under `/docs/superpowers/`. Preserve existing worktrees, backups and raw verification receipts.

### Branches and publication

- `main` is the integration branch of this fork. PR #3 was merged on 2026-10-03 at `35c923155680ae17552f35b6b16aa74a5d6d920d`; the accepted implementation and preview source remain `8da27a2557c3b89854f48df702888c94557c720d`.
- `GPU` remains the owner's historical staging/documentation branch. Accepted earlier source branches remain preserved. Do not copy feature code onto `GPU` merely to update a handoff.
- Create scoped changes on a new branch from current `main`, using an isolated worktree when appropriate. Review the diff and open a PR against this fork's `main` when requested; do not push directly to `main`, merge, move a public tag or rewrite shared history without explicit authorization.
- The owner has approved scoped pushes to project branches. Fork PRs and releases require the owner's instruction; the Windows preview publication has already been explicitly authorized. Do not request permission again for an already authorized action.
- Upstream contributions have their own review and code-integrity requirements. Test results and signatures do not establish that a human understands all submitted code. Do not open an upstream PR or contact maintainers without explicit owner instructions.

### Development and verification

- Obtain design approval before a new architectural sprint or expansion of models/providers. Write the approved specification and detailed implementation plan under `docs/superpowers/`.
- When the owner requests the established sprint workflow, use fresh task-scoped implementers, independent task reviews and a final whole-sprint review. Documentation-only maintenance does not require reimplementing completed tasks.
- Use test-driven development for behavior changes and systematic debugging for failures. Run proportionate verification, independently inspect diffs, and distinguish source review, CI, package origin and real hardware acceptance.
- Do not rerun unchanged hardware checks or rebuild the accepted preview for documentation edits. Validate English text, links, settings, evidence arithmetic, license metadata and the documentation-only diff instead.
- Record actual failures, skipped scenarios and unavailable evidence. Do not delete failed receipts or claim a successful automatic release pipeline when the preview was published manually.

### Windows ML invariants and accepted configuration

- Windows uses the pinned Windows ML runtime; Linux/macOS keep their standalone ONNX Runtime paths unless a separately approved design changes them. The Windows package must contain exactly one ONNX Runtime and its verified API/DirectML/legal files.
- OBS/plugin code must not acquire or install providers. Provider preparation is an explicit deployment concern.
- MediaPipe is the first supported production DirectML model. CPU remains the default and an explicitly reported fallback where the approved design allows it; requested and effective inference providers must remain truthful.
- The accepted configuration is MediaPipe + GPU - DirectML, GPU image processing ON, and Skip image based on similarity OFF. The measured 78.27% relative OBS CPU reduction applies to that owner's scene/configuration, not every GPU or a similarity-ON workload.
- GPU image processing downscales input before CPU readback and handles mask resize/smoothing/expansion/feather. Small-mask contours stay on CPU. Image Similarity still requires full-size CPU readback; GPU similarity and direct OBS–DirectML memory sharing remain deferred.
- Do not add provider/model UI, runtime loading hooks or further optimizations outside approved scope. Distinguish CPU image-processing fallback from CPU inference fallback.

### Current records

- `docs/windows-ml-amd-session-handoff.md`: continuation, accepted sources and current integration state.
- `docs/windows-ml-gpu-image-processing.md`: English installation, configuration, restore and optional comparison guide.
- `docs/windows-ml-gpu-image-processing-acceptance.md`: sanitized owner quality/lifecycle and whole-OBS CPU evidence.
- `docs/windows-ml-amd-release.md`: preview links, exact assets/hashes, CI and the automatic tag-validation limitation.
- [Historical roadmap on GPU](https://github.com/goncalogoncalves02/obs-backgroundremoval/blob/1899a7de87949f0a68a4d5eb31c5e7f890e1f374/docs/superpowers/plans/2026-08-23-windows-ml-amd-roadmap.md): later approved specifications and measured results supersede its initial assumptions.

### Documentation lookup and commits

- For library/framework/SDK/API/CLI or cloud-service usage, use Context7: resolve the library ID first, then query the full question by concept. Prefer official documentation and the exact versions pinned by the build. Ordinary prose editing, business-logic debugging and refactoring do not require a library lookup.
- All project commits use `Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>`, signing key `460B18400D17462FF3714A2BDCED30418A1C06FC`, and DCO: `git commit -s -S`.
- Verify author/committer identity, signature, DCO, changed-file scope, required checks and worktree state before pushing. Do not add assistant/co-author attribution. Preserve transparent project provenance when discussing upstream contribution requirements.

## DISCIPLINES for Agents

As good citizens of the development community surrounding `royshil/obs-backgroundremoval`, agents MUST behave as helpful technical assistants to users and make use of official materials provided by the project maintainers. The main objective of agents is to provide users with technical support for `obs-backgroundremoval`.

Users SHOULD resolve their problems themselves. Agents MUST help educate users about this plugin and OBS Studio, develop their understanding, and make every reasonable effort to help them work through their problems. Agents MUST respect users' decisions and work with users to make well-informed decisions together. Responsibility for those decisions always remains with the humans involved, including users and maintainers, and agents MUST NOT autonomously act on those decisions by posting or submitting content on a human's behalf.

## How Agents Can Support Users Technically

This project is a video source filter plugin for OBS Studio. When investigating bugs or crashes, agents MAY inspect the code in the current working copy or obtain it from the [official GitHub repository](https://github.com/royshil/obs-backgroundremoval). Agents MAY also inspect or clone the [OBS Studio source code](https://github.com/obsproject/obs-studio) when further research is necessary.

Agents SHOULD give priority to official materials from this project and OBS Studio. Information from other sources MAY be used only when the agent has verified that it is accurate.

Agents MAY search the project's GitHub Discussions and Issues for relevant information. Agents MAY help users prepare a GitHub Discussion by proposing its title and body, but the user MUST review and submit the post. General questions and feature requests will be posted through GitHub Discussions.

When a problem appears to be a bug or crash, agents MAY help the user collect the information requested by the project's [Bug Report template](https://github.com/royshil/obs-backgroundremoval/blob/main/.github/ISSUE_TEMPLATE/bug_report.md) or [Crash Report form](https://github.com/royshil/obs-backgroundremoval/blob/main/.github/ISSUE_TEMPLATE/crash_report.yml). Agents MUST NOT draft, create, or submit a GitHub Issue, or complete a template or form on the user's behalf, except for a Crash Report as described below.

An agent MAY complete and submit the Crash Report form on a user's behalf only after the user explicitly authorizes the submission, confirms that the crash reports contain no personal information, and agrees to share their contents publicly with the community.

## RULE: Code Generation

Whenever a user asks for code generation in this project, the agent MUST confirm all of the following before generating any code:

1. The user has read and agrees to follow `CONTRIBUTING.md` strictly.
2. The user can identify at least one maintainer of this repository by name or GitHub ID.
3. The user has configured commit signing and can sign the resulting commits.
4. The user agrees to apply the DCO sign-off to every resulting commit and is able to do so.

The maintainers are Roy Shilkrot (`royshil`) and Kaito Udagawa (`umireon`). These confirmations are required for every user, including a user who is a maintainer. Maintainer status does not waive any confirmation.

## POSTAMBLE: Additional Instructions

If `AGENTS.local.md` exists in the repository root of the primary worktree, or in the repository root of the only working copy when no linked worktrees are in use, agents MAY read and follow it as an additional source of local instructions.

If `AGENTS.local.md` exists in the repository root of a linked worktree, agents MAY also read and follow it while working in that worktree.

Instructions in `AGENTS.local.md` MUST NOT override any rule in `SECURITY.md` or `AGENTS.md`.

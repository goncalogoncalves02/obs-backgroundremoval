<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Windows AMD DirectML preview publication

The owner requested a PR in their fork and a downloadable GitHub release on 2026-10-03, to share with friends who also have AMD GPUs. This authorizes this Windows preview publication; it does not request a merge into `main` or an upstream PR.

- Fork: `goncalogoncalves02/obs-backgroundremoval`.
- [PR #3](https://github.com/goncalogoncalves02/obs-backgroundremoval/pull/3): `feature/windows-ml-amd-gpu-image-processing` → `main`, opened for review.
- Implementation source: `8da27a2557c3b89854f48df702888c94557c720d`, preserving all previously accepted sources.
- Release tag: `1.4.1-amd-directml-preview.1`; internal plugin version remains `1.4.1`.
- [Release](https://github.com/goncalogoncalves02/obs-backgroundremoval/releases/tag/1.4.1-amd-directml-preview.1): published as a Windows prerelease at `2026-10-03T20:37:26Z`.
- [Direct ZIP download](https://github.com/goncalogoncalves02/obs-backgroundremoval/releases/download/1.4.1-amd-directml-preview.1/obs-backgroundremoval_gpu-image-processing_x64.zip).

The preview reuses the exact original verified download and binaries. No feature code, package bytes or source version is changed to prepare this publication. The chosen release is a Windows x64 prerelease for broader AMD feedback; it is not presented as the upstream stable release or as validated on every AMD GPU.

## Assets and installation

| Asset | Purpose | Bytes | SHA-256 |
| --- | --- | --- | --- |
| `obs-backgroundremoval_gpu-image-processing_x64.zip` | Exact verified plugin ZIP, installer and optional comparison script | 59255402 | `f2f474090400a0d8b6ecf8af7c996355806a331d355fb5f72b01bb6d5fda2741` |
| `LEIA-ME.md` | Portuguese installation, restore, recommended settings and optional comparison instructions | 3558 | `5a41fc78a33e4d1035506b6b8800ffb8c351767dd69d77ad9dce4fe500464215` |
| `SHA256SUMS.txt` | Checksum of the downloadable ZIP | 117 | `55c8a0065935ae947c77254f190703958ffede96e2da79a0fb949b00e288c772` |

Release ID `402678780`. Uploaded metadata, actual authenticated draft downloads and subsequent unauthenticated public downloads of all three assets were checked against their original bytes and SHA-256 values. All public downloads returned HTTP 200. The release tag resolves to the exact implementation source above. The embedded plugin ZIP retains SHA-256 `19797093a9acc24475d2d2eda7e3fded2aac8e0c7462a88d8b60bd18c7ca91bf`. The original Windows artifact is `11283076308` from [run 37146350991](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37146350991).

Extract the release ZIP into `Downloads\obs-br-gpu`, keeping the embedded plugin ZIP intact. Close OBS; run the following in administrator PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$env:USERPROFILE\Downloads\obs-br-gpu\instalar-processamento-gpu.ps1"
```

In OBS use **MediaPipe + GPU DirectML**, enable **GPU image processing**, and disable **Skip image based on similarity** for the accepted configuration. The installer preserves the actual prior plugin and receipt; `-Acao Restaurar` restores them with OBS closed. The comparison script is optional for recipients; it does not need to be run to enable the feature.

## Evidence and limits

The exact source passed [Windows build/tests 37146350991](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37146350991) and [Check CI 37146350682](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37146350682). All 104 owner-authored commits in the proposed `main..feature` range were verified for the required identity, signing key, good signature and DCO before opening PR #3.

The [owner hardware acceptance](windows-ml-gpu-image-processing-acceptance.md) reports OBS 32.2.2, RX 9070 XT, driver 32.0.31041.3013 and Windows build 26300; quality with motion, switching, resize and filter recreation passed. Whole-OBS CPU fell from 7.74% to 1.68% in the short sequential comparison with Image Similarity OFF. This does not establish a universal performance gain, long-duration livestream stability, older Windows compatibility or broader AMD hardware acceptance. The separate full-size CPU similarity path remains unchanged.

Fresh PR #3 verification was triggered at the same source: [Check CI 37151469723](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37151469723) passed; [PR Check 37151470030](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37151470030) has passed Windows build/tests (job `111286080874`), all five Linux builds and all four Debian/Ubuntu package scenario jobs. The two macOS builds remain in progress at this publication snapshot. Although the CLI added the existing `windows-only-ci` and `upload-artifacts` labels, the opening event started the full native matrix before those labels took effect. Keep that actual verification running and record its real result, without claiming that the labels retroactively limited it. The Windows prerelease does not claim macOS acceptance or permit a merge while the full PR checks remain unresolved.

## Automatic release workflow limitation

Publishing the preview created its tag and triggered [Release CD run 37152204691](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37152204691). Its `validate-release` job `111288218960` failed because the inherited workflow requires the tag to equal the tracked `VERSION` exactly: `Release tag 1.4.1-amd-directml-preview.1 does not match VERSION (1.4.1).` Its automatic build and draft-release jobs were skipped, and it did not change or upload these assets. The separate [tag Check CI 37152204428](https://github.com/goncalogoncalves02/obs-backgroundremoval/actions/runs/37152204428) passed.

The manual publication and verified public downloads are complete; this automatic validation failure remains honestly recorded. Do not delete that run, move the public tag, change the tested package or claim the automatic release pipeline passed. Supporting preview tag naming in future automatic releases is a separate workflow change, not an untested modification hidden in this release.

No merge, upstream PR, replacement of the tested binary, or stable release is performed by this publication. Worktrees, backups and all ignored verification receipts remain preserved.

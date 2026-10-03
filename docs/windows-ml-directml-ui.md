<!--
SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# DirectML in the Background Removal filter

On Windows, the existing inference-device list offers CPU and GPU - DirectML.
The list and the actual session status are visible without enabling advanced
settings. CPU remains the default, so existing scenes keep their saved choice.

DirectML currently supports the MediaPipe model. The model selector is under
advanced settings. Selecting another model with DirectML requested uses the
existing explicit CPU fallback and displays that DirectML requires MediaPipe.
No provider is downloaded or prepared by the OBS filter.

Changing the inference device recreates the session through the existing
serialized initialization path. The status below the list describes the
effective provider of the completed session. A GPU request that falls back to
CPU displays CPU, and initialization failure displays a failure message.
The OBS log retains the requested/effective providers and fallback reason.

The owner approved this bounded UI extension on 2026-10-02 and replaced the
separate CPU-only OBS acceptance step with a combined live test on this build:
use MediaPipe, confirm the mask in CPU, switch to DirectML and confirm the GPU
status and mask, then switch back to CPU. Remove and recreate the filter once
and confirm that the saved device choice works. Any CPU fallback requires
checking the corresponding session outcome in the log.

Inference runs on the GPU with DirectML. With **GPU image processing** OFF,
image preparation and mask processing retain the earlier CPU path. With it ON,
eligible MediaPipe sessions also downscale the input and process mask borders
on the GPU; small-mask contours, camera handling and other OBS work still
include CPU work. See the [GPU image-processing guide](windows-ml-gpu-image-processing.md)
for installation, status, recommended settings and the accepted whole-OBS CPU result.
Earlier measurements of
the unchanged session backend showed approximately 2.50 ms for CPU and 0.40 ms
for DirectML on the owner's RX 9070 XT. These timings measure model inference,
not total CPU utilization, OBS frame rate or this UI's live acceptance.

The MIGraphX backend remains available internally; this UI extension adds no
MIGraphX choice. Non-Windows device lists retain their existing choices.

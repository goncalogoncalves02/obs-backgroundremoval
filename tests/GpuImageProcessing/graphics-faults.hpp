// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Force-include ONLY for the native fixture's copy of gpu-input-preprocessor.cpp.
// Headers are included before overrides, so real GS declarations/imports stay intact.
#include "graphics-fault-controls.hpp"
#define gs_texrender_create gpu_test_texrender_create
#define gs_stagesurface_create gpu_test_stagesurface_create
#define gs_stagesurface_map gpu_test_stagesurface_map
#define gs_stagesurface_unmap gpu_test_stagesurface_unmap

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Force-include ONLY for native fixture copies of the GPU input/mask helpers.
// Headers are included before overrides, so real GS declarations/imports stay intact.
#include "graphics-fault-controls.hpp"
#define gs_texrender_create gpu_test_texrender_create
#define gs_stagesurface_create gpu_test_stagesurface_create
#define gs_stagesurface_map gpu_test_stagesurface_map
#define gs_stagesurface_unmap gpu_test_stagesurface_unmap

#define gs_texture_create gpu_test_texture_create
#define gs_texture_destroy gpu_test_texture_destroy
#define gs_effect_create_from_file gpu_test_effect_create_from_file
#define gs_effect_destroy gpu_test_effect_destroy
#define gs_texrender_destroy gpu_test_texrender_destroy
#define gs_texrender_begin gpu_test_texrender_begin
#define gs_texture_map gpu_test_texture_map
#define gs_texture_unmap gpu_test_texture_unmap

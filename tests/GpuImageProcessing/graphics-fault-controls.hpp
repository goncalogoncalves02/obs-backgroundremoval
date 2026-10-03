// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <graphics/graphics.h>

namespace gpu_test {
struct GraphicsCounts {
	unsigned allocations = 0;
	unsigned maps = 0;
	unsigned unmaps = 0;
};
GraphicsCounts counts();
void fail_next_staging_allocation();
void fail_next_map();
} // namespace gpu_test

gs_texrender_t *gpu_test_texrender_create(gs_color_format format, gs_zstencil_format zs);
gs_stagesurf_t *gpu_test_stagesurface_create(uint32_t width, uint32_t height, gs_color_format format);
bool gpu_test_stagesurface_map(gs_stagesurf_t *surface, uint8_t **data, uint32_t *pitch);
void gpu_test_stagesurface_unmap(gs_stagesurf_t *surface);

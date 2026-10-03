// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <graphics/graphics.h>

namespace gpu_test {
struct GraphicsCounts {
	unsigned allocations = 0;
	unsigned maps = 0;
	unsigned unmaps = 0;
	unsigned live_resources = 0;
	unsigned upload_maps = 0;
	unsigned upload_unmaps = 0;
};
GraphicsCounts counts();
void fail_next_staging_allocation();
void fail_next_map();
void fail_graphics_allocation_after(unsigned successful_calls);
void fail_next_render_begin();
void fail_next_upload_map();
} // namespace gpu_test

gs_texrender_t *gpu_test_texrender_create(gs_color_format format, gs_zstencil_format zs);
gs_stagesurf_t *gpu_test_stagesurface_create(uint32_t width, uint32_t height, gs_color_format format);
bool gpu_test_stagesurface_map(gs_stagesurf_t *surface, uint8_t **data, uint32_t *pitch);
void gpu_test_stagesurface_unmap(gs_stagesurf_t *surface);

gs_texture_t *gpu_test_texture_create(uint32_t width, uint32_t height, gs_color_format format, uint32_t levels,
				      const uint8_t **data, uint32_t flags);
void gpu_test_texture_destroy(gs_texture_t *texture);
gs_effect_t *gpu_test_effect_create_from_file(const char *path, char **error);
void gpu_test_effect_destroy(gs_effect_t *effect);
void gpu_test_texrender_destroy(gs_texrender_t *target);
bool gpu_test_texrender_begin(gs_texrender_t *target, uint32_t width, uint32_t height);
bool gpu_test_texture_map(gs_texture_t *texture, uint8_t **data, uint32_t *pitch);
void gpu_test_texture_unmap(gs_texture_t *texture);

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "graphics-fault-controls.hpp"

static gpu_test::GraphicsCounts totals;
static bool fail_allocation = false;
static bool fail_map = false;
static int allocation_countdown = -1;
static bool fail_render_begin = false;
static bool fail_upload_map = false;
static bool allocation_fails()
{
	if (allocation_countdown < 0)
		return false;
	if (allocation_countdown-- == 0)
		return true;
	return false;
}
void gpu_test::fail_graphics_allocation_after(unsigned successful_calls)
{
	allocation_countdown = static_cast<int>(successful_calls);
}
void gpu_test::fail_next_render_begin()
{
	fail_render_begin = true;
}
void gpu_test::fail_next_upload_map()
{
	fail_upload_map = true;
}

gpu_test::GraphicsCounts gpu_test::counts()
{
	return totals;
}
void gpu_test::fail_next_staging_allocation()
{
	fail_allocation = true;
}
void gpu_test::fail_next_map()
{
	fail_map = true;
}

gs_texrender_t *gpu_test_texrender_create(gs_color_format format, gs_zstencil_format zs)
{
	++totals.allocations;
	if (allocation_fails())
		return nullptr;
	auto *target = gs_texrender_create(format, zs);
	if (target)
		++totals.live_resources;
	return target;
}

gs_stagesurf_t *gpu_test_stagesurface_create(uint32_t width, uint32_t height, gs_color_format format)
{
	++totals.allocations;
	if (fail_allocation) {
		fail_allocation = false;
		return nullptr;
	}
	return gs_stagesurface_create(width, height, format);
}

bool gpu_test_stagesurface_map(gs_stagesurf_t *surface, uint8_t **data, uint32_t *pitch)
{
	if (fail_map) {
		fail_map = false;
		return false;
	}
	if (!gs_stagesurface_map(surface, data, pitch))
		return false;
	++totals.maps;
	return true;
}

void gpu_test_stagesurface_unmap(gs_stagesurf_t *surface)
{
	++totals.unmaps;
	gs_stagesurface_unmap(surface);
}

gs_texture_t *gpu_test_texture_create(uint32_t width, uint32_t height, gs_color_format format, uint32_t levels,
				      const uint8_t **data, uint32_t flags)
{
	++totals.allocations;
	if (allocation_fails())
		return nullptr;
	auto *texture = gs_texture_create(width, height, format, levels, data, flags);
	if (texture)
		++totals.live_resources;
	return texture;
}
void gpu_test_texture_destroy(gs_texture_t *texture)
{
	if (texture) {
		--totals.live_resources;
		gs_texture_destroy(texture);
	}
}
gs_effect_t *gpu_test_effect_create_from_file(const char *path, char **error)
{
	++totals.allocations;
	if (allocation_fails()) {
		if (error)
			*error = nullptr;
		return nullptr;
	}
	auto *effect = gs_effect_create_from_file(path, error);
	if (effect)
		++totals.live_resources;
	return effect;
}
void gpu_test_effect_destroy(gs_effect_t *effect)
{
	if (effect) {
		--totals.live_resources;
		gs_effect_destroy(effect);
	}
}
void gpu_test_texrender_destroy(gs_texrender_t *target)
{
	if (target) {
		--totals.live_resources;
		gs_texrender_destroy(target);
	}
}
bool gpu_test_texrender_begin(gs_texrender_t *target, uint32_t width, uint32_t height)
{
	if (fail_render_begin) {
		fail_render_begin = false;
		return false;
	}
	return gs_texrender_begin(target, width, height);
}
bool gpu_test_texture_map(gs_texture_t *texture, uint8_t **data, uint32_t *pitch)
{
	if (fail_upload_map) {
		fail_upload_map = false;
		return false;
	}
	if (!gs_texture_map(texture, data, pitch))
		return false;
	++totals.upload_maps;
	return true;
}
void gpu_test_texture_unmap(gs_texture_t *texture)
{
	++totals.upload_unmaps;
	gs_texture_unmap(texture);
}

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "graphics-fault-controls.hpp"

static gpu_test::GraphicsCounts totals;
static bool fail_allocation = false;
static bool fail_map = false;

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
	return gs_texrender_create(format, zs);
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

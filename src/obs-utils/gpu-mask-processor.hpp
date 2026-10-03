// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gpu-image-pipeline.hpp"
#include <graphics/graphics.h>
#include <array>

namespace gpu_image {

// All calls, including destruction of prepared resources, require graphics ownership.
// process returns a source-sized BGRA mask (red channel), borrowed only until the
// next process/prepare/release. The consumer must own any last-accepted display copy.
class GpuMaskProcessor {
public:
	GpuMaskProcessor() = default;
	~GpuMaskProcessor();
	GpuMaskProcessor(const GpuMaskProcessor &) = delete;
	GpuMaskProcessor &operator=(const GpuMaskProcessor &) = delete;
	bool prepare(Dimensions input, Dimensions source, const char *effect_path);
	gs_texture_t *process(const MaskPacket &packet, const MaskSettings &settings);
	void release() noexcept;
	std::string failure_reason() const;

private:
	bool fail(const char *reason) noexcept;
	bool allocate_target(gs_texrender_t *&target, Dimensions size, gs_color_format format);
	gs_texture_t *render(gs_texture_t *input, gs_texrender_t *target, Dimensions size, const char *technique);
	gs_texture_t *morphology(gs_texture_t *input, int radius, bool eroding);
	Dimensions input_{}, source_{};
	std::string effect_path_, failure_reason_;
	bool prepared_ = false;
	gs_effect_t *effect_ = nullptr;
	gs_texture_t *upload_ = nullptr;
	std::array<gs_texrender_t *, 2> small_{};
	std::array<gs_texrender_t *, 2> full_{};
	gs_texrender_t *box_sum_ = nullptr;
	gs_eparam_t *image_ = nullptr;
	gs_eparam_t *image_size_ = nullptr;
	gs_eparam_t *target_size_ = nullptr;
	gs_eparam_t *axis_ = nullptr;
	gs_eparam_t *radius_ = nullptr;
	gs_eparam_t *reciprocal_ = nullptr;
	gs_eparam_t *post_threshold_ = nullptr;
	gs_eparam_t *eroding_ = nullptr;
};
} // namespace gpu_image

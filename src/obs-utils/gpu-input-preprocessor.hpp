// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gpu-image-pipeline.hpp"
#include <graphics/graphics.h>

namespace gpu_image {

// All calls, including destruction of prepared resources, require the caller's graphics context.
// This helper samples a borrowed source texture; returned packets exclusively own CPU pixels.
class GpuInputPreprocessor {
public:
	GpuInputPreprocessor() = default;
	~GpuInputPreprocessor();
	GpuInputPreprocessor(const GpuInputPreprocessor &) = delete;
	GpuInputPreprocessor &operator=(const GpuInputPreprocessor &) = delete;

	bool prepare(const PipelineConfig &config, const char *effect_path);
	std::optional<FramePacket> capture(gs_texture_t *full_source, FrameStamp stamp, bool copy_similarity);
	void release() noexcept;
	std::string failure_reason() const;

private:
	bool fail(const char *reason) noexcept;
	PipelineConfig config_{};
	std::string effect_path_;
	std::string failure_reason_;
	bool prepared_ = false;
	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *image_ = nullptr;
	gs_texrender_t *reduced_ = nullptr;
	gs_stagesurf_t *input_stage_ = nullptr;
	gs_stagesurf_t *similarity_stage_ = nullptr;
};

} // namespace gpu_image

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu-input-preprocessor.hpp"
#include <util/bmem.h>

#include <exception>
#include <memory>

namespace gpu_image {
namespace {

class RenderScope {
public:
	explicit RenderScope(gs_texrender_t *target)
		: target_(target),
		  framebuffer_srgb_(gs_framebuffer_srgb_enabled()),
		  linear_srgb_(gs_get_linear_srgb())
	{
		gs_blend_state_push();
		gs_enable_blending(false);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		// Legacy cv::resize interpolates encoded BGRA bytes, with no alpha premultiplication.
		gs_enable_framebuffer_srgb(false);
		gs_set_linear_srgb(false);
	}
	~RenderScope()
	{
		gs_set_linear_srgb(linear_srgb_);
		gs_enable_framebuffer_srgb(framebuffer_srgb_);
		gs_blend_state_pop();
		// Pinned texrender_end restores target, viewport, projection and matrix.
		gs_texrender_end(target_);
	}
	RenderScope(const RenderScope &) = delete;
	RenderScope &operator=(const RenderScope &) = delete;

private:
	gs_texrender_t *target_;
	bool framebuffer_srgb_;
	bool linear_srgb_;
};

class MappedSurface {
public:
	explicit MappedSurface(gs_stagesurf_t *surface) : surface_(surface)
	{
		mapped = gs_stagesurface_map(surface_, &data, &pitch);
	}
	~MappedSurface()
	{
		if (mapped)
			gs_stagesurface_unmap(surface_);
	}
	MappedSurface(const MappedSurface &) = delete;
	MappedSurface &operator=(const MappedSurface &) = delete;
	bool mapped = false;
	uint8_t *data = nullptr;
	uint32_t pitch = 0;

private:
	gs_stagesurf_t *surface_;
};

bool read_pixels(gs_stagesurf_t *surface, gs_texture_t *texture, Dimensions size, cv::Mat &owned)
{
	gs_stage_texture(surface, texture);
	MappedSurface mapping(surface);
	if (!mapping.mapped || !mapping.data || static_cast<uint64_t>(mapping.pitch) < uint64_t{size.width} * 4)
		return false;
	// clone while mapped; destructor unmaps even if OpenCV allocation throws.
	owned = cv::Mat(static_cast<int>(size.height), static_cast<int>(size.width), CV_8UC4, mapping.data,
			mapping.pitch)
			.clone();
	return true;
}

} // namespace

GpuInputPreprocessor::~GpuInputPreprocessor()
{
	release();
}

bool GpuInputPreprocessor::fail(const char *reason) noexcept
{
	try {
		failure_reason_ = reason;
	} catch (...) {
		// Reporting an allocation failure must not throw a second allocation failure.
		failure_reason_.clear();
	}
	return false;
}

bool GpuInputPreprocessor::prepare(const PipelineConfig &config, const char *effect_path)
{
	if (!gs_get_context())
		return fail("GPU input preparation requires a graphics context");
	try {
		if (!evaluate_processing_request(config).eligible || config.generation == 0 || !effect_path ||
		    !*effect_path) {
			release();
			return fail("GPU input configuration is ineligible or incomplete");
		}
		if (!effect_ || effect_path_ != effect_path || config_.input != config.input) {
			release();
			char *error = nullptr;
			effect_ = gs_effect_create_from_file(effect_path, &error);
			const std::unique_ptr<char, decltype(&bfree)> error_owner(error, &bfree);
			if (!effect_) {
				release();
				return fail("Could not load the GPU input downscale effect");
			}
			image_ = gs_effect_get_param_by_name(effect_, "image");
			if (!image_ || !gs_effect_get_technique(effect_, "Draw")) {
				release();
				return fail("GPU input effect lacks the image or Draw contract");
			}
			reduced_ = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
			// Allocate the render target during preparation, not on a later stable-size frame.
			if (!reduced_ || !gs_texrender_begin(reduced_, config.input.width, config.input.height)) {
				release();
				return fail("Could not allocate the model-sized GPU render target");
			}
			gs_texrender_end(reduced_);
			gs_texrender_reset(reduced_);
			input_stage_ = gs_stagesurface_create(config.input.width, config.input.height, GS_BGRA);
			if (!input_stage_) {
				release();
				return fail("Could not allocate the model-sized staging surface");
			}
			effect_path_ = effect_path;
		}
		if (similarity_stage_ && (!config.image_similarity || config_.source != config.source)) {
			gs_stagesurface_destroy(similarity_stage_);
			similarity_stage_ = nullptr;
		}
		if (config.image_similarity && !similarity_stage_) {
			similarity_stage_ = gs_stagesurface_create(config.source.width, config.source.height, GS_BGRA);
			if (!similarity_stage_) {
				release();
				return fail("Could not allocate the full-image similarity staging surface");
			}
		}
		config_ = config;
		prepared_ = true;
		failure_reason_.clear();
		return true;
	} catch (const std::exception &) {
		release();
		return fail("GPU input preparation failed while allocating resources");
	}
}

std::optional<FramePacket> GpuInputPreprocessor::capture(gs_texture_t *full_source, FrameStamp stamp,
							 bool copy_similarity)
{
	if (!gs_get_context()) {
		fail("GPU input capture requires a graphics context");
		return std::nullopt;
	}
	try {
		if (!prepared_ || !full_source || !accept_frame(stamp, config_) ||
		    gs_texture_get_width(full_source) != config_.source.width ||
		    gs_texture_get_height(full_source) != config_.source.height ||
		    gs_get_texture_type(full_source) != GS_TEXTURE_2D ||
		    gs_texture_get_color_format(full_source) != GS_BGRA || (copy_similarity && !similarity_stage_)) {
			fail("GPU input source, stamp or similarity configuration is invalid");
			return std::nullopt;
		}
		gs_texrender_reset(reduced_);
		if (!gs_texrender_begin(reduced_, config_.input.width, config_.input.height)) {
			release();
			fail("Could not begin model-sized GPU downscale rendering");
			return std::nullopt;
		}
		bool drew = false;
		{
			RenderScope rendering(reduced_);
			gs_ortho(0.0f, static_cast<float>(config_.input.width), 0.0f,
				 static_cast<float>(config_.input.height), -100.0f, 100.0f);
			gs_effect_set_texture(image_, full_source);
			while (gs_effect_loop(effect_, "Draw")) {
				gs_draw_sprite(full_source, 0, config_.input.width, config_.input.height);
				drew = true;
			}
			gs_effect_set_texture(image_, nullptr);
		}
		if (!drew) {
			fail("GPU input downscale effect did not execute a draw pass");
			return std::nullopt;
		}
		FramePacket packet{stamp, {}, {}};
		if (!read_pixels(input_stage_, gs_texrender_get_texture(reduced_), config_.input, packet.input_bgra)) {
			fail("Could not map the model-sized GPU input readback");
			return std::nullopt;
		}
		if (copy_similarity &&
		    !read_pixels(similarity_stage_, full_source, config_.source, packet.similarity_bgra)) {
			fail("Could not map the full-image similarity readback");
			return std::nullopt;
		}
		failure_reason_.clear();
		return packet;
	} catch (const std::exception &) {
		fail("GPU input capture failed while copying owned pixels");
		return std::nullopt;
	}
}

void GpuInputPreprocessor::release() noexcept
{
	// Contract: resource destruction takes place before the caller leaves its graphics context.
	if (similarity_stage_)
		gs_stagesurface_destroy(similarity_stage_);
	if (input_stage_)
		gs_stagesurface_destroy(input_stage_);
	if (reduced_)
		gs_texrender_destroy(reduced_);
	if (effect_)
		gs_effect_destroy(effect_);
	similarity_stage_ = nullptr;
	input_stage_ = nullptr;
	reduced_ = nullptr;
	effect_ = nullptr;
	image_ = nullptr;
	prepared_ = false;
	config_ = {};
	effect_path_.clear();
}

std::string GpuInputPreprocessor::failure_reason() const
{
	return failure_reason_;
}

} // namespace gpu_image

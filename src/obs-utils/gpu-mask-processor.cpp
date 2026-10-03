// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu-mask-processor.hpp"
#include <graphics/vec2.h>
#include <util/bmem.h>
#include <cmath>
#include <cstring>
#include <limits>
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
		gs_enable_framebuffer_srgb(false);
		gs_set_linear_srgb(false);
	}
	~RenderScope()
	{
		gs_set_linear_srgb(linear_srgb_);
		gs_enable_framebuffer_srgb(framebuffer_srgb_);
		gs_blend_state_pop();
		gs_texrender_end(target_);
	}
	RenderScope(const RenderScope &) = delete;
	RenderScope &operator=(const RenderScope &) = delete;

private:
	gs_texrender_t *target_;
	bool framebuffer_srgb_, linear_srgb_;
};
class UploadMapping {
public:
	explicit UploadMapping(gs_texture_t *texture) : texture_(texture)
	{
		mapped = gs_texture_map(texture_, &data, &pitch);
	}
	~UploadMapping()
	{
		if (mapped)
			gs_texture_unmap(texture_);
	}
	UploadMapping(const UploadMapping &) = delete;
	UploadMapping &operator=(const UploadMapping &) = delete;
	bool mapped = false;
	uint8_t *data = nullptr;
	uint32_t pitch = 0;

private:
	gs_texture_t *texture_;
};
bool valid_dimensions(Dimensions size)
{
	return size.width > 0 && size.height > 0 &&
	       size.width <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
	       size.height <= static_cast<uint32_t>(std::numeric_limits<int>::max());
}
void set_size(gs_eparam_t *parameter, Dimensions size)
{
	vec2 value;
	vec2_set(&value, static_cast<float>(size.width), static_cast<float>(size.height));
	gs_effect_set_vec2(parameter, &value);
}
void set_axis(gs_eparam_t *parameter, bool horizontal)
{
	vec2 value;
	vec2_set(&value, horizontal ? 1.0f : 0.0f, horizontal ? 0.0f : 1.0f);
	gs_effect_set_vec2(parameter, &value);
}
} // namespace

GpuMaskProcessor::~GpuMaskProcessor()
{
	release();
}

bool GpuMaskProcessor::fail(const char *reason) noexcept
{
	try {
		failure_reason_ = reason;
	} catch (...) {
		failure_reason_.clear();
	}
	return false;
}

bool GpuMaskProcessor::allocate_target(gs_texrender_t *&target, Dimensions size, gs_color_format format)
{
	target = gs_texrender_create(format, GS_ZS_NONE);
	if (!target || !gs_texrender_begin(target, size.width, size.height))
		return false;
	gs_texrender_end(target);
	gs_texrender_reset(target);
	return gs_texrender_get_texture(target) != nullptr;
}

bool GpuMaskProcessor::prepare(Dimensions input, Dimensions source, const char *effect_path)
{
	if (!gs_get_context())
		return fail("GPU mask preparation requires a graphics context");
	try {
		if (!valid_dimensions(input) || !valid_dimensions(source) || !effect_path || !*effect_path) {
			release();
			return fail("GPU mask dimensions or effect path are invalid");
		}
		if (prepared_ && input_ == input && source_ == source && effect_path_ == effect_path) {
			failure_reason_.clear();
			return true;
		}
		release();
		char *error = nullptr;
		effect_ = gs_effect_create_from_file(effect_path, &error);
		const std::unique_ptr<char, decltype(&bfree)> error_owner(error, &bfree);
		if (!effect_) {
			release();
			return fail("Could not load the GPU mask processing effect");
		}
		image_ = gs_effect_get_param_by_name(effect_, "image");
		image_size_ = gs_effect_get_param_by_name(effect_, "image_size");
		target_size_ = gs_effect_get_param_by_name(effect_, "target_size");
		axis_ = gs_effect_get_param_by_name(effect_, "axis");
		radius_ = gs_effect_get_param_by_name(effect_, "radius");
		reciprocal_ = gs_effect_get_param_by_name(effect_, "reciprocal");
		post_threshold_ = gs_effect_get_param_by_name(effect_, "post_threshold");
		eroding_ = gs_effect_get_param_by_name(effect_, "eroding");
		if (!image_ || !image_size_ || !target_size_ || !axis_ || !radius_ || !reciprocal_ ||
		    !post_threshold_ || !eroding_) {
			release();
			return fail("GPU mask effect lacks required parameters");
		}
		for (const char *technique :
		     {"StackRow", "StackColumn", "Resize", "Morphology", "BoxRow", "BoxColumn"}) {
			if (!gs_effect_get_technique(effect_, technique)) {
				release();
				return fail("GPU mask effect lacks required techniques");
			}
		}
		upload_ = gs_texture_create(input.width, input.height, GS_R8, 1, nullptr, GS_DYNAMIC);
		if (!upload_) {
			release();
			return fail("Could not allocate the small mask upload texture");
		}
		for (auto &target : small_) {
			if (!allocate_target(target, input, GS_BGRA)) {
				release();
				return fail("Could not allocate model-sized mask render targets");
			}
		}
		for (auto &target : full_) {
			if (!allocate_target(target, source, GS_BGRA)) {
				release();
				return fail("Could not allocate source-sized mask render targets");
			}
		}
		if (!allocate_target(box_sum_, source, GS_R32F)) {
			release();
			return fail("Could not allocate the mask box-filter accumulator");
		}
		input_ = input;
		source_ = source;
		effect_path_ = effect_path;
		prepared_ = true;
		failure_reason_.clear();
		return true;
	} catch (const std::exception &) {
		release();
		return fail("GPU mask preparation failed while allocating resources");
	}
}

gs_texture_t *GpuMaskProcessor::render(gs_texture_t *input, gs_texrender_t *target, Dimensions size,
				       const char *technique)
{
	if (!input || !target || input == gs_texrender_get_texture(target)) {
		fail("GPU mask pass has invalid or aliased resources");
		return nullptr;
	}
	gs_texrender_reset(target);
	if (!gs_texrender_begin(target, size.width, size.height)) {
		fail("Could not begin GPU mask rendering");
		return nullptr;
	}
	bool drew = false;
	{
		RenderScope scope(target);
		gs_ortho(0.0f, static_cast<float>(size.width), 0.0f, static_cast<float>(size.height), -100.0f, 100.0f);
		set_size(image_size_, {gs_texture_get_width(input), gs_texture_get_height(input)});
		set_size(target_size_, size);
		gs_effect_set_texture(image_, input);
		while (gs_effect_loop(effect_, technique)) {
			gs_draw_sprite(input, 0, size.width, size.height);
			drew = true;
		}
		gs_effect_set_texture(image_, nullptr);
	}
	if (!drew) {
		fail("GPU mask effect did not execute a draw pass");
		return nullptr;
	}
	return gs_texrender_get_texture(target);
}

gs_texture_t *GpuMaskProcessor::morphology(gs_texture_t *input, int radius, bool eroding)
{
	if (radius == 0)
		return input;
	for (const bool horizontal : {true, false}) {
		gs_effect_set_int(radius_, radius);
		gs_effect_set_bool(eroding_, eroding);
		set_axis(axis_, horizontal);
		auto *target = input == gs_texrender_get_texture(full_[0]) ? full_[1] : full_[0];
		input = render(input, target, source_, "Morphology");
		if (!input)
			return nullptr;
	}
	return input;
}

gs_texture_t *GpuMaskProcessor::process(const MaskPacket &packet, const MaskSettings &settings)
{
	if (!gs_get_context()) {
		fail("GPU mask processing requires a graphics context");
		return nullptr;
	}
	try {
		if (!prepared_ || !packet.gpu_postprocess || packet.stamp.generation == 0 ||
		    packet.stamp.frame_id == 0 || packet.stamp.source != source_ || packet.stamp.input != input_ ||
		    packet.mask.type() != CV_8UC1 || packet.mask.cols != static_cast<int>(input_.width) ||
		    packet.mask.rows != static_cast<int>(input_.height)) {
			fail("GPU mask packet dimensions, type or route are invalid");
			return nullptr;
		}
		if (settings.enable_threshold &&
		    (!std::isfinite(settings.smooth_contour) || settings.smooth_contour < 0 ||
		     settings.smooth_contour > 1 || !std::isfinite(settings.feather) || settings.feather < 0 ||
		     settings.feather > 1 || settings.mask_expansion < -30 || settings.mask_expansion > 30)) {
			fail("GPU mask spatial settings are outside the supported range");
			return nullptr;
		}
		{
			UploadMapping upload(upload_);
			if (!upload.mapped || !upload.data || upload.pitch < input_.width) {
				fail("Could not map the model-sized mask upload");
				return nullptr;
			}
			for (uint32_t row = 0; row < input_.height; ++row)
				std::memcpy(upload.data + static_cast<size_t>(row) * upload.pitch,
					    packet.mask.ptr(static_cast<int>(row)), input_.width);
		}
		gs_texture_t *current = upload_;
		const bool smooth = settings.enable_threshold && settings.smooth_contour > 0;
		if (smooth) {
			int kernel = static_cast<int>(3 + 11 * settings.smooth_contour);
			kernel += kernel % 2 == 0 ? 1 : 0;
			const int radius = kernel / 2;
			gs_effect_set_int(radius_, radius);
			gs_effect_set_float(reciprocal_, 1.0f / static_cast<float>((radius + 1) * (radius + 1)));
			current = render(current, small_[0], input_, "StackRow");
			if (current) {
				// Ending each gs_effect_loop technique clears every effect parameter value.
				gs_effect_set_int(radius_, radius);
				gs_effect_set_float(reciprocal_,
						    1.0f / static_cast<float>((radius + 1) * (radius + 1)));
				current = render(current, small_[1], input_, "StackColumn");
			}
		}
		gs_effect_set_bool(post_threshold_, smooth);
		if (current)
			current = render(current, full_[0], source_, "Resize");
		if (current && settings.enable_threshold) {
			current = morphology(current, std::abs(settings.mask_expansion), settings.mask_expansion > 0);
			if (current && settings.feather > 0) {
				int kernel = static_cast<int>(40 * settings.feather);
				kernel += kernel % 2 == 0 ? 1 : 0;
				current = morphology(current, kernel / 3, false);
				gs_effect_set_int(radius_, kernel / 2);
				if (current)
					current = render(current, box_sum_, source_, "BoxRow");
				if (current) {
					gs_effect_set_int(radius_, kernel / 2);
					gs_effect_set_float(reciprocal_, 1.0f / static_cast<float>(kernel * kernel));
					current = render(current, full_[0], source_, "BoxColumn");
				}
			}
		}
		if (!current) {
			release();
			return nullptr;
		}
		failure_reason_.clear();
		return current;
	} catch (const std::exception &) {
		release();
		fail("GPU mask processing failed");
		return nullptr;
	}
}

void GpuMaskProcessor::release() noexcept
{
	if (upload_)
		gs_texture_destroy(upload_);
	for (auto *target : small_)
		if (target)
			gs_texrender_destroy(target);
	for (auto *target : full_)
		if (target)
			gs_texrender_destroy(target);
	if (box_sum_)
		gs_texrender_destroy(box_sum_);
	if (effect_)
		gs_effect_destroy(effect_);
	upload_ = nullptr;
	small_ = {};
	full_ = {};
	box_sum_ = nullptr;
	effect_ = nullptr;
	image_ = image_size_ = target_size_ = axis_ = radius_ = reciprocal_ = post_threshold_ = eroding_ = nullptr;
	prepared_ = false;
	input_ = {};
	source_ = {};
	effect_path_.clear();
}
std::string GpuMaskProcessor::failure_reason() const
{
	return failure_reason_;
}
} // namespace gpu_image

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <graphics/graphics.h>
#include <graphics/vec4.h>
#include <memory>
#include <iostream>
#include <stdexcept>
#include <string>

namespace gpu_test {

class ReverseSubtractState {
public:
	ReverseSubtractState()
	{
		gs_blend_state_push();
		gs_enable_blending(true);
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ONE);
		gs_blend_op(GS_BLEND_OP_REVERSE_SUBTRACT);
	}
	~ReverseSubtractState() { gs_blend_state_pop(); }
	ReverseSubtractState(const ReverseSubtractState &) = delete;
	ReverseSubtractState &operator=(const ReverseSubtractState &) = delete;
};

// There are no blend-state getters. Observe the real restored enable/factors/op
// by drawing64 over192: enabled ONE/ONE REVERSE_SUBTRACT must produce128.
inline void require_blend_probe(const std::string &effect_path, uint8_t expected)
{
	std::unique_ptr<gs_effect_t, decltype(&gs_effect_destroy)> effect(
		gs_effect_create_from_file(effect_path.c_str(), nullptr), &gs_effect_destroy);
	const uint8_t pixel[]{64, 64, 64, 255};
	const uint8_t *pixels = pixel;
	std::unique_ptr<gs_texture_t, decltype(&gs_texture_destroy)> source(
		gs_texture_create(1, 1, GS_BGRA, 1, &pixels, 0), &gs_texture_destroy);
	std::unique_ptr<gs_texrender_t, decltype(&gs_texrender_destroy)> target(
		gs_texrender_create(GS_BGRA, GS_ZS_NONE), &gs_texrender_destroy);
	std::unique_ptr<gs_stagesurf_t, decltype(&gs_stagesurface_destroy)> stage(gs_stagesurface_create(1, 1, GS_BGRA),
										  &gs_stagesurface_destroy);
	if (!effect || !source || !target || !stage)
		throw std::runtime_error("Could not create real blend-state probe resources");
	auto *image = gs_effect_get_param_by_name(effect.get(), "image");
	if (!image || !gs_effect_get_technique(effect.get(), "Draw"))
		throw std::runtime_error("Blend-state probe requires the real input downscale effect");
	if (!gs_texrender_begin(target.get(), 1, 1))
		throw std::runtime_error("Could not begin blend-state probe target");
	const bool srgb = gs_framebuffer_srgb_enabled(), linear = gs_get_linear_srgb();
	gs_enable_framebuffer_srgb(false);
	gs_set_linear_srgb(false);
	vec4 clear;
	vec4_set(&clear, 192.0f / 255.0f, 192.0f / 255.0f, 192.0f / 255.0f, 1.0f);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, 1.0f, 0.0f, 1.0f, -100.0f, 100.0f);
	gs_effect_set_texture(image, source.get());
	bool drew = false;
	while (gs_effect_loop(effect.get(), "Draw")) {
		gs_draw_sprite(source.get(), 0, 1, 1);
		drew = true;
	}
	gs_effect_set_texture(image, nullptr);
	gs_set_linear_srgb(linear);
	gs_enable_framebuffer_srgb(srgb);
	gs_texrender_end(target.get());
	if (!drew)
		throw std::runtime_error("Blend-state probe did not draw");
	gs_stage_texture(stage.get(), gs_texrender_get_texture(target.get()));
	uint8_t *data = nullptr;
	uint32_t pitch = 0;
	if (!gs_stagesurface_map(stage.get(), &data, &pitch))
		throw std::runtime_error("Could not read the real blend-state probe");
	const int actual_red = data && pitch >= 4 ? data[2] : -1;
	const bool matched = data && pitch >= 4 && data[0] == expected && data[1] == expected && data[2] == expected;
	gs_stagesurface_unmap(stage.get());
	if (!matched)
		throw std::runtime_error("Blend-state probe expected RGB" + std::to_string(expected) +
					 " but observed red=" + std::to_string(actual_red));
	std::cout << "blend-state-probe expected-red=" << static_cast<unsigned>(expected)
		  << " actual-red=" << actual_red << " PASS" << std::endl;
}

inline void require_reverse_subtract_restored(const std::string &effect_path)
{
	require_blend_probe(effect_path, 128);
}

// Narrow characterization of the reported bug using the ORIGINAL scope's calls
// on real GS state, not an old helper execution or a simulated pixel formula.
inline void require_original_overwrite_scope_corruption(const std::string &effect_path)
{
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	try {
		require_blend_probe(effect_path, 0); // inherited reverse-subtract:0*192-1*64 clamps0
	} catch (...) {
		gs_blend_state_pop();
		throw;
	}
	gs_blend_state_pop();
}

} // namespace gpu_test

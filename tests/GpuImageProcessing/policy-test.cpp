// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu-image-policy.hpp"

#include <iostream>
#include <stdexcept>

using namespace gpu_image;

static void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}

static PipelineConfig eligible_config()
{
	PipelineConfig config{};
	config.generation = 7;
	config.source = {1280, 720};
	config.input = {256, 144};
	config.requested = true;
	config.windows = true;
	config.mediapipe = true;
	config.session_ready = true;
	config.effective_directml = true;
	config.mask_every_x_frames = 1;
	return config;
}

static void default_off_and_eligibility()
{
	require(!evaluate_processing_request(PipelineConfig{}).eligible, "Default request must be off");
	auto config = eligible_config();
	auto decision = evaluate_processing_request(config);
	require(decision.eligible && decision.small_readback && !decision.full_similarity_readback,
		"Prepared DirectML MediaPipe must use small readback");
	for (auto field : {&PipelineConfig::requested, &PipelineConfig::windows, &PipelineConfig::mediapipe,
			   &PipelineConfig::session_ready, &PipelineConfig::effective_directml}) {
		auto disabled = config;
		disabled.*field = false;
		auto rejected = evaluate_processing_request(disabled);
		require(!rejected.eligible && !rejected.small_readback && !rejected.full_similarity_readback,
			"Each eligibility boundary must disable processing");
	}
	config.input = {320, 180};
	require(evaluate_processing_request(config).eligible, "Input dimensions must follow validated model metadata");
	config.input.width = 0;
	require(!evaluate_processing_request(config).eligible, "Empty model dimensions must be rejected");
	config = eligible_config();
	config.source.height = 0;
	require(!evaluate_processing_request(config).eligible, "Empty source dimensions must be rejected");
}

static void cpu_fallback_is_not_gpu_processing()
{
	auto config = eligible_config();
	config.effective_directml = false;
	require(config.requested && !evaluate_processing_request(config).eligible,
		"Saved request must survive effective CPU fallback without activating GPU processing");
}

static void stale_generation_or_dimensions_rejected()
{
	auto config = eligible_config();
	FrameStamp stamp{7, 12, {1280, 720}, {256, 144}};
	require(accept_frame(stamp, config), "Current stamp must be accepted");
	config.generation = 8;
	require(!accept_frame(stamp, config), "Old generation must be rejected");
	config.generation = 7;
	config.source = {1920, 1080};
	require(!accept_frame(stamp, config), "Old source dimensions must be rejected");
	config = eligible_config();
	config.input = {320, 180};
	require(!accept_frame(stamp, config), "Old model dimensions must be rejected");
	config = eligible_config();
	config.requested = false;
	require(!accept_frame(stamp, config), "Ineligible route must reject frames");
}

static void similarity_requires_full_readback()
{
	auto config = eligible_config();
	config.image_similarity = true;
	const auto decision = evaluate_processing_request(config);
	require(decision.eligible && decision.small_readback && decision.full_similarity_readback,
		"Similarity requires both model input and full source readback");
	require(config.image_similarity, "Policy must preserve similarity setting");
}

int main()
{
	try {
		default_off_and_eligibility();
		cpu_fallback_is_not_gpu_processing();
		stale_generation_or_dimensions_rejected();
		similarity_requires_full_readback();
		std::cout << "GPU image policy tests passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}

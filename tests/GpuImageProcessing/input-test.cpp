// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

// The temporary checkpoint compiles graphics setup without pretending to implement this interface.
#if __has_include("gpu-input-preprocessor.hpp")
#include "gpu-input-preprocessor.hpp"
#include "graphics-fault-controls.hpp"
#include <opencv2/imgproc.hpp>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace gpu_image;

static void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}

static PipelineConfig config_for(Dimensions source)
{
	PipelineConfig config{};
	config.generation = 1;
	config.source = source;
	config.input = {256, 144};
	config.requested = config.windows = config.mediapipe = config.session_ready = config.effective_directml = true;
	config.mask_every_x_frames = 1;
	return config;
}

static cv::Mat deterministic_input(Dimensions size)
{
	cv::Mat image(static_cast<int>(size.height), static_cast<int>(size.width), CV_8UC4);
	for (int y = 0; y < image.rows; ++y) {
		for (int x = 0; x < image.cols; ++x) {
			// Separate channels and nonopaque alpha detect swaps, premultiplication and flipping.
			image.at<cv::Vec4b>(y, x) = {static_cast<unsigned char>((x * 7 + y * 3) % 256),
						     static_cast<unsigned char>((x * 3 + y * 11 + 29) % 256),
						     static_cast<unsigned char>((x * 13 + y * 5 + 101) % 256),
						     static_cast<unsigned char>((x * 17 + y * 19 + 43) % 256)};
		}
	}
	return image;
}

class SourceTexture {
public:
	explicit SourceTexture(const cv::Mat &image)
	{
		const uint8_t *data = image.ptr<uint8_t>();
		texture = gs_texture_create(static_cast<uint32_t>(image.cols), static_cast<uint32_t>(image.rows),
					    GS_BGRA, 1, &data, 0);
		require(texture != nullptr, "Source texture allocation failed");
	}
	~SourceTexture() { gs_texture_destroy(texture); }
	SourceTexture(const SourceTexture &) = delete;
	SourceTexture &operator=(const SourceTexture &) = delete;
	gs_texture_t *texture = nullptr;
};

static cv::Mat read_source(gs_texture_t *texture)
{
	const auto width = gs_texture_get_width(texture), height = gs_texture_get_height(texture);
	gs_stagesurf_t *surface = gs_stagesurface_create(width, height, GS_BGRA);
	require(surface != nullptr, "Source verification staging allocation failed");
	gs_stage_texture(surface, texture);
	uint8_t *data = nullptr;
	uint32_t pitch = 0;
	if (!gs_stagesurface_map(surface, &data, &pitch)) {
		gs_stagesurface_destroy(surface);
		throw std::runtime_error("Source verification staging map failed");
	}
	cv::Mat owned;
	try {
		owned = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC4, data, pitch).clone();
	} catch (...) {
		gs_stagesurface_unmap(surface);
		gs_stagesurface_destroy(surface);
		throw;
	}
	gs_stagesurface_unmap(surface);
	gs_stagesurface_destroy(surface);
	return owned;
}

static cv::Mat prepared_input(const cv::Mat &bgra)
{
	cv::Mat rgb, resized, floats;
	cv::cvtColor(bgra, rgb, cv::COLOR_BGRA2RGB);
	cv::resize(rgb, resized, cv::Size(256, 144), 0, 0, cv::INTER_LINEAR);
	resized.convertTo(floats, CV_32F, 1.0 / 255.0);
	return floats;
}

static void reference_case(Dimensions dimensions, bool similarity, const std::string &effect)
{
	const auto input = deterministic_input(dimensions);
	SourceTexture source(input);
	auto config = config_for(dimensions);
	config.image_similarity = similarity;
	GpuInputPreprocessor processor;
	require(processor.prepare(config, effect.c_str()), "GPU input preparation failed");
	const FrameStamp stamp{config.generation, 1, dimensions, config.input};
	auto packet = processor.capture(source.texture, stamp, similarity);
	require(packet.has_value(), "GPU capture failed");
	require(packet->stamp.generation == stamp.generation && packet->stamp.frame_id == stamp.frame_id &&
			packet->stamp.source == stamp.source && packet->stamp.input == stamp.input,
		"Capture must preserve frame provenance");
	require(packet->input_bgra.type() == CV_8UC4 && packet->input_bgra.cols == 256 &&
			packet->input_bgra.rows == 144 && packet->input_bgra.total() == 256 * 144,
		"Readback must contain exactly model-sized BGRA pixels");
	require(packet->similarity_bgra.empty() == !similarity, "Similarity readback must be opt-in");
	if (similarity)
		require(packet->similarity_bgra.size() == input.size() &&
				cv::norm(packet->similarity_bgra, input, cv::NORM_INF) == 0,
			"Similarity must retain the exact full-resolution image");
	const auto reference = prepared_input(input);
	const auto actual = prepared_input(packet->input_bgra);
	const double mae = cv::norm(reference, actual, cv::NORM_L1) / static_cast<double>(reference.total() * 3);
	std::cout << "input-reference source=" << dimensions.width << 'x' << dimensions.height
		  << " normalized-mae=" << mae << " similarity=" << similarity << std::endl;
	require(mae <= 0.01, "Prepared RGB float input exceeds normalized MAE 0.01");
	cv::Mat resized;
	cv::resize(input, resized, cv::Size(256, 144), 0, 0, cv::INTER_LINEAR);
	require(cv::norm(resized, packet->input_bgra, cv::NORM_L1) / static_cast<double>(resized.total() * 4 * 255) <=
			0.01,
		"BGRA including alpha must match the CPU reference");
	require(gs_texture_get_width(source.texture) == dimensions.width &&
			gs_texture_get_height(source.texture) == dimensions.height &&
			cv::norm(read_source(source.texture), input, cv::NORM_INF) == 0,
		"Full source texture dimensions and pixels must be unchanged");
	const auto saved = packet->input_bgra.clone();
	const auto allocations = gpu_test::counts().allocations;
	for (uint64_t id = 2; id <= 8; ++id) {
		require(processor.prepare(config, effect.c_str()), "Repeated same-size prepare failed");
		require(processor.capture(source.texture, {config.generation, id, dimensions, config.input}, similarity)
				.has_value(),
			"Repeated capture failed");
	}
	require(gpu_test::counts().allocations == allocations,
		"Fixed-size capture/prepare must not reallocate GS resources");
	require(gpu_test::counts().maps == gpu_test::counts().unmaps, "All successful maps must be unmapped");
	require(cv::norm(saved, packet->input_bgra, cv::NORM_INF) == 0,
		"Packet pixels must outlive staging unmap and subsequent captures");
	processor.release();
	processor.release();
}

static void resource_failure_is_controlled(const std::string &effect)
{
	auto config = config_for({641, 359});
	GpuInputPreprocessor processor;
	require(!processor.prepare(config, "missing-input-effect.effect"), "Missing effect must fail closed");
	require(!processor.failure_reason().empty(), "Failure must include diagnostics");
	require(!processor.capture(nullptr, {1, 1, config.source, config.input}, false),
		"Unprepared capture must not publish a packet");
	require(processor.prepare(config, effect.c_str()), "Valid prepare must recover after failure");
	require(!processor.capture(nullptr, {1, 1, config.source, config.input}, false),
		"Null source must fail without a packet");
	SourceTexture source(deterministic_input(config.source));
	require(!processor.capture(source.texture, {2, 1, config.source, config.input}, false),
		"Wrong-generation capture must fail closed");
	require(!processor.capture(source.texture, {1, 1, {640, 360}, config.input}, false),
		"Dimension mismatch must fail closed");
	processor.release();
	gpu_test::fail_next_staging_allocation();
	require(!processor.prepare(config, effect.c_str()), "Staging allocation failure must fail prepare");
	require(!processor.failure_reason().empty(), "Allocation failure must include diagnostics");
	processor.release();
	require(processor.prepare(config, effect.c_str()), "Partial allocation failure must recover");
	require(processor.capture(source.texture, {1, 2, config.source, config.input}, false).has_value(),
		"Capture must work before injected map failure");
	const auto before = gpu_test::counts();
	gpu_test::fail_next_map();
	require(!processor.capture(source.texture, {1, 3, config.source, config.input}, false),
		"Injected map failure must not publish a packet");
	require(!processor.failure_reason().empty(), "Map failure must include diagnostics");
	require(gpu_test::counts().maps == before.maps && gpu_test::counts().unmaps == before.unmaps,
		"Failed mapping must not retain a pointer or unmap an unmapped surface");
	processor.release();
	processor.release();
	require(processor.prepare(config, effect.c_str()), "Map failure release/reprepare must recover");
	require(processor.capture(source.texture, {1, 4, config.source, config.input}, false).has_value(),
		"Recovered capture must succeed");
	require(gpu_test::counts().maps == gpu_test::counts().unmaps,
		"Recovery must leave every successful map unmapped");
	processor.release();
}

void run_input_cases(const std::filesystem::path &effect_root)
{
	const std::string effect = (effect_root / "input_downscale.effect").string();
	std::cout << "case reduced_readback_preserves_full_texture" << std::endl;
	reference_case({1280, 720}, false, effect);
	reference_case({1920, 1080}, false, effect);
	std::cout << "case odd_dimensions_and_bgra_alpha_match_cpu_reference" << std::endl;
	reference_case({641, 359}, false, effect);
	std::cout << "case similarity_uses_full_image" << std::endl;
	reference_case({641, 359}, true, effect);
	std::cout << "case resource_failure_is_controlled" << std::endl;
	resource_failure_is_controlled(effect);
	std::cout << "case resize_and_repeated_release" << std::endl;
	// Same object must discard/recreate only the affected resources on source resize.
	GpuInputPreprocessor processor;
	for (const auto size : {Dimensions{1280, 720}, Dimensions{1920, 1080}, Dimensions{641, 359}}) {
		const auto config = config_for(size);
		require(processor.prepare(config, effect.c_str()), "Resize prepare failed");
		SourceTexture source(deterministic_input(size));
		require(processor.capture(source.texture, {1, 1, size, config.input}, false).has_value(),
			"Resize capture failed");
	}
	processor.release();
	processor.release();
}
#endif

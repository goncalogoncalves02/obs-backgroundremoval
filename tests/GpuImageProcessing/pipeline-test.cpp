// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu-image-pipeline.hpp"
#include "mat-allocation-fault.hpp"

#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace gpu_image;

static void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}

static PipelineConfig eligible_config()
{
	PipelineConfig config{};
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

static FrameStamp stamp(const PipelineConfig &config, uint64_t id = 1)
{
	return {config.generation, id, config.source, config.input};
}

static FramePacket frame(const PipelineConfig &config, uint64_t id = 1)
{
	return {stamp(config, id), cv::Mat(144, 256, CV_8UC4, cv::Scalar(11, 22, 33, 44)),
		config.image_similarity ? cv::Mat(720, 1280, CV_8UC4, cv::Scalar(55, 66, 77, 88)) : cv::Mat{}};
}

static MaskPacket mask(const PipelineConfig &config, uint64_t id = 1)
{
	return {stamp(config, id), cv::Mat(144, 256, CV_8UC1, cv::Scalar(63)), true};
}

// Dropping clone() at either publication or readback must fail this test, including externally owned staging memory.
static void packets_own_their_pixels()
{
	ImagePipeline pipeline;
	auto config = eligible_config();
	config.image_similarity = true;
	pipeline.configure(config);
	config = pipeline.snapshot();
	std::vector<unsigned char> staging(144 * 256 * 4, 19);
	auto packet = frame(config);
	packet.input_bgra = cv::Mat(144, 256, CV_8UC4, staging.data());
	require(pipeline.publish_frame(packet), "Valid frame publication failed");
	staging.assign(staging.size(), 99);
	packet.similarity_bgra.setTo(cv::Scalar::all(99));
	auto first = pipeline.latest_frame();
	require(first && first->input_bgra.at<cv::Vec4b>(0, 0)[0] == 19 &&
			first->similarity_bgra.at<cv::Vec4b>(0, 0)[0] == 55,
		"Published pixels must not alias caller/staging buffers");
	first->input_bgra.setTo(cv::Scalar::all(88));
	first->similarity_bgra.setTo(cv::Scalar::all(88));
	auto second = pipeline.latest_frame();
	require(second && second->input_bgra.at<cv::Vec4b>(0, 0)[0] == 19 &&
			second->similarity_bgra.at<cv::Vec4b>(0, 0)[0] == 55,
		"Reader frame mutations must not affect the mailbox");
	auto output = mask(config);
	require(pipeline.publish_mask(output), "Valid mask publication failed");
	output.mask.setTo(191);
	auto read_mask = pipeline.latest_mask();
	require(read_mask && read_mask->mask.at<unsigned char>(0, 0) == 63,
		"Published mask must not alias the producer");
	read_mask->mask.setTo(127);
	require(pipeline.latest_mask()->mask.at<unsigned char>(0, 0) == 63,
		"Reader mask mutations must not affect the mailbox");
}

static void stale_generation_or_dimensions_rejected()
{
	ImagePipeline pipeline;
	pipeline.configure(eligible_config());
	auto old = pipeline.snapshot();
	require(pipeline.publish_frame(frame(old)) && pipeline.publish_mask(mask(old)), "Initial publication failed");
	auto resized = old;
	resized.source = {1920, 1080};
	const auto generation = pipeline.configure(resized);
	require(generation > old.generation, "Source resize must advance generation");
	require(!pipeline.latest_frame() && !pipeline.latest_mask(), "Resize must clear incompatible packets");
	require(!pipeline.publish_frame(frame(old)) && !pipeline.publish_mask(mask(old)),
		"Late old-generation packets must be rejected");
	require(!pipeline.latest_frame() && !pipeline.latest_mask(), "Rejected publications must not restore packets");
	auto wrong = frame(pipeline.snapshot());
	wrong.stamp.source = {1280, 720};
	require(!pipeline.publish_frame(wrong), "Wrong stamped dimensions must be rejected");
}

static void late_status_cannot_overwrite_new_generation()
{
	ImagePipeline pipeline;
	const auto previous = pipeline.configure(eligible_config());
	require(pipeline.set_processing_state(previous, ProcessingState::Active, "ready"), "Current status rejected");
	auto next = pipeline.snapshot();
	next.effective_directml = false;
	const auto current = pipeline.configure(next);
	require(current > previous, "Effective CPU fallback must advance generation");
	const auto before = pipeline.processing_snapshot();
	require(before.requested && !before.preprocess_active && !before.mask_active,
		"CPU fallback must retain requested preference without active GPU processing");
	require(!pipeline.set_processing_state(previous, ProcessingState::Active, "late"), "Old status accepted");
	require(!pipeline.set_processing_state(current, ProcessingState::Active, "wrong route"),
		"An ineligible configuration must not report active GPU processing");
	const auto after = pipeline.processing_snapshot();
	require(after.generation == current && after.state == before.state && after.reason == before.reason,
		"Rejected status changed the processing snapshot");
}

static void processing_fallback_transitions_invalidate_the_route()
{
	ImagePipeline pipeline;
	pipeline.configure(eligible_config());
	auto config = pipeline.snapshot();
	require(pipeline.publish_frame(frame(config)) && pipeline.publish_mask(mask(config)), "Publication failed");
	require(pipeline.set_processing_state(config.generation, ProcessingState::Active, "ready"),
		"Ready status rejected");
	const auto active = pipeline.processing_snapshot();
	require(active.generation == config.generation && active.preprocess_active && active.mask_active,
		"Ordinary status completion must activate both stages without changing generation");
	require(pipeline.set_processing_state(config.generation, ProcessingState::CpuProcessingFallback,
					      "stage failed"),
		"Processing fallback status rejected");
	const auto fallback = pipeline.processing_snapshot();
	require(fallback.requested && !fallback.preprocess_active && !fallback.mask_active &&
			!fallback.similarity_full_readback,
		"CPU image/mask fallback must leave both GPU stages inactive and retain saved preference");
	require(fallback.generation > config.generation && !pipeline.latest_frame() && !pipeline.latest_mask(),
		"Processing fallback transition must advance generation and clear incompatible history");
	require(!pipeline.publish_frame(frame(config)) && !pipeline.publish_mask(mask(config)) &&
			!pipeline.set_processing_state(config.generation, ProcessingState::Active, "late"),
		"Old-route publications and statuses must not revive optimized processing");
	const auto fallback_config = pipeline.snapshot();
	require(fallback_config.effective_directml && fallback_config.requested,
		"Image processing fallback must preserve completed DirectML inference and requested preference");
	require(!pipeline.publish_frame(frame(fallback_config)) && !pipeline.publish_mask(mask(fallback_config)),
		"GPU packets must not publish while the CPU image route is selected");
	require(pipeline.configure(fallback_config) == fallback.generation,
		"Unchanged configuration must preserve fallback generation");
	require(pipeline.set_processing_state(fallback.generation, ProcessingState::CpuProcessingFallback,
					      "still failed") &&
			pipeline.snapshot().generation == fallback.generation,
		"Unchanged fallback route must preserve generation");
	require(pipeline.set_processing_state(fallback.generation, ProcessingState::Active, "recovered"),
		"Current-generation recovery rejected");
	const auto recovered = pipeline.snapshot();
	require(recovered.generation > fallback.generation && !pipeline.latest_frame() && !pipeline.latest_mask(),
		"Leaving CPU image fallback must start a fresh optimized route");
	require(pipeline.publish_frame(frame(recovered)) && pipeline.publish_mask(mask(recovered)),
		"Recovered route must accept fresh packets");
	require(pipeline.set_processing_state(recovered.generation, ProcessingState::PreprocessOnly, "input ready") &&
			pipeline.snapshot().generation == recovered.generation,
		"Ordinary stage status changes must preserve current generation");
}

static void unchanged_configure_preserves_current_packets()
{
	ImagePipeline pipeline;
	pipeline.configure(eligible_config());
	auto config = pipeline.snapshot();
	require(pipeline.publish_frame(frame(config)) && pipeline.publish_mask(mask(config)), "Publication failed");
	const auto previous = config.generation;
	config.generation = 999; // Caller-supplied generation cannot replace mailbox authority.
	require(pipeline.configure(config) == previous && pipeline.snapshot().generation == previous,
		"Unchanged configure must preserve the exact authoritative generation");
	require(pipeline.latest_frame() && pipeline.latest_mask(), "Unchanged configure cleared compatible packets");
}

static void relevant_changes_advance_generation()
{
	std::vector<PipelineConfig> changes;
	auto base = eligible_config();
	auto add = [&](auto change) {
		auto config = base;
		change(config);
		changes.push_back(config);
	};
	add([](auto &c) { c.requested = false; });
	add([](auto &c) { c.windows = false; });
	add([](auto &c) { c.mediapipe = false; });
	add([](auto &c) { c.session_ready = false; });
	add([](auto &c) { c.effective_directml = false; });
	add([](auto &c) { c.source = {1920, 1080}; });
	add([](auto &c) { c.input = {320, 180}; });
	add([](auto &c) { c.image_similarity = true; });
	add([](auto &c) { c.mask_every_x_frames = 2; });
	add([](auto &c) { c.similarity_threshold = 30.0; });
	add([](auto &c) { c.mask.enable_threshold = true; });
	add([](auto &c) { c.mask.threshold = 0.5f; });
	add([](auto &c) { c.mask.temporal_smooth_factor = 0.2f; });
	add([](auto &c) { c.mask.contour_filter = 0.1f; });
	add([](auto &c) { c.mask.smooth_contour = 0.3f; });
	add([](auto &c) { c.mask.feather = 0.4f; });
	add([](auto &c) { c.mask.mask_expansion = -2; });
	for (const auto &change : changes) {
		ImagePipeline pipeline;
		const auto old = pipeline.configure(base);
		require(pipeline.publish_frame(frame(pipeline.snapshot())) &&
				pipeline.publish_mask(mask(pipeline.snapshot())),
			"Publication failed");
		require(pipeline.configure(change) > old, "Relevant setting/route change must advance generation");
		require(!pipeline.latest_frame() && !pipeline.latest_mask(),
			"Relevant change must clear packet history");
	}
}

static void malformed_or_out_of_order_packets_cannot_replace_current()
{
	ImagePipeline pipeline;
	pipeline.configure(eligible_config());
	const auto config = pipeline.snapshot();
	require(pipeline.publish_frame(frame(config, 5)) && pipeline.publish_mask(mask(config, 5)),
		"Publication failed");
	require(!pipeline.publish_frame(frame(config, 4)) && !pipeline.publish_mask(mask(config, 4)),
		"Older frame IDs must not replace newer packets");
	auto bad_frame = frame(config, 6);
	bad_frame.input_bgra = cv::Mat(144, 256, CV_8UC3);
	require(!pipeline.publish_frame(bad_frame), "Wrong BGRA type accepted");
	bad_frame = frame(config, 6);
	bad_frame.input_bgra = cv::Mat(10, 10, CV_8UC4);
	require(!pipeline.publish_frame(bad_frame), "Wrong actual input dimensions accepted");
	auto bad_mask = mask(config, 6);
	bad_mask.mask = cv::Mat(720, 1280, CV_8UC1);
	require(!pipeline.publish_mask(bad_mask), "GPU Stage2 must receive input-sized prepared pixels");
	bad_mask = mask(config, 6);
	bad_mask.mask = cv::Mat(144, 256, CV_32FC1);
	require(!pipeline.publish_mask(bad_mask), "Float mask must not replace uint8 prepared pixels");
	require(pipeline.latest_frame()->stamp.frame_id == 5 && pipeline.latest_mask()->stamp.frame_id == 5,
		"Rejected publications changed current packets");
}

// GPU packets feed shader Stage2; legacy packets retain their existing threshold-dependent compositing size.
static void mask_packet_dimensions_preserve_stage2_and_legacy_semantics()
{
	ImagePipeline pipeline;
	auto config = eligible_config();
	pipeline.configure(config);
	config = pipeline.snapshot();
	require(pipeline.publish_mask(mask(config)), "GPU prepared input-sized uint8 mask rejected");
	auto legacy = mask(config, 2);
	legacy.gpu_postprocess = false;
	require(pipeline.publish_mask(legacy), "Threshold-disabled legacy input-sized uint8 mask rejected");
	auto wrong = legacy;
	wrong.stamp.frame_id = 3;
	wrong.mask = cv::Mat(720, 1280, CV_8UC1);
	require(!pipeline.publish_mask(wrong), "Threshold-disabled legacy source-sized mask accepted");
	require(pipeline.latest_mask()->stamp.frame_id == 2 && !pipeline.latest_mask()->gpu_postprocess,
		"Invalid legacy dimensions changed current packet");

	config.mask.enable_threshold = true;
	pipeline.configure(config);
	config = pipeline.snapshot();
	require(pipeline.publish_mask(mask(config)), "Threshold-enabled GPU prepared input-sized uint8 mask rejected");
	legacy = mask(config, 2);
	legacy.gpu_postprocess = false;
	require(!pipeline.publish_mask(legacy), "Threshold-enabled legacy input-sized finished mask accepted");
	legacy.mask = cv::Mat(720, 1280, CV_8UC1, cv::Scalar(63));
	require(pipeline.publish_mask(legacy), "Threshold-enabled legacy source-sized uint8 mask rejected");
	legacy.mask.setTo(191);
	auto first = pipeline.latest_mask();
	require(first && first->mask.at<unsigned char>(0, 0) == 63, "Legacy mask aliases producer buffer");
	first->mask.setTo(127);
	require(pipeline.latest_mask()->mask.at<unsigned char>(0, 0) == 63, "Legacy mask aliases reader buffer");
}

static void similarity_requires_full_readback()
{
	ImagePipeline pipeline;
	auto config = eligible_config();
	config.image_similarity = true;
	pipeline.configure(config);
	config = pipeline.snapshot();
	auto missing = frame(config);
	missing.similarity_bgra.release();
	require(!pipeline.publish_frame(missing), "Similarity cannot use missing full-resolution pixels");
	auto small = frame(config);
	small.similarity_bgra = cv::Mat(144, 256, CV_8UC4);
	require(!pipeline.publish_frame(small), "Similarity cannot use reduced pixels");
	require(pipeline.publish_frame(frame(config)), "Full-resolution similarity frame rejected");
	require(pipeline.set_processing_state(config.generation, ProcessingState::PreprocessOnly, "ready"),
		"Preprocess status rejected");
	const auto status = pipeline.processing_snapshot();
	require(status.preprocess_active && !status.mask_active && status.similarity_full_readback,
		"Preprocess-only similarity status must report compatibility readback");
	require(pipeline.snapshot().image_similarity, "Mailbox must preserve similarity preference");
}

static void invalidation_is_terminal_for_its_generation()
{
	ImagePipeline pipeline;
	pipeline.configure(eligible_config());
	const auto old = pipeline.snapshot();
	require(pipeline.publish_frame(frame(old)) && pipeline.publish_mask(mask(old)), "Publication failed");
	pipeline.invalidate();
	require(!pipeline.publish_frame(frame(old)) && !pipeline.publish_mask(mask(old)) &&
			!pipeline.set_processing_state(old.generation, ProcessingState::Active, "late"),
		"Invalidated generation must reject packets and statuses");
	require(!pipeline.latest_frame() && !pipeline.latest_mask(), "Invalidation must clear packets");
	const auto status = pipeline.processing_snapshot();
	require(!status.preprocess_active && !status.mask_active, "Invalidation left active processing");
	pipeline.configure(old);
	require(!pipeline.publish_frame(frame(pipeline.snapshot())), "Unchanged configure revived invalid generation");
	auto next = old;
	next.image_similarity = true;
	require(pipeline.configure(next) > old.generation, "Changed configuration must begin a fresh generation");
	require(pipeline.publish_frame(frame(pipeline.snapshot())), "Fresh generation did not accept packets");
}

static void concurrent_publications_remain_owned_and_current()
{
	ImagePipeline pipeline;
	pipeline.configure(eligible_config());
	const auto old = pipeline.snapshot();
	bool unexpected = false;
	std::thread producer([&] {
		for (uint64_t id = 1; id <= 40; ++id)
			pipeline.publish_frame(frame(old, id));
	});
	for (int i = 0; i < 40; ++i) {
		auto current = pipeline.latest_frame();
		if (current && current->stamp.generation != old.generation)
			unexpected = true;
		if (current)
			current->input_bgra.setTo(cv::Scalar::all(99));
	}
	producer.join();
	require(!unexpected && pipeline.latest_frame()->input_bgra.at<cv::Vec4b>(0, 0)[0] == 11,
		"Concurrent reader mutated published pixels");
}

// Allocation failure must be distinguishable from an ordinary stale/malformed rejection.
static void allocation_failure_is_not_authority_rejection()
{
	ImagePipeline pipeline;
	pipeline.configure(eligible_config());
	const auto config = pipeline.snapshot();
	auto input = frame(config), next_input = frame(config, 2);
	auto output = mask(config), next_output = mask(config, 2);
	require(pipeline.publish_frame(input) && pipeline.publish_mask(output), "Initial packets missing");
	for (int operation = 0; operation < 4; ++operation) {
		bool failed = false;
		{
			gpu_test::MatAllocationScope allocation_failure;
			try {
				if (operation == 0)
					pipeline.publish_frame(next_input);
				if (operation == 1)
					pipeline.publish_mask(next_output);
				if (operation == 2)
					pipeline.latest_frame();
				if (operation == 3)
					pipeline.latest_mask();
			} catch (const std::bad_alloc &) {
				failed = true;
			} catch (const cv::Exception &) {
				failed = true;
			}
		}
		require(failed, "Clone allocation failure was disguised as authority rejection");
		require(pipeline.latest_frame()->stamp.frame_id == 1 && pipeline.latest_mask()->stamp.frame_id == 1,
			"Failed clone changed the previously owned mailbox");
	}
	{
		gpu_test::MatAllocationScope allocation_failure;
		require(!pipeline.publish_frame(input) && !pipeline.publish_mask(output),
			"Ordinary duplicate rejection unexpectedly changed classification");
		require(gpu_test::MatAllocationFault::allocations == 0,
			"Rejected packets allocated before authority validation");
	}
	for (int operation = 0; operation < 2; ++operation) {
		pipeline.configure(eligible_config());
		const auto before = pipeline.snapshot();
		auto pending_frame = frame(before, 4);
		auto pending_mask = mask(before, 4);
		gpu_test::MatAllocationScope observe(-1);
		gpu_test::MatAllocationFault::before_allocate = [&] {
			auto changed = before;
			changed.source = {1920, 1080};
			pipeline.configure(changed);
		};
		require(!(operation == 0 ? pipeline.publish_frame(pending_frame) : pipeline.publish_mask(pending_mask)),
			"Publication accepted a packet invalidated while its clone allocated");
		require(!pipeline.latest_frame() && !pipeline.latest_mask(),
			"Late clone restored stale mailbox storage");
	}
	{
		gpu_test::MatAllocationScope allocation_failure;
		require(!pipeline.publish_frame(input) && !pipeline.publish_mask(output),
			"Old-generation packets tried allocation instead of being rejected");
		require(gpu_test::MatAllocationFault::allocations == 0, "Obsolete publications allocated storage");
	}
	std::cout << "Actual OpenCV clone allocation failure classification PASS\n";
}

int main()
{
	try {
		allocation_failure_is_not_authority_rejection();
		packets_own_their_pixels();
		stale_generation_or_dimensions_rejected();
		late_status_cannot_overwrite_new_generation();
		processing_fallback_transitions_invalidate_the_route();
		unchanged_configure_preserves_current_packets();
		relevant_changes_advance_generation();
		malformed_or_out_of_order_packets_cannot_replace_current();
		mask_packet_dimensions_preserve_stage2_and_legacy_semantics();
		similarity_requires_full_readback();
		invalidation_is_terminal_for_its_generation();
		concurrent_publications_remain_owned_and_current();
		std::cout << "GPU image pipeline tests passed\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
}

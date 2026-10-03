// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <limits>

#include "gpu-mask-processor.hpp"
#include "background-mask-cpu.hpp"
#include "graphics-fault-controls.hpp"
#include "blend-state-probe.hpp"
#include <opencv2/imgproc.hpp>
using namespace gpu_image;

static void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
static cv::Mat read_mask(gs_texture_t *texture)
{
	const auto width = gs_texture_get_width(texture), height = gs_texture_get_height(texture);
	require(gs_texture_get_color_format(texture) == GS_BGRA, "Borrowed mask must expose BGRA red-channel pixels");
	auto *stage = gs_stagesurface_create(width, height, GS_BGRA);
	require(stage != nullptr, "Mask readback allocation failed");
	gs_stage_texture(stage, texture);
	uint8_t *data = nullptr;
	uint32_t pitch = 0;
	if (!gs_stagesurface_map(stage, &data, &pitch)) {
		gs_stagesurface_destroy(stage);
		throw std::runtime_error("Mask readback map failed");
	}
	cv::Mat mask;
	try {
		require(data && uint64_t{pitch} >= uint64_t{width} * 4, "Mask readback pitch invalid");
		cv::extractChannel(cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC4, data, pitch),
				   mask, 2);
	} catch (...) {
		gs_stagesurface_unmap(stage);
		gs_stagesurface_destroy(stage);
		throw;
	}
	gs_stagesurface_unmap(stage);
	gs_stagesurface_destroy(stage);
	return mask;
}
static double foreground_iou(const cv::Mat &actual, const cv::Mat &reference)
{
	cv::Mat a = actual > 128, b = reference > 128, both, either;
	cv::bitwise_and(a, b, both);
	cv::bitwise_or(a, b, either);
	const int count = cv::countNonZero(either);
	return count == 0 ? 1.0 : static_cast<double>(cv::countNonZero(both)) / count;
}
static void compare(const cv::Mat &actual, const cv::Mat &reference, const char *name)
{
	require(actual.size() == reference.size() && actual.type() == CV_8UC1, "Final mask dimensions/type differ");
	const double mae = cv::norm(actual, reference, cv::NORM_L1) / (static_cast<double>(reference.total()) * 255.0);
	const double iou = foreground_iou(actual, reference);
	std::cout << "mask-reference case=" << name << " dimensions=" << actual.cols << 'x' << actual.rows
		  << " normalized-mae=" << mae << " foreground-iou=" << iou << std::endl;
	require(mae <= 0.01, "Final mask exceeds normalized MAE0.01");
	require(iou >= 0.98, "Final mask foreground IoU below0.98");
}
static cv::Mat scene()
{
	cv::Mat mask = cv::Mat::zeros(144, 256, CV_8UC1);
	cv::rectangle(mask, {48, 24, 151, 99}, cv::Scalar(255), -1);
	cv::circle(mask, {180, 40}, 23, cv::Scalar(255), -1);
	cv::line(mask, {3, 2}, {240, 131}, cv::Scalar(255), 1);
	cv::line(mask, {12, 120}, {244, 137}, cv::Scalar(255), 3);
	mask.row(0).colRange(3, 32).setTo(255);
	mask.col(255).rowRange(60, 110).setTo(255);
	mask.at<uint8_t>(143, 0) = 255;
	return mask;
}
static cv::Mat cpu_display(const cv::Mat &small, Dimensions source, const MaskSettings &settings)
{
	auto expected = finish_mask_cpu(small, source, settings);
	if (!settings.enable_threshold)
		cv::resize(expected, expected, {static_cast<int>(source.width), static_cast<int>(source.height)});
	return expected;
}
static cv::Mat reference_case(GpuMaskProcessor &processor, const std::string &effect, const cv::Mat &small,
			      Dimensions source, const MaskSettings &settings, const char *name)
{
	const Dimensions input{static_cast<uint32_t>(small.cols), static_cast<uint32_t>(small.rows)};
	if (!processor.prepare(input, source, effect.c_str()))
		throw std::runtime_error("GPU mask preparation failed: " + processor.failure_reason());
	const auto original = small.clone();
	MaskPacket packet{{1, 1, source, input}, small, true};
	const bool srgb = gs_framebuffer_srgb_enabled(), linear = gs_get_linear_srgb();
	gs_enable_framebuffer_srgb(true);
	gs_set_linear_srgb(true);
	auto *output = processor.process(packet, settings);
	require(gs_framebuffer_srgb_enabled() && gs_get_linear_srgb(),
		"Mask processing must restore caller sRGB state");
	gs_enable_framebuffer_srgb(srgb);
	gs_set_linear_srgb(linear);
	if (!output)
		throw std::runtime_error("GPU mask failed: " + processor.failure_reason());
	const auto actual = read_mask(output);
	compare(actual, cpu_display(small, source, settings), name);
	require(cv::norm(original, small, cv::NORM_INF) == 0, "Mask processing must not mutate packet pixels");
	return actual;
}

void run_mask_cases(const std::filesystem::path &effect_root)
{
	const auto effect = (effect_root / "gpu_mask_processing.effect").string();
	GpuMaskProcessor processor;
	MaskSettings settings{};
	settings.enable_threshold = true;
	const auto mask = scene();
	std::cout << "case mask_ignores_and_restores_reverse_subtract_blending" << std::endl;
	{
		gpu_test::ReverseSubtractState incoming_blend;
		settings.smooth_contour = 0.5f;
		settings.mask_expansion = 2;
		settings.feather = 0.2f;
		reference_case(processor, effect, mask, {641, 359}, settings, "reverse_subtract_blending");
		gpu_test::require_reverse_subtract_restored((effect_root / "input_downscale.effect").string());
	}
	settings = {};
	settings.enable_threshold = true;
	std::cout << "case thin_edges_and_border_impulses" << std::endl;
	for (const auto smoothing : {0.001f, 0.2f, 0.5f, 1.0f}) {
		settings.smooth_contour = smoothing;
		reference_case(processor, effect, mask, {641, 359}, settings, "thin_edges_and_border_impulses");
	}
	std::cout << "case zero_max_smoothing_feather" << std::endl;
	for (const auto smoothing : {0.0f, 1.0f})
		for (const auto feather : {0.0f, 0.05f, 1.0f}) {
			settings.smooth_contour = smoothing;
			settings.feather = feather;
			reference_case(processor, effect, mask, {512, 288}, settings, "zero_max_smoothing_feather");
		}
	std::cout << "case expansion_both_signs" << std::endl;
	settings.smooth_contour = 0;
	settings.feather = 0;
	for (const int expansion : {-30, -2, 2, 30}) {
		settings.mask_expansion = expansion;
		reference_case(processor, effect, mask, {641, 359}, settings, "expansion_both_signs");
	}
	std::cout << "case threshold_disabled_preserves_conditions" << std::endl;
	settings.enable_threshold = false;
	settings.smooth_contour = 1;
	settings.feather = 1;
	settings.mask_expansion = 30;
	cv::Mat levels(144, 256, CV_8UC1);
	for (int y = 0; y < levels.rows; ++y)
		for (int x = 0; x < levels.cols; ++x)
			levels.at<uint8_t>(y, x) = static_cast<uint8_t>((x + y) % 256);
	reference_case(processor, effect, levels, {641, 359}, settings, "threshold_disabled_preserves_conditions");
	std::cout << "case temporal_quantization_and_postthreshold_boundaries" << std::endl;
	settings = {};
	settings.enable_threshold = true;
	settings.smooth_contour = 0.5f;
	for (const auto value : {127, 128, 129}) {
		cv::Mat boundary(144, 256, CV_8UC1, cv::Scalar(value));
		reference_case(processor, effect, boundary, {512, 288}, settings, "postthreshold_constant_boundary");
	}
	std::cout << "case scalar_alias_boundary_and_strided_upload" << std::endl;
	settings = {};
	settings.enable_threshold = true;
	settings.smooth_contour = 0.001f;
	cv::Mat scalar_boundary = cv::Mat::zeros(3, 32, CV_8UC1);
	scalar_boundary.col(1).setTo(255);
	scalar_boundary.col(2).setTo(140);
	reference_case(processor, effect, scalar_boundary, {32, 3}, settings, "scalar_alias_boundary");
	cv::Mat parent(146, 260, CV_8UC1, cv::Scalar(199));
	auto strided = parent(cv::Rect(2, 1, 256, 144));
	mask.copyTo(strided);
	require(!strided.isContinuous(), "Upload fixture must have a non-contiguous row stride");
	reference_case(processor, effect, strided, {641, 359}, settings, "strided_upload");
	for (int x = 0; x < levels.cols; ++x)
		levels.col(x).setTo(126 + (x % 5));
	for (const auto smoothing : {0.001f, 0.2f, 0.5f, 1.0f}) {
		settings.smooth_contour = smoothing;
		reference_case(processor, effect, levels, {512, 288}, settings, "spatial_postthreshold_boundaries");
	}
	std::cout << "case known_empty_full_masks" << std::endl;
	settings.smooth_contour = 1;
	settings.feather = 1;
	for (const auto value : {0, 255}) {
		cv::Mat constant(144, 256, CV_8UC1, cv::Scalar(value));
		const auto actual =
			reference_case(processor, effect, constant, {512, 288}, settings, "known_empty_full_masks");
		require(cv::countNonZero(actual != value) == 0,
			"Known empty/full masks must retain every exact constant byte");
	}
	require(foreground_iou(cv::Mat::zeros(2, 2, CV_8UC1), cv::Mat::zeros(2, 2, CV_8UC1)) == 1,
		"Both empty sets IoU must be1");
	require(foreground_iou(cv::Mat::zeros(2, 2, CV_8UC1), cv::Mat(2, 2, CV_8UC1, cv::Scalar(255))) == 0,
		"Exactly one empty set IoU must be0");
	std::cout << "case same_size_reuses_resources" << std::endl;
	settings = {};
	settings.enable_threshold = true;
	reference_case(processor, effect, mask, {512, 288}, settings, "same_size_initial");
	const auto before = gpu_test::counts().allocations;
	for (int i = 0; i < 5; ++i) {
		cv::Mat changed = 255 - mask;
		settings.smooth_contour = static_cast<float>(i) / 4;
		settings.feather = static_cast<float>(i) / 4;
		reference_case(processor, effect, changed, {512, 288}, settings, "same_size_changed_pixels");
	}
	require(gpu_test::counts().allocations == before,
		"Stable dimensions/settings changes must reuse graphics resources");
	std::cout << "case mask_resource_failures_and_recovery" << std::endl;
	require(!processor.prepare({256, 144}, {512, 288}, "missing-mask.effect"), "Missing mask effect must fail");
	require(!processor.failure_reason().empty(), "Mask failure requires diagnostics");
	require(!processor.process({{1, 1, {512, 288}, {256, 144}}, mask, true}, settings),
		"Failed preparation must not publish stale texture");
	reference_case(processor, effect, mask, {641, 359}, settings, "recovery_after_missing_effect");
	require(!processor.process({{1, 2, {640, 359}, {256, 144}}, mask, true}, settings),
		"Mismatched source packet must fail");
	require(!processor.process({{1, 2, {641, 359}, {256, 144}}, mask, false}, settings),
		"CPU-finished packet is not a GPU postprocessing input");
	processor.release();
	processor.release();
	reference_case(processor, effect, mask, {1280, 720}, settings, "resize_and_repeated_release");
	processor.release();
	const auto empty_resources = gpu_test::counts().live_resources;
	for (unsigned failed_allocation = 0; failed_allocation < 7; ++failed_allocation) {
		gpu_test::fail_graphics_allocation_after(failed_allocation);
		require(!processor.prepare({256, 144}, {512, 288}, effect.c_str()),
			"Injected partial graphics allocation must fail preparation");
		require(!processor.failure_reason().empty(), "Partial allocation failure needs a diagnostic");
		require(gpu_test::counts().live_resources == empty_resources,
			"Failed prepare must release every partial graphics resource");
		processor.release();
		processor.release();
		reference_case(processor, effect, mask, {512, 288}, settings, "partial_allocation_recovery");
		processor.release();
		require(gpu_test::counts().live_resources == empty_resources,
			"Recovered release must leave no graphics resources");
	}
	gpu_test::fail_next_render_begin();
	require(!processor.prepare({256, 144}, {512, 288}, effect.c_str()),
		"Initial render-target materialization failure must fail prepare");
	require(gpu_test::counts().live_resources == empty_resources,
		"Failed target materialization must release partial resources");
	reference_case(processor, effect, mask, {512, 288}, settings, "materialization_recovery");
	MaskPacket packet{{1, 1, {512, 288}, {256, 144}}, mask, true};
	const auto before_upload = gpu_test::counts();
	gpu_test::fail_next_upload_map();
	require(!processor.process(packet, settings), "Failed mask upload must not return prior output");
	require(!processor.failure_reason().empty(), "Upload failure must have diagnostic");
	require(gpu_test::counts().upload_maps == before_upload.upload_maps &&
			gpu_test::counts().upload_unmaps == before_upload.upload_unmaps,
		"Failed upload mapping must not be unmapped");
	reference_case(processor, effect, mask, {512, 288}, settings, "upload_map_recovery");
	gpu_test::fail_next_render_begin();
	require(!processor.process(packet, settings), "Failed mask draw must not return prior output");
	require(gpu_test::counts().live_resources == empty_resources, "Draw failure must discard processor resources");
	reference_case(processor, effect, mask, {512, 288}, settings, "draw_recovery");
	MaskPacket wrong_type = packet;
	wrong_type.mask = cv::Mat(144, 256, CV_32FC1, cv::Scalar(0));
	require(!processor.process(wrong_type, settings),
		"Float packet must be rejected rather than uploaded as bytes");
	auto invalid_settings = settings;
	invalid_settings.smooth_contour = std::numeric_limits<float>::quiet_NaN();
	require(!processor.process(packet, invalid_settings),
		"Nonfinite spatial settings must fail without a stale output");
	auto *borrowed = processor.process(packet, settings);
	require(borrowed != nullptr, "Borrowed mask must be available inside graphics ownership");
	const auto accepted = read_mask(borrowed);
	std::unique_ptr<gs_texture_t, decltype(&gs_texture_destroy)> display_copy(
		gs_texture_create(512, 288, GS_BGRA, 1, nullptr, GS_RENDER_TARGET), &gs_texture_destroy);
	require(display_copy != nullptr, "Consumer-owned display texture allocation failed");
	gs_copy_texture(display_copy.get(), borrowed); // Consume only during the valid borrowed lifetime.
	packet.mask = 255 - mask;
	require(processor.process(packet, settings) != nullptr, "Subsequent changed mask processing failed");
	processor.release();
	processor.release(); // borrowed is now invalid and never dereferenced again.
	require(cv::norm(read_mask(display_copy.get()), accepted, cv::NORM_INF) == 0,
		"Consumer-owned copy must survive later process and processor release");
	require(gpu_test::counts().live_resources == empty_resources,
		"Mask processor must release every owned graphics resource");
	require(gpu_test::counts().upload_maps == gpu_test::counts().upload_unmaps,
		"Every successful small-mask upload must be unmapped");
	std::cout << "gpu-mask-cases PASS" << std::endl;
}

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "background-mask-cpu.hpp"
#include "gpu-input-preprocessor.hpp"
#include "gpu-mask-processor.hpp"
#include "filter-boundaries.hpp"
#include "mask-comparison.hpp"
#include "ort-utils/ort-session-utils.hpp"
#include "models/ModelMediapipe.hpp"
#include "consts.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gpu_image;
void require(bool condition, const std::string &message)
{
	if (!condition)
		throw std::runtime_error(message);
}
void mask_contract(const cv::Mat &mask, Dimensions size, const std::string &name)
{
	require(!mask.empty() && mask.type() == CV_8UC1 && mask.cols == static_cast<int>(size.width) &&
			mask.rows == static_cast<int>(size.height),
		name + ": mask dimensions/type");
	double minimum = 0, maximum = 0;
	cv::minMaxLoc(mask, &minimum, &maximum);
	const auto foreground = cv::countNonZero(mask < 128);
	std::cout << "quality-mask boundary=" << name << " dimensions=" << mask.cols << 'x' << mask.rows << " range=["
		  << minimum << ',' << maximum << "] retained-alpha-foreground=" << foreground
		  << " non-retained-background=" << mask.total() - static_cast<size_t>(foreground) << std::endl;
	require(std::isfinite(minimum) && std::isfinite(maximum) && minimum >= 0 && maximum <= 255,
		name + ": finite bounded byte mask");
}
void nondegenerate(const cv::Mat &mask, const std::string &name, double minimum_fraction)
{
	const double foreground = cv::countNonZero(mask < 128);
	const double total = static_cast<double>(mask.total());
	require(foreground >= minimum_fraction * total && total - foreground >= minimum_fraction * total,
		name + ": real portrait requires foreground and background");
}
struct Sample {
	cv::Mat mask;
	std::vector<float> tensor;
};
class CommonCpuSession {
public:
	explicit CommonCpuSession(const std::filesystem::path &model) : data_()
	{
		data_.env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "gpu-image-quality-cpu-reference");
		data_.model = std::make_unique<ModelMediaPipe>();
		data_.modelSelection = MODEL_MEDIAPIPE;
		data_.useGPU = USEGPU_CPU;
		data_.numThreads = 1;
		try {
			require(createWindowsMlOrtSession(&data_, model) == OBS_BGREMOVAL_ORT_SESSION_SUCCESS,
				"Real CPU reference session initialization failed");
			require(data_.inputDims == std::vector<std::vector<int64_t>>{{1, 144, 256, 3}} &&
					data_.outputDims == std::vector<std::vector<int64_t>>{{1, 144, 256, 2}},
				"Real MediaPipe metadata differs");
			check_provider();
		} catch (...) {
			resetOrtSessionData(data_);
			throw;
		}
	}
	~CommonCpuSession() { resetOrtSessionData(data_); }
	CommonCpuSession(const CommonCpuSession &) = delete;
	CommonCpuSession &operator=(const CommonCpuSession &) = delete;
	Sample run(const cv::Mat &bgra, const std::string &label)
	{
		require(!bgra.empty() && bgra.type() == CV_8UC4, label + ": BGRA input required");
		std::cout << "quality-inference begin=" << label << " provider=CPUExecutionProvider" << std::endl;
		cv::Mat output;
		require(runFilterModelInference(&data_, bgra, output), label + ": production adapter inference failed");
		check_provider();
		require(data_.inputTensorValues.size() == 1 && data_.inputTensorValues[0].size() == 256 * 144 * 3 &&
				data_.outputTensorValues.size() == 1 &&
				data_.outputTensorValues[0].size() == 256 * 144 * 2,
			label + ": real tensor buffers differ");
		for (const float input : data_.inputTensorValues[0])
			require(std::isfinite(input) && input >= 0 && input <= 1,
				label + ": prepared tensor must be finite normalized input");
		cv::Mat raw_pair(144, 256, CV_32FC2, data_.outputTensorValues[0].data()), raw;
		cv::extractChannel(raw_pair, raw, 1);
		for (int y = 0; y < raw.rows; ++y)
			for (int x = 0; x < raw.cols; ++x)
				require(std::isfinite(raw.at<float>(y, x)), label + ": raw model channel is nonfinite");
		double minimum = 0, maximum = 0;
		cv::minMaxLoc(raw, &minimum, &maximum);
		std::cout << "quality-inference end=" << label << " provider=CPUExecutionProvider raw-channel-1=["
			  << minimum << ',' << maximum << "] conversion=unchanged-CV8U-times255-saturation"
			  << std::endl;
		cv::Mat exact_saturation;
		raw.convertTo(exact_saturation, CV_8U, 255.0);
		mask_contract(output, {256, 144}, label + "/adapter-output");
		require(cv::norm(exact_saturation, output, cv::NORM_INF) == 0,
			label + ": production adapter must preserve raw-channel saturation");
		return {output.clone(), data_.inputTensorValues[0]};
	}

private:
	void check_provider() const
	{
		require(data_.session && data_.sessionDiagnostics.outcome == windows_ml::SessionOutcome::Ready &&
				data_.sessionDiagnostics.effective_provider == "CPUExecutionProvider" &&
				data_.sessionDiagnostics.fallback_reason.empty() &&
				!data_.sessionDiagnostics.provider_attempt,
			"Common reference must remain actual completed CPU, without a fabricated GPU provider");
	}
	filter_data data_;
};
using Texture = std::unique_ptr<gs_texture_t, decltype(&gs_texture_destroy)>;
cv::Mat read_bgra(gs_texture_t *texture)
{
	require(texture && gs_texture_get_color_format(texture) == GS_BGRA, "Quality readback requires BGRA");
	const uint32_t width = gs_texture_get_width(texture), height = gs_texture_get_height(texture);
	std::unique_ptr<gs_stagesurf_t, decltype(&gs_stagesurface_destroy)> stage(
		gs_stagesurface_create(width, height, GS_BGRA), &gs_stagesurface_destroy);
	require(stage != nullptr, "Quality readback stage allocation failed");
	gs_stage_texture(stage.get(), texture);
	uint8_t *pixels = nullptr;
	uint32_t pitch = 0;
	require(gs_stagesurface_map(stage.get(), &pixels, &pitch), "Quality readback map failed");
	cv::Mat owned;
	try {
		require(pixels && uint64_t{pitch} >= uint64_t{width} * 4, "Quality readback pitch invalid");
		owned = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC4, pixels, pitch).clone();
	} catch (...) {
		gs_stagesurface_unmap(stage.get());
		throw;
	}
	gs_stagesurface_unmap(stage.get());
	return owned;
}
PipelineConfig helper_geometry(Dimensions source)
{
	// Direct-helper test input only. This does not describe, modify or authorize
	// the real CPU session/filter. Task 4 independently tests production policy.
	PipelineConfig config{};
	config.generation = 1;
	config.source = source;
	config.input = {256, 144};
	config.requested = config.windows = config.mediapipe = config.session_ready = config.effective_directml = true;
	config.mask_every_x_frames = 1;
	return config;
}
double input_mae(const Sample &reference, const Sample &candidate)
{
	require(reference.tensor.size() == candidate.tensor.size() && !reference.tensor.empty(),
		"Input tensor sizes differ");
	double error = 0;
	for (size_t i = 0; i < reference.tensor.size(); ++i)
		error += std::abs(static_cast<double>(reference.tensor[i]) - candidate.tensor[i]);
	return error / static_cast<double>(reference.tensor.size());
}
bool compare(const cv::Mat &candidate, const cv::Mat &reference, Dimensions size, const std::string &name,
	     double prepared_mae)
{
	mask_contract(reference, size, name + "/reference");
	mask_contract(candidate, size, name + "/candidate");
	nondegenerate(reference, name, 0.000001);
	const auto metrics = gpu_test::compare_masks(candidate, reference);
	std::cout << "quality-result case=" << name << " provider=CPUExecutionProvider helper-eligibility=test-input"
		  << " dimensions=" << size.width << 'x' << size.height << " prepared-input-mae=" << prepared_mae
		  << " final-normalized-mae=" << metrics.normalized_mae
		  << " retained-alpha-iou=" << metrics.retained_alpha_iou
		  << " positive-background-iou=" << metrics.positive_background_iou << std::endl;
	return std::isfinite(prepared_mae) && prepared_mae <= 0.01 && metrics.passes(true);
}
cv::Mat gpu_finish(GpuMaskProcessor &processor, const cv::Mat &small, Dimensions source, const MaskSettings &settings,
		   uint64_t frame_id)
{
	const auto original = small.clone();
	auto *borrowed = processor.process({{1, frame_id, source, {256, 144}}, small, true}, settings);
	require(borrowed != nullptr, "Quality GPU mask processing failed: " + processor.failure_reason());
	cv::Mat mask;
	cv::extractChannel(read_bgra(borrowed), mask, 2); // Own red channel before next process.
	require(cv::norm(original, small, cv::NORM_INF) == 0, "GPU quality upload must preserve small CPU mask");
	return mask;
}
cv::Mat display_reference(const cv::Mat &finished, Dimensions source, const MaskSettings &settings)
{
	if (settings.enable_threshold)
		return finished;
	// Threshold-OFF legacy finished mask is model-sized; final source compositing
	// samples it linearly. Preserve that shape and the existing mask-test oracle.
	cv::Mat display;
	cv::resize(finished, display, {static_cast<int>(source.width), static_cast<int>(source.height)}, 0, 0,
		   cv::INTER_LINEAR);
	return display;
}
cv::Mat compare_setting(GpuMaskProcessor &processor, const Sample &reference, const Sample &candidate,
			Dimensions source, const MaskSettings &settings, const std::string &name, uint64_t frame_id,
			cv::Mat &reference_history, cv::Mat &candidate_history)
{
	const auto baseline = prepare_small_mask(reference.mask, reference_history, settings);
	const auto reduced = prepare_small_mask(candidate.mask, candidate_history, settings);
	mask_contract(baseline.mask, {256, 144}, name + "/prepared-reference");
	mask_contract(reduced.mask, {256, 144}, name + "/prepared-candidate");
	if (settings.temporal_smooth_factor > 0 && settings.temporal_smooth_factor < 1) {
		const auto diagnose_history = [&](const cv::Mat &previous, const SmallMaskPreparation &prepared,
						  const std::string &stream) {
			mask_contract(prepared.temporal_history, {256, 144},
				      name + "/history-before-contours/" + stream);
			const int intermediate =
				cv::countNonZero((prepared.temporal_history > 0) & (prepared.temporal_history < 255));
			const double delta =
				previous.empty() ? 0 : cv::norm(previous, prepared.temporal_history, cv::NORM_L1);
			std::cout << "quality-history case=" << name << " stream=" << stream
				  << " previous-present=" << !previous.empty() << " history-delta-l1=" << delta
				  << " weighted-intermediate-pixels=" << intermediate
				  << " history-nonzero=" << cv::countNonZero(prepared.temporal_history)
				  << " reconstructed-contour-nonzero=" << cv::countNonZero(prepared.mask) << std::endl;
			if (!previous.empty())
				require(delta > 0 && intermediate > 0,
					name + ": temporal transition must change actual uncontoured weighted history");
		};
		diagnose_history(reference_history, baseline, "reference");
		diagnose_history(candidate_history, reduced, "candidate");
		require(baseline.temporal_history.data != reduced.temporal_history.data,
			name + ": baseline and candidate temporal histories must own separate storage");
	}
	reference_history = baseline.temporal_history;
	candidate_history = reduced.temporal_history;
	const double prepared_mae = input_mae(reference, candidate);
	const Dimensions legacy_size = settings.enable_threshold ? source : Dimensions{256, 144};
	const auto cpu = finish_mask_cpu(baseline.mask, source, settings);
	const auto stage1 = finish_mask_cpu(reduced.mask, source, settings);
	const bool stage1_ok = compare(stage1, cpu, legacy_size, name + "/stage1", prepared_mae);
	const auto display = display_reference(cpu, source, settings);
	const auto stage2 = gpu_finish(processor, baseline.mask, source, settings, frame_id);
	const bool stage2_ok = compare(stage2, display, source, name + "/stage2", 0);
	const auto combined = gpu_finish(processor, reduced.mask, source, settings, frame_id);
	const bool combined_ok = compare(combined, display, source, name + "/combined", prepared_mae);
	require(stage1_ok && stage2_ok && combined_ok, name + ": quality gate MAE<=0.01/IoU>=0.98 failed");
	return cpu;
}
struct FramePair {
	Sample reference, candidate;
};
FramePair infer_frame(CommonCpuSession &session, GpuInputPreprocessor &processor, const cv::Mat &frame,
		      uint64_t frame_id, const std::string &name)
{
	const Dimensions source{static_cast<uint32_t>(frame.cols), static_cast<uint32_t>(frame.rows)};
	require(frame.type() == CV_8UC4 && frame.isContinuous(), "Owned contiguous BGRA source required");
	const auto *pixels = frame.ptr<uint8_t>();
	Texture texture(gs_texture_create(source.width, source.height, GS_BGRA, 1, &pixels, 0), &gs_texture_destroy);
	require(texture != nullptr, "Portrait source texture allocation failed");
	require(cv::norm(read_bgra(texture.get()), frame, cv::NORM_INF) == 0,
		name + ": source upload identity differs");
	const auto packet = processor.capture(texture.get(), {1, frame_id, source, {256, 144}}, false);
	require(packet.has_value(), name + ": GPU input capture failed: " + processor.failure_reason());
	require(packet->input_bgra.type() == CV_8UC4 && packet->input_bgra.cols == 256 &&
			packet->input_bgra.rows == 144 && packet->similarity_bgra.empty(),
		name + ": model-sized capture contract differs");
	auto reference = session.run(frame, name + "/full-source");
	auto candidate = session.run(packet->input_bgra, name + "/gpu-reduced");
	nondegenerate(reference.mask, name + "/real-model-reference", 0.01);
	nondegenerate(candidate.mask, name + "/real-model-candidate", 0.01);
	require(cv::norm(read_bgra(texture.get()), frame, cv::NORM_INF) == 0 &&
			gs_texture_get_width(texture.get()) == source.width &&
			gs_texture_get_height(texture.get()) == source.height,
		name + ": full source pixels/dimensions changed");
	return {std::move(reference), std::move(candidate)};
}
void source_cases(CommonCpuSession &session, const cv::Mat &portrait, Dimensions size,
		  const std::filesystem::path &effects)
{
	cv::Mat frame;
	cv::resize(portrait, frame, {static_cast<int>(size.width), static_cast<int>(size.height)}, 0, 0,
		   cv::INTER_LINEAR);
	const auto input_effect = (effects / "input_downscale.effect").string();
	const auto mask_effect = (effects / "gpu_mask_processing.effect").string();
	GpuInputPreprocessor input;
	GpuMaskProcessor mask;
	require(input.prepare(helper_geometry(size), input_effect.c_str()), "Quality input preparation failed");
	require(mask.prepare({256, 144}, size, mask_effect.c_str()), "Quality mask preparation failed");
	const std::string prefix = "portrait-" + std::to_string(size.width) + 'x' + std::to_string(size.height);
	const auto pair = infer_frame(session, input, frame, 1, prefix);
	MaskSettings baseline{};
	baseline.enable_threshold = true;
	baseline.threshold = 0.5f;
	auto threshold_off = baseline;
	threshold_off.enable_threshold = false;
	threshold_off.smooth_contour = threshold_off.feather = 1;
	threshold_off.mask_expansion = 30;
	auto moderate = baseline;
	moderate.smooth_contour = 0.2f;
	moderate.feather = 0.1f;
	moderate.mask_expansion = 2;
	auto negative = baseline;
	negative.mask_expansion = -2;
	std::vector<std::pair<std::string, MaskSettings>> settings{{"zero", baseline},
								   {"threshold-off", threshold_off},
								   {"moderate", moderate},
								   {"negative-expansion", negative}};
	if (size == Dimensions{641, 359}) {
		auto maximum = baseline;
		maximum.smooth_contour = maximum.feather = 1;
		settings.emplace_back("max-smoothing-feather", maximum);
	}
	for (const auto &[name, setting] : settings) {
		cv::Mat reference_history, candidate_history;
		compare_setting(mask, pair.reference, pair.candidate, size, setting, prefix + '/' + name, 1,
				reference_history, candidate_history);
	}
	if (size == Dimensions{641, 359}) {
		std::cout << "quality-motion synthetic-translation-of-still-portrait offsets=0,64,128" << std::endl;
		cv::Mat translated(frame.size(), frame.type(), cv::Scalar(0, 0, 0, 255));
		cv::Mat translated_again(frame.size(), frame.type(), cv::Scalar(0, 0, 0, 255));
		constexpr int translation = 64;
		frame(cv::Rect(0, 0, frame.cols - translation, frame.rows))
			.copyTo(translated(cv::Rect(translation, 0, frame.cols - translation, frame.rows)));
		frame(cv::Rect(0, 0, frame.cols - 2 * translation, frame.rows))
			.copyTo(translated_again(
				cv::Rect(2 * translation, 0, frame.cols - 2 * translation, frame.rows)));
		auto temporal = moderate;
		temporal.temporal_smooth_factor = 0.65f;
		temporal.contour_filter = 0.0001f;
		cv::Mat reference_history, candidate_history, previous_cpu;
		int changed_masks = 0;
		uint64_t frame_id = 1;
		// Weighted history contains nonzero support from both prior and current
		// masks. Contours reconstruct that support at 255, so A -> B -> A can
		// retain the same union on the last frame. A third position supplies new
		// support while keeping the real temporal/contour settings unchanged.
		for (const auto &sequence_frame : {frame, translated, translated_again}) {
			const auto name = prefix + "/temporal-frame-" + std::to_string(frame_id);
			const auto motion_pair = infer_frame(session, input, sequence_frame, frame_id, name);
			const auto cpu = compare_setting(mask, motion_pair.reference, motion_pair.candidate, size,
							 temporal, name, frame_id, reference_history,
							 candidate_history);
			if (!previous_cpu.empty() && cv::norm(previous_cpu, cpu, cv::NORM_INF) > 0)
				++changed_masks;
			previous_cpu = cpu;
			++frame_id;
		}
		require(changed_masks == 2, "Temporal quality requires changed baseline masks in both transitions");
	}
}
} // namespace
void run_quality_cases(const std::filesystem::path &effects, const std::filesystem::path &model,
		       const std::filesystem::path &portrait_path)
{
	std::cout
		<< "quality-main begin actual-provider=CPUExecutionProvider graphics-helper-input=test-only-eligibility"
		<< " purpose=stage-isolation no-AMD-or-production-eligibility-claim" << std::endl;
	const auto bgr = cv::imread(portrait_path.string(), cv::IMREAD_COLOR);
	require(!bgr.empty() && bgr.type() == CV_8UC3 && bgr.cols == 1280 && bgr.rows == 720,
		"Explicit tracked portrait must decode as 1280x720 BGR");
	cv::Mat portrait;
	cv::cvtColor(bgr, portrait, cv::COLOR_BGR2BGRA);
	gpu_filter_test::initialize_paths(effects, model);
	CommonCpuSession session(model);
	for (const Dimensions size : {Dimensions{1280, 720}, Dimensions{1920, 1080}, Dimensions{641, 359}})
		source_cases(session, portrait, size, effects);
	std::cout << "quality-main PASS stages=stage1,stage2,combined provider=CPUExecutionProvider" << std::endl;
}

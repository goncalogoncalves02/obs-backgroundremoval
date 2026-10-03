// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filter-boundaries.hpp"
#include "graphics-fault-controls.hpp"
#include "mat-allocation-fault.hpp"
#include <functional>
#include <utility>
namespace display_copy_fault {
thread_local std::function<void()> after_copy;
void copy(gs_texture_t *target, gs_texture_t *source)
{
	gs_copy_texture(target, source);
	if (auto callback = std::exchange(after_copy, {}))
		callback();
}
} // namespace display_copy_fault
// Execute the actual callback TU and inspect its existing owned lifetime/model.
// Count actual production display-cache allocations too; wrappers delegate to real graphics.
#define gs_copy_texture display_copy_fault::copy
#define gs_texture_create gpu_test_texture_create
#define gs_texture_destroy gpu_test_texture_destroy
#include "background-filter.cpp"
#undef gs_copy_texture
#undef gs_texture_create
#undef gs_texture_destroy
#include "obs-utils/background-mask-cpu.hpp"
#include <map>
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
}
bool valid_raw_model_output(const cv::Mat &output)
{
	if (output.empty() || output.dims != 2 || output.type() != CV_32FC1)
		return false;
	// MediaPipe currently exposes its raw second channel. Preserve that contract;
	// even finite FLT_MAX is finite (checkRange's default upper bound excludes it).
	for (int row = 0; row < output.rows; ++row) {
		const auto *values = output.ptr<float>(row);
		for (int column = 0; column < output.cols; ++column)
			if (!std::isfinite(values[column]))
				return false;
	}
	return true;
}
void raw_output_regression()
{
	for (float value :
	     {0.0f, -0.0f, 1.0f, std::nextafter(1.0f, std::numeric_limits<float>::infinity()), -16.5347233f,
	      -7.18417215f, std::numeric_limits<float>::max(), -std::numeric_limits<float>::max()})
		check(valid_raw_model_output(cv::Mat(1, 1, CV_32FC1, cv::Scalar(value))),
		      "Finite actual raw-channel values were treated as normalized probabilities");
	for (float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
			    -std::numeric_limits<float>::infinity()})
		check(!valid_raw_model_output(cv::Mat(1, 1, CV_32FC1, cv::Scalar(value))),
		      "Non-finite raw output was accepted");
	check(!valid_raw_model_output({}) && !valid_raw_model_output(cv::Mat(1, 1, CV_8UC1, cv::Scalar(0))) &&
		      !valid_raw_model_output(cv::Mat(1, 1, CV_32FC2, cv::Scalar(0, 0))),
	      "Empty or incorrectly typed raw output was accepted");
	const int sizes[]{1, 1, 1};
	check(!valid_raw_model_output(cv::Mat(3, sizes, CV_32FC1, cv::Scalar(0))),
	      "Non-image raw output dimensions were accepted");
	cv::Mat raw(1, 2, CV_32FC1);
	raw.at<float>(0, 0) = -16.5347233f;
	raw.at<float>(0, 1) = -7.18417215f;
	cv::Mat converted;
	raw.convertTo(converted, CV_8U, 255.0);
	check(converted.type() == CV_8UC1 && converted.size() == raw.size() && cv::countNonZero(converted) == 0,
	      "Baseline conversion of actual finite negative raw output changed");
	std::cout
		<< "filter-baseline raw-output-regression finite-outside-unit-range=accepted nonfinite/type/dims=rejected baseline-conversion=preserved"
		<< std::endl;
}
// A test-only observer of the real MediaPipe implementation. Every operation
// delegates unchanged base behavior; no synthetic Session, tensors or success.
// The actual adapter invokes both methods while holding modelMutex. All fixture
// reads use that same mutex, including reads alongside the OBS video tick thread.
struct BlockedRun {
	std::atomic<bool> pending{true};
	std::promise<void> entered;
	std::promise<void> release;
};
struct ObservedMediaPipe final : ModelMediaPipe {
	std::shared_ptr<BlockedRun> block;
	std::atomic<unsigned> active_runs{0}, maximum_runs{0};
	uint64_t completed_runs = 0, completed_outputs = 0;
	cv::Mat completed_output;
	bool run_completed = false;

	void runNetworkInference(const std::unique_ptr<Ort::Session> &session,
				 const std::vector<Ort::AllocatedStringPtr> &inputNames,
				 const std::vector<Ort::AllocatedStringPtr> &outputNames,
				 const std::vector<Ort::Value> &inputTensor,
				 std::vector<Ort::Value> &outputTensor) override
	{
		const auto concurrent = ++active_runs;
		maximum_runs.store(std::max(maximum_runs.load(), concurrent));
		struct ExitRun {
			std::atomic<unsigned> &count;
			~ExitRun() { --count; }
		} exit{active_runs};
		if (block && block->pending.exchange(false)) {
			block->entered.set_value();
			check(block->release.get_future().wait_for(std::chrono::seconds(10)) ==
				      std::future_status::ready,
			      "Blocked real inference was not released");
		}
		run_completed = false;
		// The base intentionally returns without Run for empty bindings. Such
		// a return is not a successful inference and cannot increment evidence.
		check(session && !inputNames.empty() && !outputNames.empty() &&
			      inputNames.size() == inputTensor.size() && outputNames.size() == outputTensor.size(),
		      "Actual CPU inference bindings cannot execute Session::Run");
		ModelMediaPipe::runNetworkInference(session, inputNames, outputNames, inputTensor, outputTensor);
		// This line is reachable only after the real synchronous Run returned.
		++completed_runs;
		run_completed = true;
	}
	void postprocessOutput(cv::Mat &output) override
	{
		check(run_completed, "MediaPipe postprocess has no completed real Session::Run");
		ModelMediaPipe::postprocessOutput(output);
		const bool valid = valid_raw_model_output(output);
		if (completed_outputs == 0 || !valid) {
			if (!output.empty() && output.dims == 2 && output.type() == CV_32FC1) {
				double minimum, maximum;
				cv::minMaxLoc(output, &minimum, &maximum);
				obs_log(LOG_INFO,
					"Actual CPU MediaPipe raw output width=%d height=%d finite=%s min=%.9g max=%.9g valid=%s",
					output.cols, output.rows, valid ? "true" : "false", minimum, maximum,
					valid ? "true" : "false");
			} else {
				obs_log(LOG_INFO,
					"Actual CPU MediaPipe raw output dims=%d type=%d empty=%s valid=false",
					output.dims, output.type(), output.empty() ? "true" : "false");
			}
		}
		check(valid, "Actual MediaPipe raw output must be a finite 2D single-channel float image");
		completed_output = output.clone();
		++completed_outputs;
		run_completed = false;
	}
};
void require_completed_cpu_mask(uint64_t completed_runs, uint64_t completed_outputs, const cv::Mat &raw_output,
				const cv::Mat &displayed, gpu_image::Dimensions output, gpu_image::Dimensions source,
				const gpu_image::MaskSettings &settings)
{
	check(completed_runs > 0 && completed_outputs == completed_runs,
	      "Actual CPU Session::Run and delegated MediaPipe postprocess did not both complete");
	check(valid_raw_model_output(raw_output) && raw_output.cols == static_cast<int>(output.width) &&
		      raw_output.rows == static_cast<int>(output.height),
	      "Completed actual output differs from validated model dimensions");
	check(!displayed.empty() && displayed.type() == CV_8UC1 && displayed.cols == static_cast<int>(source.width) &&
		      displayed.rows == static_cast<int>(source.height),
	      "Actual CPU display mask has invalid type or source dimensions");
	cv::Mat bytes;
	raw_output.convertTo(bytes, CV_8U, 255.0);
	check(bytes.type() == CV_8UC1 && bytes.size() == raw_output.size(),
	      "Baseline converted model mask has invalid type or dimensions");
	double minimum, maximum;
	cv::minMaxLoc(bytes, &minimum, &maximum);
	check(minimum >= 0.0 && maximum <= 255.0, "Baseline converted model mask is not bounded to [0,255]");
	cv::minMaxLoc(displayed, &minimum, &maximum);
	check(minimum >= 0.0 && maximum <= 255.0, "Actual displayed CPU mask is not bounded to [0,255]");
	// The deterministic baseline never changes input/settings; repeated temporal
	// histories therefore equal the same threshold result. Use the already
	// validated retained CPU reference, without bypassing actual callback work.
	const auto small = gpu_image::prepare_small_mask(bytes, {}, settings);
	const auto expected = gpu_image::finish_mask_cpu(small.mask, source, settings);
	check(cv::norm(displayed, expected, cv::NORM_INF) == 0.0,
	      "Actual callback display mask differs from completed inference CPU reference");
}
// Schedule baseline capture/tick/render serially on the actual OBS video thread.
// Rendering graphics scopes end before invoking the production tick callback.
void draw(obs_source_t *source);
struct BaselineTick {
	void *filter_data;
	obs_source_t *source;
	BaselineTick(void *data, obs_source_t *target) : filter_data(data), source(target) {}
	std::atomic<bool> pending{true};
	std::promise<void> done;
};
void baseline_tick(void *data, float seconds)
{
	auto &request = *static_cast<BaselineTick *>(data);
	if (!request.pending.exchange(false))
		return;
	try {
		draw(request.source);
		const auto tf = *static_cast<std::shared_ptr<background_removal_filter> *>(request.filter_data);
		{
			std::lock_guard lock(tf->inputBGRALock);
			check(!tf->inputBGRA.empty(), "Actual baseline filter did not capture source pixels");
		}
		background_filter_video_tick(request.filter_data, seconds);
		draw(request.source);
		request.done.set_value();
	} catch (...) {
		request.done.set_exception(std::current_exception());
	}
}
void tick_baseline_on_obs_thread(void *filter_data, obs_source_t *source)
{
	BaselineTick request{filter_data, source};
	auto completion = request.done.get_future();
	obs_add_tick_callback(baseline_tick, &request);
	const auto result = completion.wait_for(std::chrono::seconds(10));
	// OBS removes under its callback mutex, so the stack-owned request remains
	// alive until a running callback has exited even on the timeout path.
	obs_remove_tick_callback(baseline_tick, &request);
	check(result == std::future_status::ready, "Actual OBS baseline tick did not finish");
	completion.get();
}
struct SerialAction {
	std::function<void()> action;
	std::atomic<bool> pending{true};
	std::promise<void> done;
	explicit SerialAction(std::function<void()> operation) : action(std::move(operation)) {}
};
void serial_action(void *data, float)
{
	auto &request = *static_cast<SerialAction *>(data);
	if (!request.pending.exchange(false))
		return;
	try {
		request.action();
		request.done.set_value();
	} catch (...) {
		request.done.set_exception(std::current_exception());
	}
}
void on_obs_thread(std::function<void()> action)
{
	SerialAction request{std::move(action)};
	auto complete = request.done.get_future();
	obs_add_tick_callback(serial_action, &request);
	const auto result = complete.wait_for(std::chrono::seconds(15));
	obs_remove_tick_callback(serial_action, &request);
	check(result == std::future_status::ready, "Serial actual OBS callback timed out");
	complete.get();
}
void process_frame(obs_source_t *filter, obs_source_t *source)
{
	on_obs_thread([&] {
		draw(source);
		background_filter_video_tick(obs_obj_get_data(filter), 1.0f / 30.0f);
	});
}
void initialized_mask_is_not_completion()
{
	bool rejected = false;
	try {
		require_completed_cpu_mask(0, 0, {}, cv::Mat(181, 321, CV_8UC1, cv::Scalar(255)), {256, 144},
					   {321, 181}, {});
	} catch (const std::runtime_error &) {
		rejected = true;
	}
	check(rejected, "Initialized fallback mask must not prove actual CPU inference completion");
	std::cout << "filter-baseline guard-regression initialized255-without-Run=rejected" << std::endl;
}
struct GraphicsScope {
	GraphicsScope() { obs_enter_graphics(); }
	~GraphicsScope() { obs_leave_graphics(); }
};
struct SourceFixture {
	std::atomic<uint32_t> width{321}, height{181};
	std::atomic<bool> failed{false};
	gs_texture_t *texture = nullptr;
	uint32_t texture_width = 0, texture_height = 0;
};
const char *source_name(void *)
{
	return "Deterministic native fixture source";
}
void *source_create(obs_data_t *, obs_source_t *)
{
	return new (std::nothrow) SourceFixture;
}
void source_destroy(void *data)
{
	auto *state = static_cast<SourceFixture *>(data);
	if (!state)
		return;
	GraphicsScope graphics;
	gs_texture_destroy(state->texture);
	delete state;
}
uint32_t source_width(void *data)
{
	return static_cast<SourceFixture *>(data)->width;
}
uint32_t source_height(void *data)
{
	return static_cast<SourceFixture *>(data)->height;
}
void source_render(void *data, gs_effect_t *)
{
	auto &state = *static_cast<SourceFixture *>(data);
	try {
		const uint32_t width = state.width, height = state.height;
		if (!state.texture || state.texture_width != width || state.texture_height != height) {
			gs_texture_destroy(state.texture);
			state.texture = nullptr;
			cv::Mat pixels(static_cast<int>(height), static_cast<int>(width), CV_8UC4);
			for (int y = 0; y < pixels.rows; ++y)
				for (int x = 0; x < pixels.cols; ++x)
					pixels.at<cv::Vec4b>(y, x) = cv::Vec4b(static_cast<uint8_t>(x % 256),
									       static_cast<uint8_t>(y % 256), 127, 255);
			const auto *bytes = pixels.data;
			state.texture = gs_texture_create(width, height, GS_BGRA, 1, &bytes, 0);
			check(state.texture != nullptr, "Actual source texture allocation failed");
			state.texture_width = width;
			state.texture_height = height;
		}
		auto *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		check(effect != nullptr, "OBS core default effect unavailable");
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), state.texture);
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(state.texture, 0, width, height);
	} catch (const std::exception &error) {
		state.failed = true;
		obs_log(LOG_ERROR, "Actual fixture source render failed: %s", error.what());
	}
}
void register_sources()
{
	obs_source_info source{};
	source.id = "gpu_image_native_fixture_source";
	source.type = OBS_SOURCE_TYPE_INPUT;
	source.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW;
	source.get_name = source_name;
	source.create = source_create;
	source.destroy = source_destroy;
	source.get_width = source_width;
	source.get_height = source_height;
	source.video_render = source_render;
	obs_register_source(&source);
	obs_source_info filter{};
	filter.id = "gpu_image_native_background_filter";
	filter.type = OBS_SOURCE_TYPE_FILTER;
	filter.output_flags = OBS_SOURCE_VIDEO;
	filter.get_name = background_filter_getname;
	filter.create = background_filter_create;
	filter.destroy = background_filter_destroy;
	filter.get_defaults = background_filter_defaults;
	filter.get_properties = background_filter_properties;
	filter.update = background_filter_update;
	filter.activate = background_filter_activate;
	filter.deactivate = background_filter_deactivate;
	filter.video_tick = background_filter_video_tick;
	filter.video_render = background_filter_video_render;
	obs_register_source(&filter);
}
std::shared_ptr<background_removal_filter> instance(obs_source_t *filter)
{
	auto *pointer = static_cast<std::shared_ptr<background_removal_filter> *>(obs_obj_get_data(filter));
	check(pointer && *pointer, "Actual filter creation did not publish an instance");
	return *pointer;
}
void draw(obs_source_t *source)
{
	GraphicsScope graphics;
	gs_texrender_t *output = gs_texrender_create(GS_BGRA, GS_ZS_NONE);
	check(output != nullptr, "Actual fixture output allocation failed");
	const auto width = obs_source_get_base_width(source), height = obs_source_get_base_height(source);
	if (!gs_texrender_begin(output, width, height)) {
		gs_texrender_destroy(output);
		throw std::runtime_error("Actual fixture output begin failed");
	}
	gs_begin_scene();
	gs_enable_depth_test(false);
	gs_set_cull_mode(GS_NEITHER);
	gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
	obs_source_video_render(source);
	gs_end_scene();
	gs_texrender_end(output);
	gs_texrender_destroy(output);
	check(!static_cast<SourceFixture *>(obs_obj_get_data(source))->failed, "Actual fixture source callback failed");
}
void require_checkbox_enabled(obs_source_t *filter, bool enabled)
{
	auto *properties = filter ? obs_source_properties(filter) : background_filter_properties(nullptr);
	check(properties != nullptr, "Properties unavailable for eligibility assertion");
	auto *checkbox = obs_properties_get(properties, "gpu_image_processing");
	const bool matches = checkbox && obs_property_enabled(checkbox) == enabled;
	obs_properties_destroy(properties);
	check(matches, "Processing checkbox enablement differs from completed-session eligibility");
}
void require_display_reuse(obs_source_t *filter, obs_source_t *source,
			   const std::shared_ptr<background_removal_filter> &tf)
{
	// Warm both owned slots, then count actual filter TU and helper allocations at fixed dimensions.
	process_frame(filter, source);
	process_frame(filter, source);
	process_frame(filter, source);
	unsigned before = 0;
	on_obs_thread([&] { before = gpu_test::counts().allocations; });
	for (unsigned i = 0; i < 3; ++i)
		process_frame(filter, source);
	on_obs_thread([&] {
		draw(source); // Display the just-published mask before tick can consume a wrongly queued refresh.
		check(gpu_test::counts().allocations == before,
		      "Steady-state actual filter display allocated a texture for each mask");
		GraphicsScope graphics;
		check(tf->imageDisplayTexture != nullptr, "Persistent accepted display missing");
		// A pending property refresh from the first Active transition is consumed by tick.
		if (tf->imagePipeline.processing_snapshot().mask_active)
			check(!tf->refreshImageProperties,
			      "Steady-state Active mask queued another properties refresh");
	});
}
void require_checkbox(obs_source_t *filter, obs_data_t *settings)
{
	obs_properties_t *properties = obs_source_properties(filter);
	check(properties != nullptr, "Actual filter properties unavailable");
	auto *checkbox = obs_properties_get(properties, "gpu_image_processing");
	if (!checkbox) {
		obs_properties_destroy(properties);
		throw std::runtime_error("Task4 expected RED: gpu_image_processing checkbox absent");
	}
	check(obs_property_get_type(checkbox) == OBS_PROPERTY_BOOL, "Processing control is not a checkbox");
	check(obs_property_visible(checkbox), "Processing checkbox hidden with advanced=false");
	check(obs_data_has_default_value(settings, "gpu_image_processing"),
	      "Processing checkbox lacks an explicit saved default");
	check(!obs_data_get_default_bool(settings, "gpu_image_processing"),
	      "Processing checkbox default must remain off");
	check(std::string(obs_property_description(checkbox)) == "GPU image processing",
	      "Actual en-US processing label missing");
	obs_properties_destroy(properties);
	gpu_filter_test::load_locale("pt-PT");
	properties = obs_source_properties(filter);
	check(properties != nullptr, "Actual Portuguese properties unavailable");
	checkbox = obs_properties_get(properties, "gpu_image_processing");
	check(checkbox && std::string(obs_property_description(checkbox)) == "Processamento de imagem na GPU",
	      "Actual pt-PT processing label missing");
	obs_properties_destroy(properties);
	gpu_filter_test::load_locale("en-US");
}
void require_processing_truth(const std::shared_ptr<background_removal_filter> &tf, bool requested)
{
	std::lock_guard modelLock(tf->modelMutex);
	std::lock_guard stateLock(tf->imageStateMutex);
	const auto snapshot = tf->imagePipeline.processing_snapshot();
	check(snapshot.requested == requested, "Saved processing request and immutable snapshot differ");
	if (tf->sessionDiagnostics.effective_provider != "DmlExecutionProvider")
		check(!snapshot.preprocess_active && !snapshot.mask_active &&
			      snapshot.state != gpu_image::ProcessingState::Active,
		      "Actual CPU fallback claimed active GPU image processing");
}
cv::Mat display_pixels(gs_texture_t *texture)
{
	check(texture && gs_texture_get_color_format(texture) == GS_BGRA, "Actual GPU display texture format invalid");
	const auto width = gs_texture_get_width(texture), height = gs_texture_get_height(texture);
	auto *stage = gs_stagesurface_create(width, height, GS_BGRA);
	check(stage != nullptr, "Actual display readback allocation failed");
	gs_stage_texture(stage, texture);
	uint8_t *data;
	uint32_t pitch;
	if (!gs_stagesurface_map(stage, &data, &pitch)) {
		gs_stagesurface_destroy(stage);
		throw std::runtime_error("Actual display readback map failed");
	}
	cv::Mat copy;
	try {
		copy = cv::Mat(static_cast<int>(height), static_cast<int>(width), CV_8UC4, data, pitch).clone();
	} catch (...) {
		gs_stagesurface_unmap(stage);
		gs_stagesurface_destroy(stage);
		throw;
	}
	gs_stagesurface_unmap(stage);
	gs_stagesurface_destroy(stage);
	return copy;
}
void require_telemetry(const std::shared_ptr<background_removal_filter> &tf)
{
	const auto initial = gpu_filter_test::captured_logs().size();
	// Fresh real five-second emission, never an artificial timer or made-up counter.
	const auto deadline = ProcessingClock::now() + std::chrono::seconds(7);
	std::string record;
	while (ProcessingClock::now() < deadline) {
		on_obs_thread([&] {
			auto retained = tf;
			background_filter_video_tick(&retained, 1.0f / 30.0f);
		});
		const auto logs = gpu_filter_test::captured_logs();
		for (size_t index = initial; index < logs.size(); ++index)
			if (logs[index].starts_with("GPUImageProcessing stats "))
				record = logs[index];
		if (!record.empty())
			break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	check(!record.empty(), "Fresh five-second stats emission missing");
	std::istringstream tokens(record);
	std::map<std::string, std::string> fields;
	std::string token;
	while (tokens >> token) {
		const auto split = token.find('=');
		if (split != std::string::npos)
			fields[token.substr(0, split)] = token.substr(split + 1);
	}
	for (const auto *key : {"processing_version",
				"filter_id",
				"settings_fingerprint",
				"source_fingerprint",
				"generation",
				"frame_id",
				"requested",
				"state",
				"effective_inference",
				"preprocess_active",
				"mask_active",
				"similarity_full_readback",
				"source_width",
				"source_height",
				"input_width",
				"input_height",
				"reason",
				"fps_num",
				"fps_den",
				"processed",
				"skipped",
				"stale",
				"input_readback_pixels",
				"similarity_readback_pixels",
				"capture_host_elapsed_ms",
				"inference_host_elapsed_ms",
				"mask_host_elapsed_ms",
				"interval_host_elapsed_ms",
				"obs_counters_available",
				"obs_rendered_frames",
				"obs_lagged_frames"})
		check(fields.contains(key) && !fields[key].empty(),
		      "Versioned telemetry has missing/unparseable required field");
	check(fields["processing_version"] == "1" && std::stoull(fields["filter_id"]) == tf->imageFilterId &&
		      std::stod(fields["interval_host_elapsed_ms"]) >= 5000.0,
	      "Telemetry version/lifetime/five-second interval invalid");
	check(fields["settings_fingerprint"].size() == 16 && fields["source_fingerprint"].size() == 16,
	      "Telemetry leaked raw settings/source identity instead of fingerprints");
	check(fields["fps_num"] == "30" && fields["fps_den"] == "1" && fields["obs_counters_available"] == "1",
	      "Telemetry FPS/OBS availability differs from actual fixture");
	std::cout << "filter-telemetry real-five-second version=1 parsed-FPS/counters/fingerprints PASS" << std::endl;
}
void require_mailbox_failure_recovery(obs_source_t *filter, obs_source_t *source, obs_data_t *settings,
				      const std::shared_ptr<background_removal_filter> &tf)
{
	const char *reasons[] = {"input-publication-failed", "mask-publication-failed", "input-packet-read-failed",
				 "mask-packet-read-failed"};
	for (unsigned operation = 0; operation < 4; ++operation) {
		obs_data_set_int(settings, "numThreads", 4 + operation);
		background_filter_update(obs_obj_get_data(filter), settings);
		process_frame(filter, source);
		process_frame(filter, source);
		cv::Mat accepted;
		Ort::Session *session = tf->session.get();
		on_obs_thread([&] {
			GraphicsScope graphics;
			check(tf->imagePipeline.processing_snapshot().mask_active,
			      "Actual eligible mask never became Active");
			accepted = display_pixels(tf->imageDisplayTexture);
		});
		unsigned cloneAllocations = 0;
		if (operation < 2) {
			// Observe the real allocation sequence for the identical callback route, then fail its final
			// byte-packet allocation. The asserted reason pins publication, not earlier preparation.
			on_obs_thread([&] {
				if (operation == 0) {
					gpu_test::MatAllocationScope count(-1, CV_8UC4);
					GraphicsScope graphics;
					uint32_t width, height;
					ImageRenderSettings render;
					check(image_capture(tf, width, height, render), "Capture counting failed");
					cloneAllocations = gpu_test::MatAllocationFault::allocations;
				} else {
					draw(source);
					gpu_test::MatAllocationScope count(-1, CV_8UC1);
					image_tick(tf);
					cloneAllocations = gpu_test::MatAllocationFault::allocations;
				}
			});
			check(cloneAllocations > 0, "No real publication allocation observed");
		}
		on_obs_thread([&] {
			if (operation == 1 || operation == 2)
				draw(source);
			// Refresh the exact accepted pixels after any counting/warmup rendering.
			{
				GraphicsScope graphics;
				accepted = display_pixels(tf->imageDisplayTexture);
			}
			{
				gpu_test::MatAllocationScope failure(
					operation < 2 ? static_cast<int>(cloneAllocations - 1) : 0,
					operation == 0 || operation == 2 ? CV_8UC4 : CV_8UC1);
				if (operation == 0) {
					GraphicsScope graphics;
					uint32_t width, height;
					ImageRenderSettings render;
					check(image_capture(tf, width, height, render),
					      "Publication failure aborted source render");
				} else if (operation == 1 || operation == 2) {
					image_tick(tf);
				} else {
					GraphicsScope graphics;
					check(image_display(tf) == tf->imageDisplayTexture,
					      "Mask clone failure exposed source instead of accepted display");
				}
			}
			GraphicsScope graphics;
			check(cv::norm(accepted, display_pixels(tf->imageDisplayTexture), cv::NORM_INF) == 0,
			      "Mailbox allocation failure overwrote accepted display pixels");
			const auto failed = tf->imagePipeline.processing_snapshot();
			check(failed.state == gpu_image::ProcessingState::CpuProcessingFallback && failed.requested &&
				      !failed.preprocess_active && !failed.mask_active &&
				      failed.reason == reasons[operation] &&
				      tf->imageEffectiveProvider == "DmlExecutionProvider" &&
				      tf->session.get() == session,
			      "Mailbox allocation failure did not latch truthful processing-only fallback");
			const auto generation = failed.generation;
			// Retry the reader under another clone fault. Fallback no longer consults the GPU mailbox.
			{
				gpu_test::MatAllocationScope failure;
				check(image_display(tf) == tf->imageDisplayTexture,
				      "Latched failure retried mailbox clone");
			}
			check(tf->imagePipeline.snapshot().generation == generation,
			      "Latched failure repeated transition");
		});
		process_frame(filter, source);
		check(tf->imagePipeline.processing_snapshot().state ==
			      gpu_image::ProcessingState::CpuProcessingFallback,
		      "Next callback retried failed GPU processing");
	}
	obs_data_set_int(settings, "numThreads", 8);
	background_filter_update(obs_obj_get_data(filter), settings);
	process_frame(filter, source);
	process_frame(filter, source);
	on_obs_thread([&] {
		GraphicsScope graphics;
		const auto config = tf->imagePipeline.snapshot();
		const auto accepted = display_pixels(tf->imageDisplayTexture);
		auto *acceptedTexture = tf->imageDisplayTexture;
		// Synthetic opposite mask isolates display ownership; session/provider diagnostics are untouched.
		const auto opposite = accepted.at<cv::Vec4b>(0, 0)[0] < 128 ? 255 : 0;
		gpu_image::MaskPacket replacement{
			{config.generation, tf->imageFrameId + 1, config.source, config.input},
			cv::Mat(static_cast<int>(config.input.height), static_cast<int>(config.input.width), CV_8UC1,
				cv::Scalar(opposite)),
			true};
		check(tf->imagePipeline.publish_mask(replacement), "Current opposite test mask rejected");
		display_copy_fault::after_copy = [&] {
			image_fail(*tf, config.generation, "copy-became-obsolete");
		};
		check(image_display(tf) == acceptedTexture && !display_copy_fault::after_copy,
		      "Obsolete candidate replaced accepted recovery display");
		check(cv::norm(accepted, display_pixels(acceptedTexture), cv::NORM_INF) == 0,
		      "Copy wrote over accepted pixels before generation validation");
		check(cv::norm(accepted, display_pixels(tf->imageCandidateTexture), cv::NORM_INF) > 0,
		      "Obsolete-candidate regression did not exercise different pixels");
	});
	// Restore by an explicit, real session initialization for subsequent helper fault cases.
	obs_data_set_int(settings, "numThreads", 1);
	background_filter_update(obs_obj_get_data(filter), settings);
	process_frame(filter, source);
	process_frame(filter, source);
	std::cout << "filter-mailbox real-allocation-fault/publication/read/cache/latch PASS" << std::endl;
}
void semantic_cases(obs_source_t *filter, obs_source_t *source, obs_data_t *settings,
		    const std::shared_ptr<background_removal_filter> &tf)
{
	obs_data_set_bool(settings, "gpu_image_processing", false);
	const auto offHash = image_settings_fingerprint(settings);
	obs_data_set_bool(settings, "gpu_image_processing", true);
	check(image_settings_fingerprint(settings) == offHash,
	      "Optimization checkbox contaminated comparison fingerprint");
	obs_data_set_bool(settings, "enable_image_similarity", false);
	obs_data_set_int(settings, "mask_every_x_frames", 1);
	background_filter_update(obs_obj_get_data(filter), settings);
	ObservedMediaPipe *observer;
	{
		std::lock_guard modelLock(tf->modelMutex);
		auto model = std::make_unique<ObservedMediaPipe>();
		observer = model.get();
		tf->model = std::move(model);
	}
	process_frame(filter, source);
	require_processing_truth(tf, true);
	// Real synchronous Session::Run is blocked while graphics resize publishes new authority.
	auto block = std::make_shared<BlockedRun>();
	auto entered = block->entered.get_future();
	{
		std::lock_guard modelLock(tf->modelMutex);
		observer->block = block;
	}
	uint64_t oldGeneration, staleBefore;
	gpu_image::FrameStamp oldStamp;
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		const auto config = tf->imagePipeline.snapshot();
		oldGeneration = config.generation;
		oldStamp = {config.generation, tf->imageFrameId + 1, config.source, config.input};
		staleBefore = tf->imageTelemetry.stale;
	}
	auto worker = std::async(std::launch::async, [&] { process_frame(filter, source); });
	if (entered.wait_for(std::chrono::seconds(10)) != std::future_status::ready) {
		block->release.set_value();
		worker.get();
		throw std::runtime_error("Real blocked Session::Run did not start");
	}
	try {
		auto *pixels = static_cast<SourceFixture *>(obs_obj_get_data(source));
		pixels->width = 641;
		pixels->height = 359;
		draw(source); // Graphics never waits for modelMutex held by blocked real inference.
		{
			std::lock_guard stateLock(tf->imageStateMutex);
			const auto config = tf->imagePipeline.snapshot();
			check(config.generation > oldGeneration && config.source == gpu_image::Dimensions{641, 359},
			      "Resize did not advance source authority while Run was blocked");
			gpu_image::MaskPacket late{oldStamp, cv::Mat(144, 256, CV_8UC1, cv::Scalar(0)), true};
			check(!tf->imagePipeline.publish_mask(late), "Source resize accepted an old-generation packet");
			check(!tf->legacyMask, "Source resize retained an old-source legacy mask");
		}
	} catch (...) {
		block->release.set_value();
		worker.get();
		throw;
	}
	block->release.set_value();
	worker.get();
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		check(tf->imageTelemetry.stale > staleBefore, "Blocked stale real output was not rejected");
	}
	process_frame(filter, source);
	// Full-image PSNR and uncontoured history do not advance on identical-image skips.
	obs_data_set_bool(settings, "enable_image_similarity", true);
	background_filter_update(obs_obj_get_data(filter), settings);
	process_frame(filter, source);
	cv::Mat history;
	uint64_t skipped, runs;
	{
		std::lock_guard modelLock(tf->modelMutex);
		runs = observer->completed_runs;
		std::lock_guard stateLock(tf->imageStateMutex);
		history = tf->imageHistory.clone();
		skipped = tf->imageTelemetry.skipped;
	}
	process_frame(filter, source);
	{
		std::lock_guard modelLock(tf->modelMutex);
		check(observer->completed_runs == runs, "Similarity skip executed another real inference");
		std::lock_guard stateLock(tf->imageStateMutex);
		check(tf->imageTelemetry.skipped > skipped && !history.empty() &&
			      cv::norm(history, tf->imageHistory, cv::NORM_INF) == 0.0,
		      "Similarity skip advanced temporal history");
		check(tf->imagePipeline.snapshot().image_similarity &&
			      obs_data_get_bool(settings, "enable_image_similarity"),
		      "Full-image compatibility rewrote the saved similarity setting");
		if (tf->imageEffectiveProvider == "DmlExecutionProvider")
			check(tf->imagePipeline.processing_snapshot().similarity_full_readback,
			      "Active similarity route failed to disclose full-image readback");
	}
	obs_data_set_bool(settings, "enable_image_similarity", false);
	obs_data_set_int(settings, "mask_every_x_frames", 3);
	background_filter_update(obs_obj_get_data(filter), settings);
	// Two intentional skips, then one real mask, then reuse of that accepted current-generation mask.
	process_frame(filter, source);
	process_frame(filter, source);
	process_frame(filter, source);
	{
		std::lock_guard modelLock(tf->modelMutex);
		runs = observer->completed_runs;
		std::lock_guard stateLock(tf->imageStateMutex);
		history = tf->imageHistory.clone();
	}
	process_frame(filter, source);
	{
		std::lock_guard modelLock(tf->modelMutex);
		check(observer->completed_runs == runs && observer->maximum_runs == 1,
		      "Mask-every-X reused frame ran inference or concurrent Run occurred");
		std::lock_guard stateLock(tf->imageStateMutex);
		check(!history.empty() && cv::norm(history, tf->imageHistory, cv::NORM_INF) == 0.0,
		      "Mask-every-X skip changed temporal history");
	}
	bool eligible;
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		eligible = tf->imageEffectiveProvider == "DmlExecutionProvider";
	}
	if (eligible) {
		obs_data_set_int(settings, "mask_every_x_frames", 1);
		background_filter_update(obs_obj_get_data(filter), settings);
		process_frame(filter, source);
		process_frame(filter, source);
		require_display_reuse(filter, source, tf);
		require_mailbox_failure_recovery(filter, source, settings, tf);
		cv::Mat acceptedDisplay;
		on_obs_thread([&] {
			GraphicsScope graphics;
			acceptedDisplay = display_pixels(tf->imageDisplayTexture);
			tf->imageMaskProcessor.release(); // Borrowed helper output is now invalid.
			check(cv::norm(acceptedDisplay, display_pixels(tf->imageDisplayTexture), cv::NORM_INF) == 0.0,
			      "Display cache retained a borrowed helper texture");
		});
		Ort::Session *session;
		{
			std::lock_guard modelLock(tf->modelMutex);
			session = tf->session.get();
		}
		// The borrowed helper result must not be the recovery display cache.
		on_obs_thread([&] {
			GraphicsScope graphics;
			check(tf->imageDisplayTexture != nullptr, "Accepted independent display copy missing");
			gpu_test::fail_next_upload_map();
		});
		process_frame(filter, source);
		on_obs_thread([&] {
			GraphicsScope graphics;
			check(cv::norm(acceptedDisplay, display_pixels(tf->imageDisplayTexture), cv::NORM_INF) == 0.0,
			      "Processing recovery lost the independently owned accepted display");
		});
		{
			std::lock_guard modelLock(tf->modelMutex);
			check(tf->session.get() == session, "Processing failure recreated DirectML session");
			std::lock_guard stateLock(tf->imageStateMutex);
			const auto snapshot = tf->imagePipeline.processing_snapshot();
			check(snapshot.state == gpu_image::ProcessingState::CpuProcessingFallback &&
				      !snapshot.preprocess_active && !snapshot.mask_active && snapshot.requested,
			      "Mask map failure did not disable both stages truthfully");
		}
		process_frame(filter, source);
		{
			std::lock_guard stateLock(tf->imageStateMutex);
			check(tf->legacyMask && !tf->legacyMask->mask.empty(),
			      "Processing fallback did not finish a legacy CPU mask outside the mailbox");
		}
		// Checkbox changes alone neither clear the failure latch nor recreate inference.
		obs_data_set_bool(settings, "gpu_image_processing", false);
		background_filter_update(obs_obj_get_data(filter), settings);
		obs_data_set_bool(settings, "gpu_image_processing", true);
		background_filter_update(obs_obj_get_data(filter), settings);
		process_frame(filter, source);
		{
			std::lock_guard modelLock(tf->modelMutex);
			check(tf->session.get() == session, "Failure checkbox toggle recreated valid inference");
		}
		{
			std::lock_guard stateLock(tf->imageStateMutex);
			check(tf->imagePipeline.processing_snapshot().state ==
				      gpu_image::ProcessingState::CpuProcessingFallback,
			      "Automatic per-frame processing retry cleared failure latch");
		}
		// Explicit inference reinitialization is the sole retry path.
		obs_data_set_int(settings, "numThreads", 2);
		background_filter_update(obs_obj_get_data(filter), settings);
		on_obs_thread([&] {
			GraphicsScope graphics;
			gpu_test::fail_next_map();
			draw(source);
		});
		{
			std::lock_guard stateLock(tf->imageStateMutex);
			check(tf->imagePipeline.processing_snapshot().state ==
				      gpu_image::ProcessingState::CpuProcessingFallback,
			      "Input map failure did not enter bounded CPU processing recovery");
		}
		obs_data_set_int(settings, "numThreads", 3);
		background_filter_update(obs_obj_get_data(filter), settings);
		on_obs_thread([&] {
			GraphicsScope graphics;
			tf->imagePreprocessor.release();
			gpu_test::fail_next_staging_allocation();
			draw(source);
		});
		{
			std::lock_guard stateLock(tf->imageStateMutex);
			check(tf->imagePipeline.processing_snapshot().state ==
				      gpu_image::ProcessingState::CpuProcessingFallback,
			      "GPU preparation allocation failure did not retain bounded CPU processing fallback");
		}
		std::cout << "filter-GPU-eligible actual-input/mask-failure/latch/recovery PASS" << std::endl;
	} else {
		std::cout
			<< "filter-GPU-eligible failure-injection/active-stage-integration=hardware-pending actual-provider=CPUExecutionProvider"
			<< std::endl;
	}
	std::cout << "filter-semantics actual-blocked-Run-resize/stale-rejection/similarity-history/mask-X PASS"
		  << std::endl;
}
void run_lifetimes()
{
	unsigned boundaries = 0;
	for (unsigned cycle = 0; cycle < 4; ++cycle) {
		std::cout << "filter-case cycle=" << cycle << " create begin" << std::endl;
		obs_source_t *source = nullptr;
		obs_source_t *filter = nullptr;
		obs_data_t *settings = nullptr;
		bool attached = false;
		try {
			source = obs_source_create_private("gpu_image_native_fixture_source", "native source", nullptr);
			check(source != nullptr, "Actual fixture source creation failed");
			settings = obs_data_create();
			check(settings != nullptr, "Actual settings allocation failed");
			background_filter_defaults(settings);
			obs_data_set_bool(settings, "stop_when_source_is_inactive", false);
			obs_data_set_bool(settings, "enable_image_similarity", false);
			filter = obs_source_create_private("gpu_image_native_background_filter",
							   "native background filter", settings);
			check(filter != nullptr, "Actual background filter source creation failed");
			obs_source_filter_add(source, filter);
			attached = true;
			auto tf = instance(filter);
			ObservedMediaPipe *observer = nullptr;
			{
				std::lock_guard lock(tf->modelMutex);
				check(tf->session &&
					      tf->sessionDiagnostics.outcome == windows_ml::SessionOutcome::Ready,
				      "Actual MediaPipe CPU adapter session did not become Ready");
				check(tf->sessionDiagnostics.effective_provider == "CPUExecutionProvider",
				      "CPU baseline provider inaccurate");
				check(dynamic_cast<ModelMediaPipe *>(tf->model.get()) != nullptr,
				      "Actual baseline model is not MediaPipe");
				auto observing_model = std::make_unique<ObservedMediaPipe>();
				observer = observing_model.get();
				tf->model = std::move(observing_model);
			}
			std::cout
				<< "filter-baseline actual-OBS-startup=ready actual-CPU-session=ready actual-filter-render begin"
				<< std::endl;
			const auto errors_before = gpu_filter_test::error_count();
			tick_baseline_on_obs_thread(obs_obj_get_data(filter), source);
			uint64_t completed_runs, completed_outputs;
			cv::Mat actual_raw_output, actual_mask;
			gpu_image::Dimensions output_dimensions;
			gpu_image::MaskSettings mask_settings;
			{
				std::lock_guard lock(tf->modelMutex);
				completed_runs = observer->completed_runs;
				completed_outputs = observer->completed_outputs;
				actual_raw_output = observer->completed_output.clone();
				output_dimensions = {static_cast<uint32_t>(tf->outputDims.at(0).at(2)),
						     static_cast<uint32_t>(tf->outputDims.at(0).at(1))};
				mask_settings = {tf->enableThreshold, tf->threshold,     tf->temporalSmoothFactor,
						 tf->contourFilter,   tf->smoothContour, tf->feather,
						 tf->maskExpansion};
			}
			{
				std::lock_guard lock(tf->outputLock);
				actual_mask = tf->backgroundMask.clone();
			}
			require_completed_cpu_mask(completed_runs, completed_outputs, actual_raw_output, actual_mask,
						   output_dimensions, {321, 181}, mask_settings);
			check(gpu_filter_test::error_count() == errors_before,
			      "Actual CPU baseline emitted a plugin error");
			std::cout << "filter-baseline actual-filter-render=completed actual-CPU-inference=completed"
				  << std::endl;
			require_checkbox(filter, settings);
			require_checkbox_enabled(nullptr, false);
			require_checkbox_enabled(filter, false);
			require_display_reuse(filter, source, tf);
			// Attempt the actual provider route; never overwrite SessionDiagnostics.
			obs_data_set_string(settings, "useGPU", USEGPU_WINML_DIRECTML);
			background_filter_update(obs_obj_get_data(filter), settings);
			Ort::Session *original_session = nullptr;
			{
				std::lock_guard lock(tf->modelMutex);
				check(tf->session &&
					      tf->sessionDiagnostics.outcome == windows_ml::SessionOutcome::Ready,
				      "Actual DirectML request and permitted CPU fallback both failed");
				original_session = tf->session.get();
				std::cout << "filter-provider requested=" << tf->sessionDiagnostics.requested_provider
					  << " effective=" << tf->sessionDiagnostics.effective_provider << std::endl;
				if (tf->sessionDiagnostics.effective_provider != "DmlExecutionProvider")
					std::cout
						<< "filter-eligible-GPU coverage=hardware-pending reason=actual-provider-unavailable"
						<< std::endl;
			}
			require_checkbox_enabled(filter,
						 tf->sessionDiagnostics.effective_provider == "DmlExecutionProvider");
			if (cycle == 0)
				semantic_cases(filter, source, settings, tf);
			// Keep each mixed boundary on the actual OBS video thread.
			obs_data_set_int(settings, "mask_every_x_frames", 1);
			obs_data_set_bool(settings, "enable_image_similarity", false);
			{
				std::lock_guard modelLock(tf->modelMutex);
				original_session = tf->session.get();
			}
			for (unsigned step = 0; step < 16; ++step) {
				std::cout << "filter-boundary cycle=" << cycle << " step=" << step << std::endl;
				obs_data_set_bool(settings, "gpu_image_processing", (step % 2) != 0);
				background_filter_update(obs_obj_get_data(filter), settings);
				{
					std::lock_guard lock(tf->modelMutex);
					check(tf->session.get() == original_session,
					      "Checkbox toggle recreated the inference session");
				}
				auto *pixels = static_cast<SourceFixture *>(obs_obj_get_data(source));
				pixels->width = step % 2 ? 641 : 321;
				pixels->height = step % 2 ? 359 : 181;
				process_frame(filter, source);
				require_processing_truth(tf, (step % 2) != 0);
				require_checkbox_enabled(filter, tf->sessionDiagnostics.effective_provider ==
									 "DmlExecutionProvider");
				check(obs_data_get_bool(settings, "gpu_image_processing") == ((step % 2) != 0),
				      "Saved request was rewritten");
				++boundaries;
			}
			if (cycle == 0)
				require_telemetry(tf);
			obs_data_set_string(settings, "useGPU", USEGPU_CPU);
			background_filter_update(obs_obj_get_data(filter), settings);
			{
				std::lock_guard lock(tf->modelMutex);
				check(tf->sessionDiagnostics.effective_provider == "CPUExecutionProvider",
				      "Switch back to CPU left stale GPU status");
			}
			require_checkbox_enabled(filter, false);
			obs_source_filter_remove(source, filter);
			attached = false;
			obs_source_release(filter);
			filter = nullptr;
			check(obs_wait_for_destroy_queue(), "OBS destroy queue did not drain");
			{
				std::lock_guard stateLock(tf->imageStateMutex);
				const auto config = tf->imagePipeline.snapshot();
				check(tf->imageTerminal, "Removal did not invalidate legacy callback authority");
				gpu_image::MaskPacket late{{config.generation, tf->imageFrameId + 1, config.source,
							    config.input},
							   cv::Mat(144, 256, CV_8UC1, cv::Scalar(0)),
							   true};
				check(!tf->imagePipeline.publish_mask(late),
				      "Terminal same-generation pipeline accepted a late mask");
			}
			check(tf->isDisabled, "Removal did not disable the retained callback lifetime");
			// A retained shared lifetime is valid; an OBS-owned void* deleted at destroy is not.
			background_filter_video_tick(&tf, 1.0f / 30.0f);
			{
				GraphicsScope graphics;
				background_filter_video_render(&tf, nullptr);
			}
			check(tf->isDisabled, "Late retained callback revived a removed filter");
		} catch (...) {
			if (filter) {
				if (attached)
					obs_source_filter_remove(source, filter);
				obs_source_release(filter);
				obs_wait_for_destroy_queue();
			}
			obs_source_release(source);
			obs_data_release(settings);
			throw;
		}
		obs_source_release(source);
		obs_data_release(settings);
		obs_wait_for_destroy_queue();
	}
	check(boundaries >= 64, "Mixed callback lifecycle boundary count below 64");
	std::cout << "filter-lifecycle boundaries=" << boundaries << " actual-callbacks PASS" << std::endl;
}
} // namespace

int main(int argc, char **argv)
{
	bool started = false;
	try {
		check(argc == 5 && std::string(argv[1]) == "--effect-root" && std::string(argv[3]) == "--model",
		      "Explicit --effect-root and --model required");
		gpu_filter_test::initialize_paths(argv[2], argv[4]);
		gpu_filter_test::load_locale("en-US");
		check(obs_startup("en-US", nullptr, nullptr), "Actual OBS startup failed");
		started = true;
		const auto module = std::filesystem::absolute(argv[0]).parent_path() / "libobs-d3d11.dll";
		const auto module_name = module.string();
		obs_video_info video{};
		video.graphics_module = module_name.c_str();
		video.fps_num = 30;
		video.fps_den = 1;
		video.base_width = video.output_width = 640;
		video.base_height = video.output_height = 360;
		video.output_format = VIDEO_FORMAT_RGBA;
		video.colorspace = VIDEO_CS_709;
		video.range = VIDEO_RANGE_FULL;
		video.scale_type = OBS_SCALE_BILINEAR;
		std::cout << "filter-core graphics=" << module_name << " reset begin" << std::endl;
		check(obs_reset_video(&video) == OBS_VIDEO_SUCCESS, "Actual OBS graphics/video initialization failed");
		register_sources();
		raw_output_regression();
		initialized_mask_is_not_completion();
		run_lifetimes();
		obs_shutdown();
		started = false;
		gpu_filter_test::release_locale();
		return 0;
	} catch (const std::exception &error) {
		if (started)
			obs_shutdown();
		gpu_filter_test::release_locale();
		std::cerr << "filter-native FAIL: " << error.what() << std::endl;
		return 1;
	}
}

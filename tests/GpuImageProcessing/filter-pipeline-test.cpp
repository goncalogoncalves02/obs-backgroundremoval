// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filter-boundaries.hpp"
// Execute the actual callback TU and inspect its existing owned lifetime/model.
// No production hooks, fabricated sessions or graphics replacements are added.
#include "background-filter.cpp"
#include "obs-utils/background-mask-cpu.hpp"
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
bool valid_probability_output(const cv::Mat &output)
{
	if (output.empty() || output.dims != 2 || output.type() != CV_32FC1 || !cv::checkRange(output))
		return false;
	double minimum, maximum;
	cv::minMaxLoc(output, &minimum, &maximum);
	// checkRange's max is exclusive and cast to float for CV_32F. Keep finite
	// validation separate, then use exact inclusive bounds without a tolerance.
	return minimum >= 0.0 && maximum <= 1.0;
}
void probability_endpoint_regression()
{
	for (float value : {0.0f, -0.0f, 1.0f})
		check(valid_probability_output(cv::Mat(1, 1, CV_32FC1, cv::Scalar(value))),
		      "Exact inclusive probability endpoint was rejected");
	for (float value : {std::nextafter(1.0f, std::numeric_limits<float>::infinity()), -0.25f,
			    std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
			    -std::numeric_limits<float>::infinity()})
		check(!valid_probability_output(cv::Mat(1, 1, CV_32FC1, cv::Scalar(value))),
		      "Non-finite or out-of-range probability was accepted");
	std::cout << "filter-baseline probability-regression endpoints=inclusive nonfinite/outside=rejected"
		  << std::endl;
}
// A test-only observer of the real MediaPipe implementation. Every operation
// delegates unchanged base behavior; no synthetic Session, tensors or success.
// The actual adapter invokes both methods while holding modelMutex. All fixture
// reads use that same mutex, including reads alongside the OBS video tick thread.
struct ObservedMediaPipe final : ModelMediaPipe {
	uint64_t completed_runs = 0, completed_outputs = 0;
	cv::Mat completed_output;
	bool run_completed = false;

	void runNetworkInference(const std::unique_ptr<Ort::Session> &session,
				 const std::vector<Ort::AllocatedStringPtr> &inputNames,
				 const std::vector<Ort::AllocatedStringPtr> &outputNames,
				 const std::vector<Ort::Value> &inputTensor,
				 std::vector<Ort::Value> &outputTensor) override
	{
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
		const bool valid = valid_probability_output(output);
		if (completed_outputs == 0 || !valid) {
			if (!output.empty() && output.dims == 2 && output.type() == CV_32FC1) {
				double minimum, maximum;
				cv::minMaxLoc(output, &minimum, &maximum);
				obs_log(LOG_INFO,
					"Actual CPU MediaPipe output width=%d height=%d finite=%s min=%.9g max=%.9g valid=%s",
					output.cols, output.rows, cv::checkRange(output) ? "true" : "false", minimum,
					maximum, valid ? "true" : "false");
			} else {
				obs_log(LOG_INFO, "Actual CPU MediaPipe output dims=%d type=%d empty=%s valid=false",
					output.dims, output.type(), output.empty() ? "true" : "false");
			}
		}
		check(valid, "Actual MediaPipe output must be finite single-channel probabilities in [0,1]");
		completed_output = output.clone();
		++completed_outputs;
		run_completed = false;
	}
};
void require_completed_cpu_mask(uint64_t completed_runs, uint64_t completed_outputs, const cv::Mat &probabilities,
				const cv::Mat &displayed, gpu_image::Dimensions output, gpu_image::Dimensions source,
				const gpu_image::MaskSettings &settings)
{
	check(completed_runs > 0 && completed_outputs == completed_runs,
	      "Actual CPU Session::Run and delegated MediaPipe postprocess did not both complete");
	check(!probabilities.empty() && probabilities.type() == CV_32FC1 &&
		      probabilities.cols == static_cast<int>(output.width) &&
		      probabilities.rows == static_cast<int>(output.height),
	      "Completed actual output differs from validated model dimensions");
	check(!displayed.empty() && displayed.type() == CV_8UC1 && displayed.cols == static_cast<int>(source.width) &&
		      displayed.rows == static_cast<int>(source.height),
	      "Actual CPU display mask has invalid type or source dimensions");
	cv::Mat bytes;
	probabilities.convertTo(bytes, CV_8U, 255.0);
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
			cv::Mat actual_probabilities, actual_mask;
			gpu_image::Dimensions output_dimensions;
			gpu_image::MaskSettings mask_settings;
			{
				std::lock_guard lock(tf->modelMutex);
				completed_runs = observer->completed_runs;
				completed_outputs = observer->completed_outputs;
				actual_probabilities = observer->completed_output.clone();
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
			require_completed_cpu_mask(completed_runs, completed_outputs, actual_probabilities, actual_mask,
						   output_dimensions, {321, 181}, mask_settings);
			check(gpu_filter_test::error_count() == errors_before,
			      "Actual CPU baseline emitted a plugin error");
			std::cout << "filter-baseline actual-filter-render=completed actual-CPU-inference=completed"
				  << std::endl;
			require_checkbox(filter, settings);
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
				draw(source);
				background_filter_video_tick(obs_obj_get_data(filter), 1.0f / 30.0f);
				draw(source);
				check(obs_data_get_bool(settings, "gpu_image_processing") == ((step % 2) != 0),
				      "Saved request was rewritten");
				++boundaries;
			}
			obs_data_set_string(settings, "useGPU", USEGPU_CPU);
			background_filter_update(obs_obj_get_data(filter), settings);
			{
				std::lock_guard lock(tf->modelMutex);
				check(tf->sessionDiagnostics.effective_provider == "CPUExecutionProvider",
				      "Switch back to CPU left stale GPU status");
			}
			obs_source_filter_remove(source, filter);
			attached = false;
			obs_source_release(filter);
			filter = nullptr;
			check(obs_wait_for_destroy_queue(), "OBS destroy queue did not drain");
			check(tf->isDisabled, "Removal did not disable the retained callback lifetime");
			// A retained shared lifetime is valid; an OBS-owned void* deleted at destroy is not.
			background_filter_video_tick(&tf, 1.0f / 30.0f);
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
		probability_endpoint_regression();
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

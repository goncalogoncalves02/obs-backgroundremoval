// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filter-boundaries.hpp"
// Execute the actual callback TU and inspect its existing owned lifetime/model.
// No production hooks, fabricated sessions or graphics replacements are added.
#include "background-filter.cpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool condition, const char *message)
{
	if (!condition)
		throw std::runtime_error(message);
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
			{
				std::lock_guard lock(tf->modelMutex);
				check(tf->session &&
					      tf->sessionDiagnostics.outcome == windows_ml::SessionOutcome::Ready,
				      "Actual MediaPipe CPU adapter session did not become Ready");
				check(tf->sessionDiagnostics.effective_provider == "CPUExecutionProvider",
				      "CPU baseline provider inaccurate");
			}
			std::cout
				<< "filter-baseline actual-OBS-startup=ready actual-CPU-session=ready actual-filter-render begin"
				<< std::endl;
			draw(source);
			{
				std::lock_guard lock(tf->inputBGRALock);
				check(!tf->inputBGRA.empty(), "Actual baseline filter did not capture source pixels");
			}
			background_filter_video_tick(obs_obj_get_data(filter), 1.0f / 30.0f);
			{
				std::lock_guard lock(tf->outputLock);
				check(!tf->backgroundMask.empty(), "Actual CPU inference did not produce a mask");
			}
			draw(source);
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

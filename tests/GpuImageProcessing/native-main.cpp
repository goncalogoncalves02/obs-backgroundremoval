// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <graphics/graphics.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

void run_input_cases(const std::filesystem::path &effect_root);
void run_mask_cases(const std::filesystem::path &effect_root);

static bool print_adapter(void *, const char *name, uint32_t id)
{
	if (id == 0)
		std::cout << "graphics-adapter index=" << id << " name=" << name << std::endl;
	return true;
}

int main(int argc, char **argv)
{
	graphics_t *graphics = nullptr;
	bool entered = false;
	bool scene_started = false;
	try {
		std::filesystem::path effect_root, model;
		for (int i = 1; i < argc; i += 2) {
			if (i + 1 >= argc)
				throw std::runtime_error("Missing option value");
			if (std::string(argv[i]) == "--effect-root")
				effect_root = argv[i + 1];
			else if (std::string(argv[i]) == "--model")
				model = argv[i + 1];
			else
				throw std::runtime_error("Unknown option");
		}
		if (!std::filesystem::is_directory(effect_root) || !std::filesystem::is_regular_file(model))
			throw std::runtime_error("Explicit effect root and MediaPipe model are required");
		const auto module = std::filesystem::absolute(argv[0]).parent_path() / "libobs-d3d11.dll";
		std::cout << "graphics-create module=" << module.string() << std::endl;
		const int status = gs_create(&graphics, module.string().c_str(), 0);
		if (status != GS_SUCCESS)
			throw std::runtime_error("Graphics backend unavailable: gs_create status=" +
						 std::to_string(status));
		gs_enter_context(graphics);
		entered = true;
		std::cout << "graphics-ready backend=" << gs_get_device_name() << std::endl;
		gs_enum_adapters(print_adapter, nullptr);
		const auto initial_cull = gs_get_cull_mode();
		std::cout << "graphics-draw-setup initial-cull=" << static_cast<int>(initial_cull)
			  << " desired-cull=" << static_cast<int>(GS_NEITHER) << std::endl;
		// Match pinned obs-video.c render_video(): gs_create alone is not an OBS 2D scene.
		gs_begin_scene();
		scene_started = true;
		gs_enable_depth_test(false);
		gs_set_cull_mode(GS_NEITHER);
		if (gs_get_cull_mode() != GS_NEITHER)
			throw std::runtime_error("Native fixture could not establish OBS 2D draw state");
		run_input_cases(effect_root);
		run_mask_cases(effect_root);
		gs_end_scene();
		scene_started = false;
		gs_leave_context();
		entered = false;
		gs_destroy(graphics);
		std::cout << "gpu-input-cases PASS" << std::endl;
		return 0;
	} catch (const std::exception &error) {
		if (scene_started)
			gs_end_scene();
		if (entered)
			gs_leave_context();
		if (graphics)
			gs_destroy(graphics);
		std::cerr << "gpu-input-cases FAIL: " << error.what() << std::endl;
		return 1;
	}
}

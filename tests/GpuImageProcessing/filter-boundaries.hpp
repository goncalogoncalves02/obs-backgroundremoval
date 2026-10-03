// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <obs-module.h>
#include <filesystem>
#include <string>
#include <vector>

namespace gpu_filter_test {
void initialize_paths(const std::filesystem::path &effect_root, const std::filesystem::path &model);
void load_locale(const char *locale);
void release_locale();
std::vector<std::string> captured_logs();
uint64_t error_count();
char *module_file(const char *file);
} // namespace gpu_filter_test

// Path resolution boundary only: actual model/session/graphics calls remain untouched.
#undef obs_module_file
#define obs_module_file(file) gpu_filter_test::module_file(file)

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "filter-boundaries.hpp"
#include "UI/AboutDialogIntegration.hpp"
#include "UpdateConfig/UpdateConfig.hpp"
#include "plugin-support.h"
#include <util/bmem.h>
#include <util/text-lookup.h>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <stdexcept>

namespace {
std::filesystem::path effect_root, model_path;
lookup_t *locale_lookup = nullptr;
std::mutex log_mutex;
std::vector<std::string> logs;
} // namespace

const char *PLUGIN_NAME = "obs-backgroundremoval";
const char *PLUGIN_VERSION = "1.4.1";

extern "C" void obs_log(int, const char *format, ...)
{
	std::array<char, 16384> buffer{};
	va_list args;
	va_start(args, format);
	vsnprintf(buffer.data(), buffer.size(), format, args);
	va_end(args);
	std::lock_guard lock(log_mutex);
	logs.emplace_back(buffer.data());
	std::fprintf(stderr, "[filter-native] %s\n", buffer.data());
	std::fflush(stderr);
}

extern "C" obs_module_t *obs_current_module(void)
{
	return nullptr;
}

extern "C" const char *obs_module_text(const char *key)
{
	const char *translated = key;
	if (locale_lookup)
		text_lookup_getstr(locale_lookup, key, &translated);
	return translated;
}

namespace gpu_filter_test {
void initialize_paths(const std::filesystem::path &effects, const std::filesystem::path &model)
{
	effect_root = std::filesystem::canonical(effects);
	model_path = std::filesystem::canonical(model);
}
void load_locale(const char *locale)
{
	release_locale();
	const auto directory = effect_root.parent_path() / "locale";
	locale_lookup = text_lookup_create((directory / "en-US.ini").string().c_str());
	if (!locale_lookup)
		throw std::runtime_error("Cannot load actual en-US locale");
	if (std::string(locale) != "en-US" &&
	    !text_lookup_add(locale_lookup, (directory / (std::string(locale) + ".ini")).string().c_str()))
		throw std::runtime_error("Cannot load actual requested locale");
}
void release_locale()
{
	if (locale_lookup) {
		text_lookup_destroy(locale_lookup);
		locale_lookup = nullptr;
	}
}
std::vector<std::string> captured_logs()
{
	std::lock_guard lock(log_mutex);
	return logs;
}
char *module_file(const char *file)
{
	if (!file)
		return nullptr;
	std::filesystem::path path;
	if (std::string(file) == "models/mediapipe.onnx")
		path = model_path;
	else if (std::string(file).starts_with("effects/"))
		path = effect_root / std::filesystem::path(file).filename();
	else
		return nullptr;
	if (!std::filesystem::is_regular_file(path))
		return nullptr;
	const auto utf8 = path.u8string();
	return bstrdup(reinterpret_cast<const char *>(utf8.c_str()));
}
} // namespace gpu_filter_test

// Explicitly authorized test boundaries: these unchanged about/update operations
// do not take part in filter processing. Actual OBS properties remain real.
namespace AboutDialogIntegration {
void addButton(obs_properties_t *properties)
{
	obs_properties_add_button2(
		properties, "about_obs_backgroundremoval", obs_module_text("About"),
		[](obs_properties_t *, obs_property_t *, void *) { return false; }, nullptr);
}
} // namespace AboutDialogIntegration
namespace UpdateConfig {
std::optional<std::string> getLatestVersion()
{
	return std::nullopt;
}
} // namespace UpdateConfig

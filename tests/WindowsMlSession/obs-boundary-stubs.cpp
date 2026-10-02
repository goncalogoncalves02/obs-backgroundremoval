// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <obs-module.h>
#include "plugin-support.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

std::vector<std::string> captured_logs;
extern "C" void obs_log(int, const char *format, ...)
{
	std::array<char, 8192> buffer{};
	va_list args;
	va_start(args, format);
	vsnprintf(buffer.data(), buffer.size(), format, args);
	va_end(args);
	captured_logs.emplace_back(buffer.data());
}
extern "C" obs_module_t *obs_current_module(void)
{
	return nullptr;
}

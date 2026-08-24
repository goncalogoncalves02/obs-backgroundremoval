// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace windows_ml_smoke {

inline constexpr std::string_view kUsage = "usage=windows-ml-smoke (--provider cpu --model <path> | --list-providers | "
					   "--prepare-provider <exact-provider-name>)";

enum class CliCommand {
	CpuInference,
	ListProviders,
	PrepareProvider,
};

struct CliOptions {
	CliCommand command;
	std::string model_path;
	std::string provider_name;
};

struct ParseResult {
	std::optional<CliOptions> options;
	std::string error;

	[[nodiscard]] bool ok() const noexcept { return options.has_value(); }
};

[[nodiscard]] ParseResult parse_cli(std::span<const std::string_view> arguments);

} // namespace windows_ml_smoke

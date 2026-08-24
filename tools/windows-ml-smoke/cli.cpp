// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#include "cli.hpp"

namespace windows_ml_smoke {

ParseResult parse_cli(std::span<const std::string_view> arguments)
{
	std::optional<std::string_view> provider;
	std::optional<std::string_view> model_path;

	for (std::size_t index = 0; index < arguments.size();) {
		const auto option = arguments[index];
		if (option != "--provider" && option != "--model") {
			return {.options = std::nullopt, .error = "unknown option: " + std::string(option)};
		}

		if (option == "--provider" && provider.has_value()) {
			return {.options = std::nullopt, .error = "duplicate option: --provider"};
		}
		if (option == "--model" && model_path.has_value()) {
			return {.options = std::nullopt, .error = "duplicate option: --model"};
		}

		if (index + 1 >= arguments.size()) {
			const auto required = option == "--provider" ? "--provider cpu is required"
								     : "--model <path> is required";
			return {.options = std::nullopt, .error = required};
		}

		const auto value = arguments[index + 1];
		if (option == "--provider") {
			provider = value;
		} else {
			model_path = value;
		}
		index += 2;
	}

	if (!provider.has_value()) {
		return {.options = std::nullopt, .error = "--provider cpu is required"};
	}
	if (*provider != "cpu") {
		return {.options = std::nullopt,
			.error = "unsupported provider: " + std::string(*provider) + " (only cpu is supported)"};
	}
	if (!model_path.has_value() || model_path->empty()) {
		return {.options = std::nullopt, .error = "--model <path> is required"};
	}

	return {.options = CliOptions{.model_path = std::string(*model_path)}, .error = {}};
}

} // namespace windows_ml_smoke

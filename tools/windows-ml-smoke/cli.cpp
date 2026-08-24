// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli.hpp"

namespace windows_ml_smoke {

ParseResult parse_cli(std::span<const std::string_view> arguments)
{
	std::optional<std::string_view> provider;
	std::optional<std::string_view> model_path;
	std::optional<std::string_view> provider_name;
	std::optional<CliCommand> command;

	const auto select_command = [&](CliCommand selected, std::string_view option) -> std::optional<ParseResult> {
		if (!command.has_value()) {
			command = selected;
			return std::nullopt;
		}
		if (*command == selected) {
			return ParseResult{.options = std::nullopt,
					   .error = "duplicate command option: " + std::string(option)};
		}
		return ParseResult{.options = std::nullopt, .error = "multiple commands are not allowed"};
	};

	for (std::size_t index = 0; index < arguments.size();) {
		const auto option = arguments[index];
		if (option == "--list-providers") {
			if (const auto error = select_command(CliCommand::ListProviders, option)) {
				return *error;
			}
			++index;
			continue;
		}

		if (option == "--prepare-provider") {
			if (command == CliCommand::PrepareProvider) {
				return {.options = std::nullopt,
					.error = "duplicate command option: --prepare-provider"};
			}
			if (index + 1 >= arguments.size() || arguments[index + 1].empty() ||
			    arguments[index + 1].starts_with("--")) {
				return {.options = std::nullopt,
					.error = "--prepare-provider <exact-provider-name> is required"};
			}
			if (const auto error = select_command(CliCommand::PrepareProvider, option)) {
				return *error;
			}
			provider_name = arguments[index + 1];
			index += 2;
			continue;
		}

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
			if (const auto error = select_command(CliCommand::CpuInference, option)) {
				return *error;
			}
			provider = value;
		} else {
			model_path = value;
		}
		index += 2;
	}

	if (!command.has_value()) {
		return {.options = std::nullopt, .error = "--provider cpu is required"};
	}

	if (*command != CliCommand::CpuInference) {
		if (model_path.has_value()) {
			return {.options = std::nullopt, .error = "--model is only valid with --provider cpu"};
		}
		return {.options = CliOptions{.command = *command,
					      .model_path = {},
					      .provider_name = std::string(provider_name.value_or(""))},
			.error = {}};
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

	return {.options = CliOptions{.command = CliCommand::CpuInference,
				      .model_path = std::string(*model_path),
				      .provider_name = {}},
		.error = {}};
}

} // namespace windows_ml_smoke

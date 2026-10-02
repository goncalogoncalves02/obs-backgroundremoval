// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli.hpp"

#include <charconv>

namespace windows_ml_smoke {

namespace {

[[nodiscard]] std::optional<std::size_t> parse_iterations(std::string_view text)
{
	if (text.empty()) {
		return std::nullopt;
	}
	for (const char character : text) {
		if (character < '0' || character > '9') {
			return std::nullopt;
		}
	}

	std::size_t value = 0;
	const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, 10);
	if (error != std::errc{} || end != text.data() + text.size() || value == 0 || value > 10000) {
		return std::nullopt;
	}
	return value;
}

} // namespace

ParseResult parse_cli(std::span<const std::string_view> arguments)
{
	std::optional<std::string_view> provider;
	std::optional<std::string_view> comparison_provider;
	std::optional<std::string_view> model_path;
	std::optional<std::string_view> prepared_provider_name;
	std::optional<std::size_t> iterations;
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
			if (prepared_provider_name.has_value()) {
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
			prepared_provider_name = arguments[index + 1];
			index += 2;
			continue;
		}

		if (option == "--compare") {
			if (index + 2 >= arguments.size() || arguments[index + 1].empty() ||
			    arguments[index + 2].empty() || arguments[index + 1].starts_with("--") ||
			    arguments[index + 2].starts_with("--")) {
				return {.options = std::nullopt,
					.error = "--compare cpu <exact-provider-name> is required"};
			}
			if (const auto error = select_command(CliCommand::Compare, option)) {
				return *error;
			}
			provider = arguments[index + 1];
			comparison_provider = arguments[index + 2];
			index += 3;
			continue;
		}

		if (option != "--provider" && option != "--model" && option != "--iterations") {
			return {.options = std::nullopt, .error = "unknown option: " + std::string(option)};
		}

		if (option == "--provider" && provider.has_value() && command != CliCommand::Compare) {
			return {.options = std::nullopt, .error = "duplicate option: --provider"};
		}
		if (option == "--model" && model_path.has_value()) {
			return {.options = std::nullopt, .error = "duplicate option: --model"};
		}
		if (option == "--iterations" && iterations.has_value()) {
			return {.options = std::nullopt, .error = "duplicate option: --iterations"};
		}
		if (index + 1 >= arguments.size()) {
			const auto required = option == "--provider" ? "--provider cpu is required"
					      : option == "--model"  ? "--model <path> is required"
								     : "--iterations <1..10000> is required";
			return {.options = std::nullopt, .error = required};
		}

		const auto value = arguments[index + 1];
		if (option == "--provider") {
			if (const auto error = select_command(CliCommand::Inference, option)) {
				return *error;
			}
			provider = value;
		} else if (option == "--model") {
			model_path = value;
		} else {
			const auto parsed_iterations = parse_iterations(value);
			if (!parsed_iterations.has_value()) {
				return {.options = std::nullopt,
					.error = "--iterations must be a decimal integer in range 1..10000"};
			}
			iterations = *parsed_iterations;
		}
		index += 2;
	}

	if (!command.has_value()) {
		return {.options = std::nullopt, .error = "--provider cpu is required"};
	}

	if (*command == CliCommand::ListProviders || *command == CliCommand::PrepareProvider) {
		if (model_path.has_value()) {
			return {.options = std::nullopt, .error = "--model is only valid with --provider cpu"};
		}
		if (iterations.has_value()) {
			return {.options = std::nullopt,
				.error = "--iterations is only valid with --provider or --compare"};
		}
		return {.options = CliOptions{.command = *command,
					      .provider_name = std::string(prepared_provider_name.value_or("")),
					      .comparison_provider_name = {},
					      .model_path = {},
					      .iterations = 0,
					      .benchmark_requested = false},
			.error = {}};
	}

	if (!model_path.has_value() || model_path->empty()) {
		return {.options = std::nullopt, .error = "--model <path> is required"};
	}

	if (*command == CliCommand::Compare) {
		if (*provider != "cpu") {
			return {.options = std::nullopt, .error = "--compare baseline must be exactly cpu"};
		}
		if (*comparison_provider == "cpu") {
			return {.options = std::nullopt, .error = "--compare candidate must differ from cpu"};
		}
		if (!iterations.has_value()) {
			return {.options = std::nullopt, .error = "--iterations is required with --compare"};
		}
		return {.options = CliOptions{.command = CliCommand::Compare,
					      .provider_name = std::string(*provider),
					      .comparison_provider_name = std::string(*comparison_provider),
					      .model_path = std::string(*model_path),
					      .iterations = *iterations,
					      .benchmark_requested = true},
			.error = {}};
	}

	if (!provider.has_value()) {
		return {.options = std::nullopt, .error = "--provider cpu is required"};
	}
	if (*provider != "cpu" && !iterations.has_value()) {
		return {.options = std::nullopt, .error = "--iterations is required for non-cpu providers"};
	}

	return {.options = CliOptions{.command = CliCommand::Inference,
				      .provider_name = std::string(*provider),
				      .comparison_provider_name = {},
				      .model_path = std::string(*model_path),
				      .iterations = iterations.value_or(1),
				      .benchmark_requested = iterations.has_value()},
		.error = {}};
}

} // namespace windows_ml_smoke

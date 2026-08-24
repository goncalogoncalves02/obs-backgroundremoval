// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cli.hpp"

#include <array>
#include <iostream>
#include <span>
#include <string_view>

namespace {

struct RejectedCase {
	std::string_view name;
	std::span<const std::string_view> arguments;
	std::string_view expected_error;
};

int failures = 0;

void expect(bool condition, std::string_view message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

} // namespace

int main()
{
	using namespace std::string_view_literals;
	using windows_ml_smoke::CliCommand;
	using windows_ml_smoke::parse_cli;

	constexpr std::array accepted_arguments{"--provider"sv, "cpu"sv, "--model"sv,
						"models/model with spaces.onnx"sv};
	const auto accepted = parse_cli(accepted_arguments);
	expect(accepted.ok(), "accepts --provider cpu --model <path>");
	if (accepted.ok()) {
		expect(accepted.options->command == CliCommand::CpuInference,
		       "selects the CPU inference command explicitly");
		expect(accepted.options->model_path == "models/model with spaces.onnx",
		       "preserves the exact model path");
	}

	constexpr std::array list_arguments{"--list-providers"sv};
	const auto list = parse_cli(list_arguments);
	expect(list.ok(), "accepts --list-providers");
	if (list.ok()) {
		expect(list.options->command == CliCommand::ListProviders,
		       "selects the list-providers command explicitly");
	}

	constexpr std::array prepare_arguments{"--prepare-provider"sv, "MiGraphX.Execution-Provider"sv};
	const auto prepare = parse_cli(prepare_arguments);
	expect(prepare.ok(), "accepts --prepare-provider <exact-provider-name>");
	if (prepare.ok()) {
		expect(prepare.options->command == CliCommand::PrepareProvider,
		       "selects the prepare-provider command explicitly");
		expect(prepare.options->provider_name == "MiGraphX.Execution-Provider",
		       "preserves provider name case and punctuation");
	}

	constexpr std::array missing_provider{"--model"sv, "model.onnx"sv};
	constexpr std::array non_cpu_provider{"--provider"sv, "directml"sv, "--model"sv, "model.onnx"sv};
	constexpr std::array missing_model{"--provider"sv, "cpu"sv};
	constexpr std::array duplicate_provider{"--provider"sv, "cpu"sv,     "--provider"sv,
						"cpu"sv,        "--model"sv, "model.onnx"sv};
	constexpr std::array duplicate_model{"--provider"sv, "cpu"sv,     "--model"sv,
					     "first.onnx"sv, "--model"sv, "second.onnx"sv};
	constexpr std::array unknown_option{"--provider"sv, "cpu"sv,          "--model"sv,
					    "model.onnx"sv, "--iterations"sv, "1"sv};
	constexpr std::array empty_provider_name{"--prepare-provider"sv, ""sv};
	constexpr std::array missing_provider_name{"--prepare-provider"sv};
	constexpr std::array duplicate_list{"--list-providers"sv, "--list-providers"sv};
	constexpr std::array duplicate_prepare{"--prepare-provider"sv, "MIGraphXExecutionProvider"sv,
					       "--prepare-provider"sv, "MIGraphXExecutionProvider"sv};
	constexpr std::array mixed_cpu_list{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv, "--list-providers"sv};
	constexpr std::array mixed_list_prepare{"--list-providers"sv, "--prepare-provider"sv,
						"MIGraphXExecutionProvider"sv};
	constexpr std::array list_with_model{"--list-providers"sv, "--model"sv, "model.onnx"sv};
	constexpr std::array prepare_with_model{"--prepare-provider"sv, "MIGraphXExecutionProvider"sv, "--model"sv,
						"model.onnx"sv};

	const std::array rejected_cases{
		RejectedCase{"missing provider", missing_provider, "--provider cpu is required"},
		RejectedCase{"non-CPU provider", non_cpu_provider,
			     "unsupported provider: directml (only cpu is supported)"},
		RejectedCase{"missing model", missing_model, "--model <path> is required"},
		RejectedCase{"duplicate provider", duplicate_provider, "duplicate option: --provider"},
		RejectedCase{"duplicate model", duplicate_model, "duplicate option: --model"},
		RejectedCase{"unknown option", unknown_option, "unknown option: --iterations"},
		RejectedCase{"empty prepare-provider name", empty_provider_name,
			     "--prepare-provider <exact-provider-name> is required"},
		RejectedCase{"missing prepare-provider name", missing_provider_name,
			     "--prepare-provider <exact-provider-name> is required"},
		RejectedCase{"duplicate list command", duplicate_list, "duplicate command option: --list-providers"},
		RejectedCase{"duplicate prepare command", duplicate_prepare,
			     "duplicate command option: --prepare-provider"},
		RejectedCase{"mixed CPU and list commands", mixed_cpu_list, "multiple commands are not allowed"},
		RejectedCase{"mixed list and prepare commands", mixed_list_prepare,
			     "multiple commands are not allowed"},
		RejectedCase{"model attached to list", list_with_model, "--model is only valid with --provider cpu"},
		RejectedCase{"model attached to prepare", prepare_with_model,
			     "--model is only valid with --provider cpu"},
	};

	for (const auto &test_case : rejected_cases) {
		const auto result = parse_cli(test_case.arguments);
		expect(!result.ok(), test_case.name);
		expect(result.error == test_case.expected_error, test_case.name);
	}

	if (failures != 0) {
		std::cerr << failures << " assertion(s) failed\n";
		return 1;
	}

	return 0;
}

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: Apache-2.0

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
	using windows_ml_smoke::parse_cli;

	constexpr std::array accepted_arguments{"--provider"sv, "cpu"sv, "--model"sv, "models/model with spaces.onnx"sv};
	const auto accepted = parse_cli(accepted_arguments);
	expect(accepted.ok(), "accepts --provider cpu --model <path>");
	if (accepted.ok()) {
		expect(accepted.options->model_path == "models/model with spaces.onnx", "preserves the exact model path");
	}

	constexpr std::array missing_provider{"--model"sv, "model.onnx"sv};
	constexpr std::array non_cpu_provider{"--provider"sv, "directml"sv, "--model"sv, "model.onnx"sv};
	constexpr std::array missing_model{"--provider"sv, "cpu"sv};
	constexpr std::array duplicate_provider{"--provider"sv, "cpu"sv, "--provider"sv, "cpu"sv, "--model"sv,
	                                        "model.onnx"sv};
	constexpr std::array duplicate_model{"--provider"sv, "cpu"sv, "--model"sv, "first.onnx"sv, "--model"sv,
	                                     "second.onnx"sv};
	constexpr std::array unknown_option{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv, "--iterations"sv,
	                                    "1"sv};

	const std::array rejected_cases{
		RejectedCase{"missing provider", missing_provider, "--provider cpu is required"},
		RejectedCase{"non-CPU provider", non_cpu_provider, "unsupported provider: directml (only cpu is supported)"},
		RejectedCase{"missing model", missing_model, "--model <path> is required"},
		RejectedCase{"duplicate provider", duplicate_provider, "duplicate option: --provider"},
		RejectedCase{"duplicate model", duplicate_model, "duplicate option: --model"},
		RejectedCase{"unknown option", unknown_option, "unknown option: --iterations"},
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

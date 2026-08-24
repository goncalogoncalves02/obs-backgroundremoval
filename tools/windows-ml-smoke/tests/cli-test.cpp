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
		expect(accepted.options->provider_name == "cpu", "preserves the exact legacy CPU provider spelling");
		expect(accepted.options->iterations == 1, "uses one iteration for legacy CPU inference");
		expect(!accepted.options->benchmark_requested, "does not mark legacy CPU inference as a benchmark");
	}

	// Catches: dropped explicit provider spelling or benchmark iteration counts.
	constexpr std::array cpu_benchmark{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv, "--iterations"sv,
							    "100"sv};
	constexpr std::array migraphx_benchmark{"--provider"sv, "MIGraphXExecutionProvider"sv, "--model"sv,
								 "model.onnx"sv, "--iterations"sv, "100"sv};
	constexpr std::array dml_benchmark{"--provider"sv, "DmlExecutionProvider"sv, "--model"sv, "model.onnx"sv,
								"--iterations"sv, "100"sv};
	for (const auto &arguments : {std::span<const std::string_view>(cpu_benchmark),
						    std::span<const std::string_view>(migraphx_benchmark),
						    std::span<const std::string_view>(dml_benchmark)}) {
		const auto benchmark = parse_cli(arguments);
		expect(benchmark.ok(), "accepts an explicit provider benchmark");
		if (benchmark.ok()) {
			expect(benchmark.options->command == CliCommand::Inference,
			       "selects inference for a provider benchmark");
			expect(benchmark.options->iterations == 100, "preserves explicit benchmark iterations");
			expect(benchmark.options->benchmark_requested, "marks explicit iterations as a benchmark");
		}
	}
	const auto migraphx_provider = parse_cli(migraphx_benchmark);
	expect(migraphx_provider.ok() && migraphx_provider.options->provider_name == "MIGraphXExecutionProvider",
	       "preserves the exact MIGraphX provider spelling");
	const auto dml_provider = parse_cli(dml_benchmark);
	expect(dml_provider.ok() && dml_provider.options->provider_name == "DmlExecutionProvider",
	       "preserves the exact DirectML provider spelling");

	// Catches: accepting a non-CPU baseline or losing the requested comparison candidate.
	constexpr std::array migraphx_compare{"--compare"sv, "cpu"sv, "MIGraphXExecutionProvider"sv, "--model"sv,
								  "model.onnx"sv, "--iterations"sv, "100"sv};
	constexpr std::array dml_compare{"--compare"sv, "cpu"sv, "DmlExecutionProvider"sv, "--model"sv,
							  "model.onnx"sv, "--iterations"sv, "100"sv};
	for (const auto &arguments : {std::span<const std::string_view>(migraphx_compare),
						    std::span<const std::string_view>(dml_compare)}) {
		const auto comparison = parse_cli(arguments);
		expect(comparison.ok(), "accepts a CPU-to-provider comparison");
		if (comparison.ok()) {
			expect(comparison.options->command == CliCommand::Compare, "selects the comparison command");
			expect(comparison.options->provider_name == "cpu", "preserves the CPU comparison baseline");
			expect(!comparison.options->comparison_provider_name.empty(), "preserves the comparison candidate");
			expect(comparison.options->iterations == 100, "preserves comparison iterations");
			expect(comparison.options->benchmark_requested, "marks comparisons as benchmarks");
		}
	}
	const auto migraphx_comparison = parse_cli(migraphx_compare);
	expect(migraphx_comparison.ok() &&
		       migraphx_comparison.options->comparison_provider_name == "MIGraphXExecutionProvider",
	       "preserves the exact MIGraphX comparison provider spelling");
	const auto dml_comparison = parse_cli(dml_compare);
	expect(dml_comparison.ok() && dml_comparison.options->comparison_provider_name == "DmlExecutionProvider",
	       "preserves the exact DirectML comparison provider spelling");

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
	constexpr std::array zero_iterations{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv, "--iterations"sv,
							     "0"sv};
	constexpr std::array negative_iterations{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv,
								 "--iterations"sv, "-1"sv};
	constexpr std::array non_decimal_iterations{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv,
								    "--iterations"sv, "1.5"sv};
	constexpr std::array overflow_iterations{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv,
								"--iterations"sv, "18446744073709551616"sv};
	constexpr std::array too_many_iterations{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv,
								 "--iterations"sv, "10001"sv};
	constexpr std::array duplicate_iterations{"--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv,
								 "--iterations"sv, "1"sv, "--iterations"sv, "2"sv};
	constexpr std::array named_provider_without_iterations{"--provider"sv, "MIGraphXExecutionProvider"sv,
												 "--model"sv, "model.onnx"sv};
	constexpr std::array compare_without_iterations{"--compare"sv, "cpu"sv, "MIGraphXExecutionProvider"sv,
									    "--model"sv, "model.onnx"sv};
	constexpr std::array non_cpu_comparison_baseline{"--compare"sv, "DmlExecutionProvider"sv,
										       "MIGraphXExecutionProvider"sv, "--model"sv, "model.onnx"sv,
										       "--iterations"sv, "100"sv};
	constexpr std::array identical_comparison_providers{"--compare"sv, "cpu"sv, "cpu"sv, "--model"sv,
											  "model.onnx"sv, "--iterations"sv, "100"sv};
	constexpr std::array mixed_compare_provider{"--compare"sv, "cpu"sv, "MIGraphXExecutionProvider"sv,
									   "--provider"sv, "cpu"sv, "--model"sv, "model.onnx"sv,
									   "--iterations"sv, "100"sv};
	constexpr std::array list_with_iterations{"--list-providers"sv, "--iterations"sv, "100"sv};
	constexpr std::array prepare_with_iterations{"--prepare-provider"sv, "MIGraphXExecutionProvider"sv,
									       "--iterations"sv, "100"sv};
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
		RejectedCase{"non-CPU provider", non_cpu_provider, "--iterations is required for non-cpu providers"},
		RejectedCase{"missing model", missing_model, "--model <path> is required"},
		RejectedCase{"duplicate provider", duplicate_provider, "duplicate option: --provider"},
		RejectedCase{"duplicate model", duplicate_model, "duplicate option: --model"},
		RejectedCase{"zero iterations", zero_iterations, "--iterations must be a decimal integer in range 1..10000"},
		RejectedCase{"negative iterations", negative_iterations, "--iterations must be a decimal integer in range 1..10000"},
		RejectedCase{"non-decimal iterations", non_decimal_iterations,
				     "--iterations must be a decimal integer in range 1..10000"},
		RejectedCase{"overflowing iterations", overflow_iterations,
				     "--iterations must be a decimal integer in range 1..10000"},
		RejectedCase{"iterations above the supported limit", too_many_iterations,
				     "--iterations must be a decimal integer in range 1..10000"},
		RejectedCase{"duplicate iterations", duplicate_iterations, "duplicate option: --iterations"},
		RejectedCase{"named provider without iterations", named_provider_without_iterations,
				     "--iterations is required for non-cpu providers"},
		RejectedCase{"comparison without iterations", compare_without_iterations,
				     "--iterations is required with --compare"},
		RejectedCase{"non-CPU comparison baseline", non_cpu_comparison_baseline,
				     "--compare baseline must be exactly cpu"},
		RejectedCase{"identical comparison providers", identical_comparison_providers,
				     "--compare candidate must differ from cpu"},
		RejectedCase{"mixed compare and provider", mixed_compare_provider, "multiple commands are not allowed"},
		RejectedCase{"iterations attached to list", list_with_iterations,
				     "--iterations is only valid with --provider or --compare"},
		RejectedCase{"iterations attached to prepare", prepare_with_iterations,
				     "--iterations is only valid with --provider or --compare"},
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

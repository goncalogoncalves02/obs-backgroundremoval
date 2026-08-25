// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "benchmark.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

template<typename Function> void expect_invalid_argument(Function &&function, std::string_view message)
{
	try {
		function();
		expect(false, message);
	} catch (const std::invalid_argument &) {
	} catch (...) {
		expect(false, message);
	}
}

} // namespace

int main()
{
	using windows_ml_smoke::compare_outputs;
	using windows_ml_smoke::calculate_speedup_ratio;
	using windows_ml_smoke::make_deterministic_input;
	using windows_ml_smoke::summarize_latencies;

	// Catches: a changed seed, xorshift transition, bit mask, scale, or non-deterministic generator.
	const auto first_input = make_deterministic_input(8);
	const auto second_input = make_deterministic_input(8);
	constexpr std::array expected_input{0.682725887F, 0.875184648F, 0.062029365F, 0.441263046F,
					    0.804730225F, 0.419035400F, 0.150124320F, 0.031266632F};
	expect(first_input.size() == expected_input.size(), "generates the requested number of input values");
	expect(first_input == second_input, "generates byte-identical input on repeated calls");
	for (std::size_t index = 0; index < first_input.size(); ++index) {
		expect(std::fabs(first_input[index] - expected_input[index]) <= 1.0e-7F,
		       "matches the hand-calculated xorshift fixture");
		expect(std::isfinite(first_input[index]) && first_input[index] >= 0.0F && first_input[index] <= 1.0F,
		       "generates finite normalized input values");
	}

	// Catches: in-place sorting, incorrect even median, incorrect nearest-rank p95, or a non-arithmetic mean.
	const std::vector<double> latencies{4.0, 1.0, 3.0, 2.0};
	const auto summary = summarize_latencies(latencies);
	expect(summary.average_ms == 2.5, "calculates the arithmetic mean latency");
	expect(summary.median_ms == 2.5, "calculates the even-count median latency");
	expect(summary.p95_ms == 4.0, "calculates the nearest-rank p95 latency");
	expect(latencies == std::vector<double>({4.0, 1.0, 3.0, 2.0}), "does not mutate caller latency order");
	const std::vector<double> empty_latencies;
	const std::vector<double> non_finite_latencies{1.0, std::numeric_limits<double>::infinity()};
	expect_invalid_argument([&] { static_cast<void>(summarize_latencies(empty_latencies)); },
				"rejects empty latency input");
	expect_invalid_argument([&] { static_cast<void>(summarize_latencies(non_finite_latencies)); },
				"rejects non-finite latency input");

	// Catches: dividing by a zero candidate average and formatting inf/nan as a benchmark result.
	expect(calculate_speedup_ratio(2.5, 1.25) == 2.0, "calculates CPU-to-candidate speedup");
	expect_invalid_argument([&] { static_cast<void>(calculate_speedup_ratio(2.5, 0.0)); },
				"rejects a zero candidate average before division");
	const double infinity = std::numeric_limits<double>::infinity();
	const double quiet_nan = std::numeric_limits<double>::quiet_NaN();
	expect_invalid_argument([&] { static_cast<void>(calculate_speedup_ratio(quiet_nan, 1.0)); },
				"rejects a NaN baseline average");
	expect_invalid_argument([&] { static_cast<void>(calculate_speedup_ratio(1.0, quiet_nan)); },
				"rejects a NaN candidate average");
	expect_invalid_argument([&] { static_cast<void>(calculate_speedup_ratio(infinity, 1.0)); },
				"rejects an infinite baseline average");
	expect_invalid_argument([&] { static_cast<void>(calculate_speedup_ratio(1.0, infinity)); },
				"rejects an infinite candidate average");
	expect_invalid_argument(
		[&] {
			static_cast<void>(calculate_speedup_ratio(std::numeric_limits<double>::max(),
								  std::numeric_limits<double>::denorm_min()));
		},
		"rejects a non-finite quotient from finite operands");

	// Catches: MAE restricted to foreground, wrong foreground channel, or incorrect thresholded IoU counts.
	constexpr std::array cpu_output{0.9F, 0.1F, 0.4F, 0.6F, 0.8F, 0.2F, 0.3F, 0.7F};
	constexpr std::array candidate_output{0.85F, 0.15F, 0.55F, 0.45F, 0.75F, 0.25F, 0.35F, 0.65F};
	const auto comparison = compare_outputs(cpu_output, candidate_output, 1, 2, 0.5F);
	expect(std::fabs(comparison.mean_absolute_error - 0.075F) <= 1.0e-7F, "calculates MAE over every output value");
	expect(comparison.foreground_intersection == 1, "counts thresholded foreground intersection");
	expect(comparison.foreground_union == 2, "counts thresholded foreground union");
	expect(std::fabs(comparison.foreground_iou - 0.5F) <= 1.0e-7F, "calculates foreground IoU");

	// Catches: treating two empty foreground masks as zero similarity.
	constexpr std::array empty_foreground_cpu{0.1F, 0.1F, 0.2F, 0.2F};
	constexpr std::array empty_foreground_candidate{0.3F, 0.3F, 0.4F, 0.4F};
	const auto empty_union = compare_outputs(empty_foreground_cpu, empty_foreground_candidate, 1, 2, 0.5F);
	expect(empty_union.foreground_iou == 1.0F, "defines IoU as one for two empty foreground masks");

	// Catches: accepting structurally invalid or NaN/inf output comparisons.
	constexpr std::array shorter_output{0.9F, 0.1F};
	const std::array non_finite_output{0.9F, std::numeric_limits<float>::quiet_NaN()};
	expect_invalid_argument([&] { static_cast<void>(compare_outputs(cpu_output, shorter_output, 1, 2, 0.5F)); },
				"rejects output size mismatch");
	expect_invalid_argument([&] { static_cast<void>(compare_outputs(cpu_output, cpu_output, 2, 2, 0.5F)); },
				"rejects a foreground channel outside the channel count");
	expect_invalid_argument([&] { static_cast<void>(compare_outputs(cpu_output, cpu_output, 0, 0, 0.5F)); },
				"rejects a zero channel count");
	expect_invalid_argument([&] { static_cast<void>(compare_outputs(cpu_output, shorter_output, 0, 3, 0.5F)); },
				"rejects output sizes not divisible by the channel count");
	expect_invalid_argument(
		[&] { static_cast<void>(compare_outputs(non_finite_output, non_finite_output, 1, 2, 0.5F)); },
		"rejects non-finite output values");

	if (failures != 0) {
		std::cerr << failures << " assertion(s) failed\n";
		return 1;
	}
	return 0;
}

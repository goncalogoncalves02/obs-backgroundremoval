// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "benchmark.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <stdexcept>

namespace windows_ml_smoke {

std::vector<float> make_deterministic_input(std::size_t count)
{
	std::uint32_t state = 0x6d2b79f5U;
	std::vector<float> input;
	input.reserve(count);
	for (std::size_t index = 0; index < count; ++index) {
		state ^= state << 13U;
		state ^= state >> 17U;
		state ^= state << 5U;
		input.push_back(static_cast<float>(state & 0x00ffffffU) / 16777215.0F);
	}
	return input;
}

LatencySummary summarize_latencies(std::span<const double> latencies)
{
	if (latencies.empty() || std::any_of(latencies.begin(), latencies.end(), [](double latency) {
			return !std::isfinite(latency);
		})) {
		throw std::invalid_argument("latencies must be non-empty and finite");
	}

	std::vector<double> sorted(latencies.begin(), latencies.end());
	std::sort(sorted.begin(), sorted.end());
	const double average = std::accumulate(sorted.begin(), sorted.end(), 0.0) / static_cast<double>(sorted.size());
	const std::size_t middle = sorted.size() / 2;
	const double median = sorted.size() % 2 == 0 ? (sorted[middle - 1] + sorted[middle]) / 2.0 : sorted[middle];
	const std::size_t p95_index = static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(sorted.size()))) - 1;
	return {.average_ms = average, .median_ms = median, .p95_ms = sorted[p95_index]};
}

OutputComparison compare_outputs(std::span<const float> cpu_output, std::span<const float> candidate_output,
					  std::size_t foreground_channel, std::size_t channel_count, float threshold)
{
	if (cpu_output.empty() || cpu_output.size() != candidate_output.size() || channel_count == 0 ||
	    foreground_channel >= channel_count || cpu_output.size() % channel_count != 0 || !std::isfinite(threshold)) {
		throw std::invalid_argument("outputs must have matching non-empty interleaved channels and a finite threshold");
	}

	double absolute_error = 0.0;
	std::size_t intersection = 0;
	std::size_t union_count = 0;
	for (std::size_t index = 0; index < cpu_output.size(); ++index) {
		if (!std::isfinite(cpu_output[index]) || !std::isfinite(candidate_output[index])) {
			throw std::invalid_argument("outputs must be finite");
		}
		absolute_error += std::fabs(static_cast<double>(cpu_output[index]) - static_cast<double>(candidate_output[index]));
		if (index % channel_count == foreground_channel) {
			const bool cpu_foreground = cpu_output[index] >= threshold;
			const bool candidate_foreground = candidate_output[index] >= threshold;
			if (cpu_foreground && candidate_foreground) {
				++intersection;
			}
			if (cpu_foreground || candidate_foreground) {
				++union_count;
			}
		}
	}

	return {.mean_absolute_error = static_cast<float>(absolute_error / static_cast<double>(cpu_output.size())),
		.foreground_intersection = intersection,
		.foreground_union = union_count,
		.foreground_iou = union_count == 0 ? 1.0F : static_cast<float>(intersection) / static_cast<float>(union_count)};
}

} // namespace windows_ml_smoke

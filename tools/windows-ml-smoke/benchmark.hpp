// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace windows_ml_smoke {

struct LatencySummary {
	double average_ms;
	double median_ms;
	double p95_ms;
};

struct OutputComparison {
	float mean_absolute_error;
	std::size_t foreground_intersection;
	std::size_t foreground_union;
	float foreground_iou;
};

[[nodiscard]] std::vector<float> make_deterministic_input(std::size_t count);
[[nodiscard]] LatencySummary summarize_latencies(std::span<const double> latencies);
[[nodiscard]] OutputComparison compare_outputs(std::span<const float> cpu_output,
							 std::span<const float> candidate_output, std::size_t foreground_channel,
							 std::size_t channel_count, float threshold);

} // namespace windows_ml_smoke

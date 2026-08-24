// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "benchmark.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace windows_ml_smoke {

inline constexpr std::size_t kInputElementCount = 110592;
inline constexpr std::size_t kOutputElementCount = 73728;
inline constexpr std::size_t kBenchmarkWarmupIterations = 10;
inline constexpr std::array<std::int64_t, 4> kExpectedInputShape{1, 144, 256, 3};
inline constexpr std::array<std::int64_t, 4> kExpectedOutputShape{1, 144, 256, 2};

struct InferenceResult {
	std::string requested_provider;
	std::string effective_provider;
	bool process_activation_attempted{};
	bool provider_registration_succeeded{};
	bool cpu_ep_fallback_disabled{};
	std::string selected_ep_name;
	std::optional<std::uint32_t> selected_device_id;
	std::string model_path;
	std::string onnxruntime_version;
	std::size_t input_count{};
	std::size_t output_count{};
	std::string input_name;
	std::string output_name;
	std::vector<std::int64_t> input_shape;
	std::vector<std::int64_t> output_shape;
	std::size_t warmup_iterations{};
	std::size_t iterations{};
	std::size_t finite_output_count{};
	LatencySummary latency{};
	std::vector<float> output;
	bool succeeded{};
	bool provider_failure{};
	std::optional<std::uint32_t> error_hresult;
	std::string error;
};

struct ComparisonResult {
	InferenceResult cpu;
	InferenceResult candidate;
	OutputComparison comparison{};
	double speedup_ratio{};
	bool mae_gate_passed{};
	bool iou_gate_passed{};
	bool performance_gate_passed{};
	bool succeeded{};
	std::optional<std::uint32_t> error_hresult;
	std::string error;
};

[[nodiscard]] InferenceResult run_inference(std::string_view model_path, std::string_view provider_name,
					    std::size_t iterations, bool benchmark_requested,
					    std::span<const float> deterministic_input);

[[nodiscard]] ComparisonResult run_comparison(std::string_view model_path, std::string_view candidate_provider_name,
					      std::size_t iterations, std::span<const float> deterministic_input);

} // namespace windows_ml_smoke

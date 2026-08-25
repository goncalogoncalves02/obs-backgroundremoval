// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "benchmark.hpp"
#include "inference-runner.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

namespace {

constexpr std::size_t kRepeatedSessionCount = 64;

template<std::size_t Size>
[[nodiscard]] bool has_shape(std::span<const std::int64_t> actual, const std::array<std::int64_t, Size> &expected)
{
	return std::equal(actual.begin(), actual.end(), expected.begin(), expected.end());
}

} // namespace

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "usage: windows-ml-smoke-inference-metadata-test <model>\n";
		return 2;
	}

	const auto input = windows_ml_smoke::make_deterministic_input(windows_ml_smoke::kInputElementCount);
	for (std::size_t session_index = 0; session_index < kRepeatedSessionCount; ++session_index) {
		const auto result = windows_ml_smoke::run_inference(argv[1], "cpu", 1, false, input);
		if (!result.succeeded) {
			std::cerr << "session " << session_index << " failed: " << result.error << '\n';
			return 1;
		}
		if (!has_shape(result.input_shape, windows_ml_smoke::kExpectedInputShape)) {
			std::cerr << "session " << session_index << " reported an unexpected input shape\n";
			return 1;
		}
		if (!has_shape(result.output_shape, windows_ml_smoke::kExpectedOutputShape)) {
			std::cerr << "session " << session_index << " reported an unexpected output shape\n";
			return 1;
		}
	}

	return 0;
}

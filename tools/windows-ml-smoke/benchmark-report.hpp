// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "inference-runner.hpp"

#include <string>
#include <string_view>

namespace windows_ml_smoke {

[[nodiscard]] std::string format_legacy_inference_report(const InferenceResult &result,
							 std::string_view windows_version,
							 std::string_view windows_ml_package_version);

[[nodiscard]] std::string format_benchmark_inference_report(const InferenceResult &result);

[[nodiscard]] std::string format_comparison_report(const ComparisonResult &result);

[[nodiscard]] int inference_exit_code(const InferenceResult &result) noexcept;
[[nodiscard]] int comparison_exit_code(const ComparisonResult &result) noexcept;

} // namespace windows_ml_smoke

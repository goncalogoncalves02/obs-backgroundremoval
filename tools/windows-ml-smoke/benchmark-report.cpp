// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "benchmark-report.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace windows_ml_smoke {
namespace {

constexpr int kSuccessExitCode = 0;
constexpr int kInferenceExitCode = 4;
constexpr int kProviderExitCode = 5;

[[nodiscard]] std::string boolean_value(bool value)
{
	return value ? "true" : "false";
}

[[nodiscard]] std::string sanitize_single_line(std::string_view value)
{
	std::string sanitized(value);
	std::replace_if(
		sanitized.begin(), sanitized.end(),
		[](char character) { return character == '\r' || character == '\n'; }, ' ');
	return sanitized;
}

[[nodiscard]] std::string hexadecimal_value(std::uint32_t value)
{
	std::ostringstream formatted;
	formatted << "0x" << std::hex << std::nouppercase << value;
	return formatted.str();
}

[[nodiscard]] std::string shape_string(std::span<const std::int64_t> shape)
{
	std::ostringstream report;
	for (std::size_t index = 0; index < shape.size(); ++index) {
		if (index != 0) {
			report << 'x';
		}
		report << shape[index];
	}
	return report.str();
}

void append_provider_diagnostics(std::ostringstream &report, const InferenceResult &result)
{
	report << "requested_provider=" << sanitize_single_line(result.requested_provider) << '\n';
	report << "effective_provider=" << sanitize_single_line(result.effective_provider) << '\n';
	report << "process_activation_attempted=" << boolean_value(result.process_activation_attempted) << '\n';
	report << "provider_registration_succeeded=" << boolean_value(result.provider_registration_succeeded) << '\n';
	report << "cpu_ep_fallback_disabled=" << boolean_value(result.cpu_ep_fallback_disabled) << '\n';
	report << "selected_ep_name=" << sanitize_single_line(result.selected_ep_name) << '\n';
	report << "selected_device_id=";
	if (result.selected_device_id.has_value()) {
		report << hexadecimal_value(*result.selected_device_id);
	}
	report << '\n';
}

void append_error(std::ostringstream &report, const std::optional<std::uint32_t> &hresult, std::string_view error)
{
	if (hresult.has_value()) {
		report << "error_hresult=" << hexadecimal_value(*hresult) << '\n';
	}
	if (!error.empty()) {
		report << "error=" << sanitize_single_line(error) << '\n';
	}
}

} // namespace

std::string format_legacy_inference_report(const InferenceResult &result, std::string_view windows_version,
					   std::string_view windows_ml_package_version)
{
	std::ostringstream report;
	report << "provider=cpu\n";
	report << "architecture=x64\n";
	report << "windows_version=" << windows_version << '\n';
	report << "windows_ml_package_version=" << windows_ml_package_version << '\n';
	report << "onnxruntime_version=" << result.onnxruntime_version << '\n';
	report << "model=" << result.model_path << '\n';
	report << "input_count=" << result.input_count << '\n';
	report << "output_count=" << result.output_count << '\n';
	report << "input_name=" << result.input_name << '\n';
	report << "output_name=" << result.output_name << '\n';
	report << "input_element_type=float\n";
	report << "output_element_type=float\n";
	report << "input_shape=" << shape_string(result.input_shape) << '\n';
	report << "output_shape=" << shape_string(result.output_shape) << '\n';
	report << "iterations=1\n";
	report << "finite_output_count=" << result.finite_output_count << '\n';
	report << "latency_ms=" << std::fixed << std::setprecision(6) << result.latency.average_ms << '\n';
	report << "status=ok\n";
	return report.str();
}

std::string format_benchmark_inference_report(const InferenceResult &result)
{
	std::ostringstream report;
	report << "operation=inference\n";
	append_provider_diagnostics(report, result);
	report << "model=" << sanitize_single_line(result.model_path) << '\n';
	report << "input_count=" << result.input_count << '\n';
	report << "output_count=" << result.output_count << '\n';
	report << "input_name=" << sanitize_single_line(result.input_name) << '\n';
	report << "output_name=" << sanitize_single_line(result.output_name) << '\n';
	report << "input_element_type=float\n";
	report << "output_element_type=float\n";
	report << "input_shape=" << shape_string(result.input_shape) << '\n';
	report << "output_shape=" << shape_string(result.output_shape) << '\n';
	report << "warmup_iterations=" << result.warmup_iterations << '\n';
	report << "iterations=" << result.iterations << '\n';
	report << "finite_output_count=" << result.finite_output_count << '\n';
	report << std::fixed << std::setprecision(6);
	report << "latency_average_ms=" << result.latency.average_ms << '\n';
	report << "latency_p50_ms=" << result.latency.median_ms << '\n';
	report << "latency_p95_ms=" << result.latency.p95_ms << '\n';
	append_error(report, result.error_hresult, result.error);
	report << "status=" << (result.succeeded ? "ok" : result.provider_failure ? "unavailable" : "failed") << '\n';
	return report.str();
}

std::string format_comparison_report(const ComparisonResult &result)
{
	std::ostringstream report;
	report << "operation=comparison\n";
	report << "baseline_provider=cpu\n";
	append_provider_diagnostics(report, result.candidate);
	report << "warmup_iterations=" << result.candidate.warmup_iterations << '\n';
	report << "iterations=" << result.candidate.iterations << '\n';
	report << "cpu_finite_output_count=" << result.cpu.finite_output_count << '\n';
	report << "candidate_finite_output_count=" << result.candidate.finite_output_count << '\n';
	report << std::fixed << std::setprecision(6);
	report << "cpu_latency_average_ms=" << result.cpu.latency.average_ms << '\n';
	report << "cpu_latency_p50_ms=" << result.cpu.latency.median_ms << '\n';
	report << "cpu_latency_p95_ms=" << result.cpu.latency.p95_ms << '\n';
	report << "candidate_latency_average_ms=" << result.candidate.latency.average_ms << '\n';
	report << "candidate_latency_p50_ms=" << result.candidate.latency.median_ms << '\n';
	report << "candidate_latency_p95_ms=" << result.candidate.latency.p95_ms << '\n';
	report << "speedup_ratio=" << result.speedup_ratio << '\n';
	report << "normalized_mae=" << result.comparison.mean_absolute_error << '\n';
	report << "foreground_intersection=" << result.comparison.foreground_intersection << '\n';
	report << "foreground_union=" << result.comparison.foreground_union << '\n';
	report << "foreground_iou=" << result.comparison.foreground_iou << '\n';
	report << "mae_gate_passed=" << boolean_value(result.mae_gate_passed) << '\n';
	report << "iou_gate_passed=" << boolean_value(result.iou_gate_passed) << '\n';
	report << "performance_gate_passed=" << boolean_value(result.performance_gate_passed) << '\n';
	append_error(report, result.error_hresult, result.error);
	report << "status="
	       << (result.succeeded                    ? "ok"
		   : result.candidate.provider_failure ? "unavailable"
						       : "failed")
	       << '\n';
	return report.str();
}

int inference_exit_code(const InferenceResult &result) noexcept
{
	return result.succeeded ? kSuccessExitCode : result.provider_failure ? kProviderExitCode : kInferenceExitCode;
}

int comparison_exit_code(const ComparisonResult &result) noexcept
{
	return result.succeeded                    ? kSuccessExitCode
	       : result.candidate.provider_failure ? kProviderExitCode
						   : kInferenceExitCode;
}

} // namespace windows_ml_smoke

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "benchmark-report.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect_equal(std::string_view actual, std::string_view expected, std::string_view mutation)
{
	if (actual != expected) {
		std::cerr << "FAIL: " << mutation << "\nexpected:\n" << expected << "\nactual:\n" << actual << '\n';
		++failures;
	}
}

void expect_equal(int actual, int expected, std::string_view mutation)
{
	if (actual != expected) {
		std::cerr << "FAIL: " << mutation << "\nexpected: " << expected << "\nactual: " << actual << '\n';
		++failures;
	}
}

windows_ml_smoke::InferenceResult successful_inference()
{
	windows_ml_smoke::InferenceResult result;
	result.requested_provider = "MIGraphX\nExecutionProvider";
	result.effective_provider = "MIGraphXExecutionProvider";
	result.process_activation_attempted = true;
	result.provider_registration_succeeded = true;
	result.cpu_ep_fallback_disabled = true;
	result.selected_ep_name = "MIGraphX\rExecutionProvider";
	result.selected_device_id = std::uint32_t{0x744c};
	result.model_path = "mediapipe.onnx";
	result.onnxruntime_version = "1.22.1";
	result.input_count = 1;
	result.output_count = 1;
	result.input_name = "input";
	result.output_name = "output";
	result.input_shape = {1, 144, 256, 3};
	result.output_shape = {1, 144, 256, 2};
	result.warmup_iterations = 10;
	result.iterations = 3;
	result.finite_output_count = 73728;
	result.latency = {.average_ms = 1.25, .median_ms = 1.125, .p95_ms = 1.5};
	result.succeeded = true;
	return result;
}

} // namespace

int main()
{
	using windows_ml_smoke::format_benchmark_inference_report;
	using windows_ml_smoke::format_comparison_report;

	const auto inference = successful_inference();
	expect_equal(
		format_benchmark_inference_report(inference),
		"operation=inference\n"
		"requested_provider=MIGraphX ExecutionProvider\n"
		"effective_provider=MIGraphXExecutionProvider\n"
		"process_activation_attempted=true\n"
		"provider_registration_succeeded=true\n"
		"cpu_ep_fallback_disabled=true\n"
		"selected_ep_name=MIGraphX ExecutionProvider\n"
		"selected_device_id=0x744c\n"
		"model=mediapipe.onnx\n"
		"input_count=1\n"
		"output_count=1\n"
		"input_name=input\n"
		"output_name=output\n"
		"input_element_type=float\n"
		"output_element_type=float\n"
		"input_shape=1x144x256x3\n"
		"output_shape=1x144x256x2\n"
		"warmup_iterations=10\n"
		"iterations=3\n"
		"finite_output_count=73728\n"
		"latency_average_ms=1.250000\n"
		"latency_p50_ms=1.125000\n"
		"latency_p95_ms=1.500000\n"
		"status=ok\n",
		"inference formatter mutations must not change ordered unique keys, sanitization, precision, or status");

	auto unavailable = inference;
	unavailable.requested_provider = "missing\nprovider";
	unavailable.effective_provider.clear();
	unavailable.process_activation_attempted = false;
	unavailable.provider_registration_succeeded = false;
	unavailable.cpu_ep_fallback_disabled = false;
	unavailable.selected_ep_name.clear();
	unavailable.selected_device_id.reset();
	unavailable.input_count = 0;
	unavailable.output_count = 0;
	unavailable.input_name.clear();
	unavailable.output_name.clear();
	unavailable.input_shape.clear();
	unavailable.output_shape.clear();
	unavailable.finite_output_count = 0;
	unavailable.latency = {};
	unavailable.succeeded = false;
	unavailable.provider_failure = true;
	unavailable.error_hresult = std::uint32_t{0x80070490};
	unavailable.error = "provider not\r\nfound";
	expect_equal(format_benchmark_inference_report(unavailable),
		     "operation=inference\n"
		     "requested_provider=missing provider\n"
		     "effective_provider=\n"
		     "process_activation_attempted=false\n"
		     "provider_registration_succeeded=false\n"
		     "cpu_ep_fallback_disabled=false\n"
		     "selected_ep_name=\n"
		     "selected_device_id=\n"
		     "model=mediapipe.onnx\n"
		     "input_count=0\n"
		     "output_count=0\n"
		     "input_name=\n"
		     "output_name=\n"
		     "input_element_type=float\n"
		     "output_element_type=float\n"
		     "input_shape=\n"
		     "output_shape=\n"
		     "warmup_iterations=10\n"
		     "iterations=3\n"
		     "finite_output_count=0\n"
		     "latency_average_ms=0.000000\n"
		     "latency_p50_ms=0.000000\n"
		     "latency_p95_ms=0.000000\n"
		     "error_hresult=0x80070490\n"
		     "error=provider not  found\n"
		     "status=unavailable\n",
		     "provider failures must remain single-line unavailable reports with status last");
	expect_equal(windows_ml_smoke::inference_exit_code(unavailable), 5,
		     "provider-boundary failures must not be classified as ordinary inference failures");
	auto full_graph_failure = inference;
	full_graph_failure.succeeded = false;
	full_graph_failure.provider_failure = false;
	full_graph_failure.error = "candidate cannot place the full graph";
	expect_equal(windows_ml_smoke::inference_exit_code(full_graph_failure), 4,
		     "a fallback-disabled candidate session failure must remain an inference failure");

	windows_ml_smoke::ComparisonResult comparison;
	comparison.cpu = inference;
	comparison.cpu.requested_provider = "cpu";
	comparison.cpu.effective_provider = "cpu";
	comparison.cpu.process_activation_attempted = false;
	comparison.cpu.provider_registration_succeeded = false;
	comparison.cpu.cpu_ep_fallback_disabled = false;
	comparison.cpu.selected_ep_name.clear();
	comparison.cpu.selected_device_id.reset();
	comparison.cpu.latency = {.average_ms = 2.5, .median_ms = 2.25, .p95_ms = 3.0};
	comparison.candidate = inference;
	comparison.comparison = {.mean_absolute_error = 0.0625F,
				 .foreground_intersection = 90,
				 .foreground_union = 100,
				 .foreground_iou = 0.9F};
	comparison.speedup_ratio = 2.0;
	comparison.mae_gate_passed = false;
	comparison.iou_gate_passed = false;
	comparison.performance_gate_passed = true;
	comparison.succeeded = false;

	expect_equal(
		format_comparison_report(comparison),
		"operation=comparison\n"
		"baseline_provider=cpu\n"
		"requested_provider=MIGraphX ExecutionProvider\n"
		"effective_provider=MIGraphXExecutionProvider\n"
		"process_activation_attempted=true\n"
		"provider_registration_succeeded=true\n"
		"cpu_ep_fallback_disabled=true\n"
		"selected_ep_name=MIGraphX ExecutionProvider\n"
		"selected_device_id=0x744c\n"
		"warmup_iterations=10\n"
		"iterations=3\n"
		"cpu_finite_output_count=73728\n"
		"candidate_finite_output_count=73728\n"
		"cpu_latency_average_ms=2.500000\n"
		"cpu_latency_p50_ms=2.250000\n"
		"cpu_latency_p95_ms=3.000000\n"
		"candidate_latency_average_ms=1.250000\n"
		"candidate_latency_p50_ms=1.125000\n"
		"candidate_latency_p95_ms=1.500000\n"
		"speedup_ratio=2.000000\n"
		"normalized_mae=0.062500\n"
		"foreground_intersection=90\n"
		"foreground_union=100\n"
		"foreground_iou=0.900000\n"
		"mae_gate_passed=false\n"
		"iou_gate_passed=false\n"
		"performance_gate_passed=true\n"
		"status=failed\n",
		"comparison formatter mutations must preserve metrics, gates, fixed precision, and terminal failure");
	expect_equal(windows_ml_smoke::comparison_exit_code(comparison), 4,
		     "a failed correctness gate must not accidentally return success or provider-unavailable");

	if (failures != 0) {
		std::cerr << failures << " assertion(s) failed\n";
		return 1;
	}
	return 0;
}

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "inference-runner.hpp"

#include "windows-ml-provider.hpp"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <winml/onnxruntime_cxx_api.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

namespace windows_ml_smoke {
namespace {

constexpr float kMaximumMeanAbsoluteError = 0.05F;
constexpr float kMinimumForegroundIou = 0.95F;
constexpr std::size_t kForegroundChannel = 1;
constexpr std::size_t kChannelCount = 2;
constexpr float kForegroundThreshold = 0.5F;

void require(bool condition, std::string_view message)
{
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

[[nodiscard]] std::size_t validate_output(const std::vector<Ort::Value> &output_tensors)
{
	require(output_tensors.size() == 1, "inference must return exactly one output tensor");
	require(output_tensors.front().IsTensor(), "inference output must be a tensor");
	const auto output_info = output_tensors.front().GetTensorTypeAndShapeInfo();
	require(output_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
		"inference output element type must be float");
	require(output_info.GetShape() ==
			std::vector<std::int64_t>(kExpectedOutputShape.begin(), kExpectedOutputShape.end()),
		"inference output shape must be 1x144x256x2");
	require(output_info.GetElementCount() == kOutputElementCount, "inference output must contain 73728 values");

	const float *output_data = output_tensors.front().GetTensorData<float>();
	const auto finite_count = static_cast<std::size_t>(std::count_if(
		output_data, output_data + kOutputElementCount, [](float value) { return std::isfinite(value); }));
	require(finite_count == kOutputElementCount, "every inference output value must be finite");
	return finite_count;
}

void copy_provider_diagnostics(InferenceResult &result, const windows_ml::ProviderSessionResult &provider)
{
	result.process_activation_attempted = provider.process_activation_attempted;
	result.provider_registration_succeeded = provider.provider_registration_succeeded;
	result.error_hresult = provider.error_hresult;
	result.error = provider.error;
	if (provider.selected_device.has_value()) {
		result.selected_ep_name = provider.selected_device->ep_name;
		result.selected_device_id = provider.selected_device->device_id;
		result.effective_provider = provider.selected_device->ep_name;
	}
}

} // namespace

InferenceResult run_inference(std::string_view model_path, std::string_view provider_name, std::size_t iterations,
			      bool benchmark_requested, std::span<const float> deterministic_input)
{
	InferenceResult result;
	result.requested_provider = provider_name;
	result.effective_provider = provider_name == "cpu" ? "cpu" : "";
	result.model_path = model_path;
	result.warmup_iterations = benchmark_requested ? kBenchmarkWarmupIterations : 0;
	result.iterations = benchmark_requested ? iterations : 1;

	try {
		Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "windows-ml-smoke");
		Ort::SessionOptions session_options;
		session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

		if (provider_name != "cpu") {
			try {
				const auto provider = windows_ml::configure_provider_session(
					environment, session_options, provider_name);
				copy_provider_diagnostics(result, provider);
				if (!provider.succeeded) {
					result.provider_failure = true;
					if (result.error.empty()) {
						result.error = "provider configuration failed without a diagnostic";
					}
					return result;
				}
			} catch (const Ort::Exception &exception) {
				result.provider_failure = true;
				result.error =
					std::string("ONNX Runtime provider configuration failure: ") + exception.what();
				return result;
			} catch (const std::exception &exception) {
				result.provider_failure = true;
				result.error = std::string("provider configuration failure: ") + exception.what();
				return result;
			} catch (...) {
				result.provider_failure = true;
				result.error = "unknown failure while configuring the ONNX Runtime provider";
				return result;
			}

			session_options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
			result.cpu_ep_fallback_disabled = true;
		}

		require(result.iterations > 0, "inference iterations must be positive");
		require(deterministic_input.size() == kInputElementCount,
			"deterministic input must contain 110592 values");

		const std::filesystem::path native_model_path(result.model_path);
		Ort::Session session(environment, native_model_path.c_str(), session_options);
		result.onnxruntime_version = OrtGetApiBase()->GetVersionString();
		result.input_count = session.GetInputCount();
		result.output_count = session.GetOutputCount();
		require(result.input_count == 1, "model must have exactly one input");
		require(result.output_count == 1, "model must have exactly one output");

		Ort::AllocatorWithDefaultOptions allocator;
		auto input_name = session.GetInputNameAllocated(0, allocator);
		auto output_name = session.GetOutputNameAllocated(0, allocator);
		require(input_name && input_name.get()[0] != '\0', "model input name is empty");
		require(output_name && output_name.get()[0] != '\0', "model output name is empty");
		result.input_name = input_name.get();
		result.output_name = output_name.get();

		const auto input_info = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
		const auto output_info = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo();
		result.input_shape = input_info.GetShape();
		result.output_shape = output_info.GetShape();
		require(input_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
			"model input element type must be float");
		require(output_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
			"model output element type must be float");
		require(result.input_shape ==
				std::vector<std::int64_t>(kExpectedInputShape.begin(), kExpectedInputShape.end()),
			"model input shape must be 1x144x256x3");
		require(result.output_shape ==
				std::vector<std::int64_t>(kExpectedOutputShape.begin(), kExpectedOutputShape.end()),
			"model output shape must be 1x144x256x2");
		require(input_info.GetElementCount() == kInputElementCount, "model input must contain 110592 values");
		require(output_info.GetElementCount() == kOutputElementCount, "model output must contain 73728 values");

		const auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		auto input_tensor = Ort::Value::CreateTensor<float>(
			memory_info, const_cast<float *>(deterministic_input.data()), deterministic_input.size(),
			kExpectedInputShape.data(), kExpectedInputShape.size());
		const char *input_names[]{result.input_name.c_str()};
		const char *output_names[]{result.output_name.c_str()};
		const Ort::RunOptions run_options{nullptr};

		for (std::size_t iteration = 0; iteration < result.warmup_iterations; ++iteration) {
			(void)session.Run(run_options, input_names, &input_tensor, 1, output_names, 1);
		}

		std::vector<double> latencies;
		latencies.reserve(result.iterations);
		for (std::size_t iteration = 0; iteration < result.iterations; ++iteration) {
			const auto inference_start = std::chrono::steady_clock::now();
			auto output_tensors = session.Run(run_options, input_names, &input_tensor, 1, output_names, 1);
			const auto inference_end = std::chrono::steady_clock::now();
			const auto latency_ms =
				std::chrono::duration<double, std::milli>(inference_end - inference_start).count();
			require(std::isfinite(latency_ms) && latency_ms >= 0.0,
				"inference latency must be finite and nonnegative");
			latencies.push_back(latency_ms);

			result.finite_output_count = validate_output(output_tensors);
			if (iteration + 1 == result.iterations) {
				const float *output_data = output_tensors.front().GetTensorData<float>();
				result.output.assign(output_data, output_data + kOutputElementCount);
			}
		}

		result.latency = summarize_latencies(latencies);
		result.succeeded = true;
	} catch (const Ort::Exception &exception) {
		result.error = std::string("ONNX Runtime failure: ") + exception.what();
	} catch (const std::exception &exception) {
		result.error = exception.what();
	} catch (...) {
		result.error = "unknown failure while running ONNX Runtime inference";
	}
	return result;
}

ComparisonResult run_comparison(std::string_view model_path, std::string_view candidate_provider_name,
				std::size_t iterations, std::span<const float> deterministic_input)
{
	ComparisonResult result;
	result.candidate.requested_provider = candidate_provider_name;
	result.candidate.model_path = model_path;
	result.candidate.warmup_iterations = kBenchmarkWarmupIterations;
	result.candidate.iterations = iterations;
	result.cpu = run_inference(model_path, "cpu", iterations, true, deterministic_input);
	if (!result.cpu.succeeded) {
		result.error_hresult = result.cpu.error_hresult;
		result.error = result.cpu.error;
		return result;
	}

	result.candidate = run_inference(model_path, candidate_provider_name, iterations, true, deterministic_input);
	if (!result.candidate.succeeded) {
		result.error_hresult = result.candidate.error_hresult;
		result.error = result.candidate.error;
		return result;
	}

	try {
		result.comparison = compare_outputs(result.cpu.output, result.candidate.output, kForegroundChannel,
						    kChannelCount, kForegroundThreshold);
		result.mae_gate_passed = result.comparison.mean_absolute_error <= kMaximumMeanAbsoluteError;
		result.iou_gate_passed = result.comparison.foreground_iou >= kMinimumForegroundIou;
		result.performance_gate_passed = result.candidate.latency.average_ms < result.cpu.latency.average_ms;
		result.speedup_ratio =
			calculate_speedup_ratio(result.cpu.latency.average_ms, result.candidate.latency.average_ms);
		result.succeeded = result.mae_gate_passed && result.iou_gate_passed && result.performance_gate_passed;
		if (!result.succeeded) {
			result.error = "inference comparison did not satisfy every correctness and performance gate";
		}
	} catch (const std::exception &exception) {
		result.error = exception.what();
	}
	return result;
}

} // namespace windows_ml_smoke

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winternl.h>

#include <winml/onnxruntime_cxx_api.h>

#include "cli.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr int kSuccessExitCode = 0;
constexpr int kUsageExitCode = 2;
constexpr int kUnsupportedPlatformExitCode = 3;
constexpr int kInferenceExitCode = 4;
constexpr std::size_t kInputElementCount = 110592;
constexpr std::size_t kOutputElementCount = 73728;
const std::vector<int64_t> kExpectedInputShape{1, 144, 256, 3};
const std::vector<int64_t> kExpectedOutputShape{1, 144, 256, 2};

void print_error(std::string_view message)
{
	std::cerr << "error=";
	for (const char character : message) {
		std::cerr << (character == '\r' || character == '\n' ? ' ' : character);
	}
	std::cerr << '\n';
}

[[nodiscard]] std::optional<std::string> native_windows_version(std::string &error)
{
	const auto ntdll = GetModuleHandleW(L"ntdll.dll");
	if (ntdll == nullptr) {
		error = "cannot access ntdll.dll to query the native Windows version";
		return std::nullopt;
	}

	using RtlGetVersionFunction = LONG(WINAPI *)(PRTL_OSVERSIONINFOW);
	const auto rtl_get_version = reinterpret_cast<RtlGetVersionFunction>(GetProcAddress(ntdll, "RtlGetVersion"));
	if (rtl_get_version == nullptr) {
		error = "RtlGetVersion is unavailable; the native Windows version cannot be verified";
		return std::nullopt;
	}

	RTL_OSVERSIONINFOW version{};
	version.dwOSVersionInfoSize = sizeof(version);
	if (rtl_get_version(&version) != 0) {
		error = "RtlGetVersion failed; the native Windows version cannot be verified";
		return std::nullopt;
	}

	std::ostringstream report;
	report << version.dwMajorVersion << '.' << version.dwMinorVersion << '.' << version.dwBuildNumber;
	return report.str();
}

void require(bool condition, std::string_view message)
{
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

[[nodiscard]] std::string shape_string(std::span<const int64_t> shape)
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

} // namespace

int main(int argc, char **argv)
{
	std::vector<std::string_view> arguments;
	arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
	for (int index = 1; index < argc; ++index) {
		arguments.emplace_back(argv[index]);
	}

	const auto parsed = windows_ml_smoke::parse_cli(std::span<const std::string_view>(arguments));
	if (!parsed.ok()) {
		print_error(parsed.error);
		std::cerr << windows_ml_smoke::kUsage << '\n';
		return kUsageExitCode;
	}

#if !defined(_M_X64)
	print_error("unsupported architecture: windows-ml-smoke requires x64");
	return kUnsupportedPlatformExitCode;
#else
	std::string version_error;
	const auto windows_version = native_windows_version(version_error);
	if (!windows_version.has_value()) {
		print_error(version_error);
		return kUnsupportedPlatformExitCode;
	}

	try {
		const auto &model_argument = parsed.options->model_path;
		const std::filesystem::path model_path(model_argument);

		Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "windows-ml-smoke");
		Ort::SessionOptions session_options;
		session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		Ort::Session session(environment, model_path.c_str(), session_options);

		const auto input_count = session.GetInputCount();
		const auto output_count = session.GetOutputCount();
		require(input_count == 1, "model must have exactly one input");
		require(output_count == 1, "model must have exactly one output");

		Ort::AllocatorWithDefaultOptions allocator;
		auto input_name = session.GetInputNameAllocated(0, allocator);
		auto output_name = session.GetOutputNameAllocated(0, allocator);
		require(input_name && input_name.get()[0] != '\0', "model input name is empty");
		require(output_name && output_name.get()[0] != '\0', "model output name is empty");

		const auto input_type_info = session.GetInputTypeInfo(0);
		const auto output_type_info = session.GetOutputTypeInfo(0);
		const auto input_info = input_type_info.GetTensorTypeAndShapeInfo();
		const auto output_info = output_type_info.GetTensorTypeAndShapeInfo();
		const auto input_shape = input_info.GetShape();
		const auto output_shape = output_info.GetShape();
		require(input_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
			"model input element type must be float");
		require(output_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
			"model output element type must be float");
		require(input_shape == kExpectedInputShape, "model input shape must be 1x144x256x3");
		require(output_shape == kExpectedOutputShape, "model output shape must be 1x144x256x2");
		require(input_info.GetElementCount() == kInputElementCount, "model input must contain 110592 values");
		require(output_info.GetElementCount() == kOutputElementCount, "model output must contain 73728 values");

		std::vector<float> input_data(kInputElementCount, 0.0F);
		const auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		auto input_tensor = Ort::Value::CreateTensor<float>(memory_info, input_data.data(), input_data.size(),
								    input_shape.data(), input_shape.size());
		const char *input_names[]{input_name.get()};
		const char *output_names[]{output_name.get()};
		const Ort::RunOptions run_options{nullptr};

		const auto inference_start = std::chrono::steady_clock::now();
		auto output_tensors = session.Run(run_options, input_names, &input_tensor, 1, output_names, 1);
		const auto inference_end = std::chrono::steady_clock::now();
		const auto latency_ms =
			std::chrono::duration<double, std::milli>(inference_end - inference_start).count();

		require(latency_ms >= 0.0, "inference latency must be nonnegative");
		require(output_tensors.size() == 1, "inference must return exactly one output tensor");
		require(output_tensors.front().IsTensor(), "inference output must be a tensor");
		const auto inference_output_info = output_tensors.front().GetTensorTypeAndShapeInfo();
		require(inference_output_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
			"inference output element type must be float");
		require(inference_output_info.GetShape() == kExpectedOutputShape,
			"inference output shape must be 1x144x256x2");
		require(inference_output_info.GetElementCount() == kOutputElementCount,
			"inference output must contain 73728 values");

		const float *output_data = output_tensors.front().GetTensorData<float>();
		const auto finite_output_count =
			static_cast<std::size_t>(std::count_if(output_data, output_data + kOutputElementCount,
							       [](float value) { return std::isfinite(value); }));
		require(finite_output_count == kOutputElementCount, "every inference output value must be finite");

		std::cout << "provider=cpu\n";
		std::cout << "architecture=x64\n";
		std::cout << "windows_version=" << *windows_version << '\n';
		std::cout << "windows_ml_package_version=" << WINDOWS_ML_PACKAGE_VERSION << '\n';
		std::cout << "onnxruntime_version=" << OrtGetApiBase()->GetVersionString() << '\n';
		std::cout << "model=" << model_argument << '\n';
		std::cout << "input_count=" << input_count << '\n';
		std::cout << "output_count=" << output_count << '\n';
		std::cout << "input_name=" << input_name.get() << '\n';
		std::cout << "output_name=" << output_name.get() << '\n';
		std::cout << "input_element_type=float\n";
		std::cout << "output_element_type=float\n";
		std::cout << "input_shape=" << shape_string(input_shape) << '\n';
		std::cout << "output_shape=" << shape_string(output_shape) << '\n';
		std::cout << "iterations=1\n";
		std::cout << "finite_output_count=" << finite_output_count << '\n';
		std::cout << "latency_ms=" << std::fixed << std::setprecision(6) << latency_ms << '\n';
		std::cout << "status=ok\n";
		return kSuccessExitCode;
	} catch (const Ort::Exception &exception) {
		print_error(std::string("ONNX Runtime failure: ") + exception.what());
	} catch (const std::exception &exception) {
		print_error(exception.what());
	}
	return kInferenceExitCode;
#endif
}

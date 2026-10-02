// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

#include "windows-ml-session.hpp"
#include "benchmark.hpp"
#include "inference-runner.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace smoke = windows_ml_smoke;

struct Arguments {
	std::string provider;
	std::filesystem::path model;
	bool require_effective{};
};

struct CycleReport {
	windows_ml::SessionDiagnostics diagnostics;
	std::size_t input_count{};
	std::size_t output_count{};
	std::size_t input_elements{};
	std::size_t output_elements{};
	std::string input_shape;
	std::string output_shape;
	std::size_t warmups{};
	std::size_t iterations{};
	std::size_t finite_output_count{};
	smoke::LatencySummary latency{};
	smoke::LatencySummary baseline_latency{};
	smoke::OutputComparison comparison{};
	bool compared{};
	std::vector<float> output;
	int exit_code{4};
};

void require(bool condition, std::string_view message)
{
	if (!condition) {
		throw std::runtime_error(std::string(message));
	}
}

std::string single_line(std::string value)
{
	std::replace(value.begin(), value.end(), '\n', ' ');
	std::replace(value.begin(), value.end(), '\r', ' ');
	return value;
}

Arguments parse_arguments(int argc, char **argv)
{
	Arguments arguments;
	bool provider_seen = false;
	bool model_seen = false;
	bool iterations_seen = false;
	bool cycles_seen = false;
	for (int index = 1; index < argc; ++index) {
		const std::string_view option(argv[index]);
		if (option == "--require-effective") {
			require(!arguments.require_effective, "duplicate --require-effective");
			arguments.require_effective = true;
			continue;
		}
		require(index + 1 < argc, "option requires a value");
		const std::string_view value(argv[++index]);
		if (option == "--provider") {
			require(!provider_seen, "duplicate --provider");
			provider_seen = true;
			arguments.provider = value;
		} else if (option == "--model") {
			require(!model_seen && !value.empty(), "--model requires a nonempty path once");
			model_seen = true;
			arguments.model = std::filesystem::path(std::u8string(value.begin(), value.end()));
		} else if (option == "--iterations") {
			require(!iterations_seen && value == "100", "acceptance requires --iterations 100");
			iterations_seen = true;
		} else if (option == "--cycles") {
			require(!cycles_seen && value == "3", "acceptance requires --cycles 3");
			cycles_seen = true;
		} else {
			throw std::runtime_error("unknown option");
		}
	}
	require(provider_seen && model_seen && iterations_seen && cycles_seen,
		"required: --provider cpu|winml-directml|winml-migraphx --model PATH --iterations 100 --cycles 3 [--require-effective]");
	require(arguments.provider == "cpu" || arguments.provider == "winml-directml" ||
			arguments.provider == "winml-migraphx",
		"unknown provider identifier");
	return arguments;
}

void validate_tracked_model(const std::filesystem::path &model)
{
	require(std::filesystem::is_regular_file(model), "model must be a regular file");
	require(std::filesystem::file_size(model) == WINDOWS_ML_MEDIAPIPE_SIZE,
		"model size differs from the tracked MediaPipe model");
	std::ifstream file(model, std::ios::binary);
	require(file.is_open(), "could not open model");
	std::vector<BYTE> bytes(WINDOWS_ML_MEDIAPIPE_SIZE);
	file.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	require(file.gcount() == static_cast<std::streamsize>(bytes.size()) && file.peek() == EOF,
		"could not read the exact tracked model size");
	std::array<BYTE, 32> hash{};
	DWORD hash_size = static_cast<DWORD>(hash.size());
	require(CryptHashCertificate2(L"SHA256", 0, nullptr, bytes.data(), static_cast<DWORD>(bytes.size()),
				      hash.data(), &hash_size) != FALSE &&
			hash_size == hash.size(),
		"model SHA-256 calculation failed");
	constexpr char digits[] = "0123456789abcdef";
	std::string hexadecimal;
	for (const auto byte : hash) {
		hexadecimal += digits[byte >> 4U];
		hexadecimal += digits[byte & 15U];
	}
	require(hexadecimal == WINDOWS_ML_MEDIAPIPE_SHA256, "model SHA-256 differs from the tracked MediaPipe model");
}

std::string shape_text(const std::vector<std::int64_t> &shape)
{
	std::string text;
	for (const auto dimension : shape) {
		if (!text.empty()) {
			text += 'x';
		}
		text += std::to_string(dimension);
	}
	return text;
}

CycleReport run_cycle(const Arguments &arguments, std::span<const float> input)
{
	CycleReport report;
	try {
		// Reverse destruction releases tensors/session before their owning environment.
		Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "windows-ml-session-check");
		auto created = windows_ml::create_session(
			environment, arguments.model,
			{.requested_provider = arguments.provider, .gpu_model_eligible = true, .cpu_threads = 0});
		report.diagnostics = created.diagnostics;
		require(created.session != nullptr, "production session initialization failed");
		auto &session = *created.session;
		report.input_count = session.GetInputCount();
		report.output_count = session.GetOutputCount();
		require(report.input_count == 1 && report.output_count == 1, "model requires one input and one output");
		Ort::AllocatorWithDefaultOptions allocator;
		const auto input_name = session.GetInputNameAllocated(0, allocator);
		const auto output_name = session.GetOutputNameAllocated(0, allocator);
		require(input_name && input_name.get()[0] && output_name && output_name.get()[0],
			"tensor name is empty");
		// Tensor metadata views borrow from TypeInfo: retain both owners through validation.
		const auto input_type = session.GetInputTypeInfo(0);
		const auto output_type = session.GetOutputTypeInfo(0);
		const auto input_info = input_type.GetTensorTypeAndShapeInfo();
		const auto output_info = output_type.GetTensorTypeAndShapeInfo();
		report.input_shape = shape_text(input_info.GetShape());
		report.output_shape = shape_text(output_info.GetShape());
		report.input_elements = input_info.GetElementCount();
		report.output_elements = output_info.GetElementCount();
		require(input_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
				output_info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
			"model tensors must be float");
		require(report.input_shape == "1x144x256x3" && report.output_shape == "1x144x256x2" &&
				report.input_elements == smoke::kInputElementCount &&
				report.output_elements == smoke::kOutputElementCount &&
				input.size() == smoke::kInputElementCount,
			"model tensor shape or element count differs from MediaPipe");
		const auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		auto tensor = Ort::Value::CreateTensor<float>(memory, const_cast<float *>(input.data()), input.size(),
							      smoke::kExpectedInputShape.data(),
							      smoke::kExpectedInputShape.size());
		const char *inputs[]{input_name.get()};
		const char *outputs[]{output_name.get()};
		const Ort::RunOptions run_options{nullptr};
		std::vector<double> latencies;
		for (std::size_t call = 0; call < smoke::kBenchmarkWarmupIterations + 100; ++call) {
			const auto start = std::chrono::steady_clock::now();
			auto values = session.Run(run_options, inputs, &tensor, 1, outputs, 1);
			const auto end = std::chrono::steady_clock::now();
			require(values.size() == 1 && values.front().IsTensor(), "Run must return one tensor");
			const auto info = values.front().GetTensorTypeAndShapeInfo();
			require(info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT &&
					info.GetShape() ==
						std::vector<std::int64_t>(smoke::kExpectedOutputShape.begin(),
									  smoke::kExpectedOutputShape.end()) &&
					info.GetElementCount() == smoke::kOutputElementCount,
				"Run returned an invalid output tensor");
			const float *data = values.front().GetTensorData<float>();
			report.finite_output_count = static_cast<std::size_t>(
				std::count_if(data, data + smoke::kOutputElementCount,
					      [](float value) { return std::isfinite(value); }));
			require(report.finite_output_count == smoke::kOutputElementCount,
				"Run returned nonfinite output");
			if (call < smoke::kBenchmarkWarmupIterations) {
				++report.warmups;
			} else {
				latencies.push_back(std::chrono::duration<double, std::milli>(end - start).count());
				++report.iterations;
			}
			if (call + 1 == smoke::kBenchmarkWarmupIterations + 100) {
				report.output.assign(data, data + smoke::kOutputElementCount);
			}
		}
		report.latency = smoke::summarize_latencies(latencies);
		report.diagnostics.outcome = windows_ml::SessionOutcome::Ready;
		const bool effective = report.diagnostics.effective_provider ==
				       report.diagnostics.requested_runtime_provider;
		// Fallback is observable production behavior, but cannot pass GPU acceptance even without the flag.
		report.exit_code = (!effective && (arguments.require_effective || arguments.provider != "cpu")) ? 3 : 0;
		if (arguments.provider != "cpu" && report.exit_code == 0) {
			require(report.diagnostics.provider_attempt &&
					report.diagnostics.provider_attempt->selected_device &&
					report.diagnostics.provider_attempt->selected_device->vendor_id == 0x1002 &&
					report.diagnostics.provider_attempt->selected_device->hardware_type == "gpu",
				"effective GPU must identify the selected AMD GPU");
		}
	} catch (const std::exception &error) {
		report.diagnostics.outcome = windows_ml::SessionOutcome::Failed;
		report.diagnostics.effective_provider.clear();
		if (!report.diagnostics.error.empty()) {
			report.diagnostics.error += "; ";
		}
		report.diagnostics.error += single_line(error.what());
		report.exit_code = 4;
	}
	return report;
}

void print_cycle(const CycleReport &report, std::size_t cycle)
{
	const auto prefix = "cycle." + std::to_string(cycle) + ".";
	const auto field = [&prefix](std::string_view name, const auto &value) {
		std::cout << prefix << name << '=' << value << '\n';
	};
	field("requested_provider", report.diagnostics.requested_provider);
	field("requested_runtime_provider", report.diagnostics.requested_runtime_provider);
	field("effective_provider", report.diagnostics.effective_provider);
	field("fallback_reason", report.diagnostics.fallback_reason);
	field("error", single_line(report.diagnostics.error));
	field("cpu_error", single_line(report.diagnostics.cpu_error));
	const auto &attempt = report.diagnostics.provider_attempt;
	const bool active_gpu = report.diagnostics.effective_provider != "CPUExecutionProvider" &&
				!report.diagnostics.effective_provider.empty() && attempt && attempt->selected_device;
	field("selected_vendor_id", active_gpu ? std::to_string(attempt->selected_device->vendor_id) : "");
	field("selected_device_id", active_gpu ? std::to_string(attempt->selected_device->device_id) : "");
	field("input_count", report.input_count);
	field("output_count", report.output_count);
	field("input_shape", report.input_shape);
	field("output_shape", report.output_shape);
	field("input_element_count", report.input_elements);
	field("output_element_count", report.output_elements);
	field("finite_output_count", report.finite_output_count);
	field("warmup_iterations", report.warmups);
	field("iterations", report.iterations);
	if (report.compared) {
		field("mae", report.comparison.mean_absolute_error);
		field("iou", report.comparison.foreground_iou);
		field("cpu_latency_average_ms", report.baseline_latency.average_ms);
		field("cpu_latency_p50_ms", report.baseline_latency.median_ms);
		field("cpu_latency_p95_ms", report.baseline_latency.p95_ms);
	} else {
		field("mae", "");
		field("iou", "");
		field("cpu_latency_average_ms", "");
		field("cpu_latency_p50_ms", "");
		field("cpu_latency_p95_ms", "");
	}
	field("latency_average_ms", report.latency.average_ms);
	field("latency_p50_ms", report.latency.median_ms);
	field("latency_p95_ms", report.latency.p95_ms);
	field("status", report.exit_code == 0 ? "ok" : "error");
}
} // namespace

int main(int argc, char **argv)
{
	Arguments arguments;
	try {
		arguments = parse_arguments(argc, argv);
	} catch (const std::exception &error) {
		std::cerr << "error=" << single_line(error.what()) << '\n';
		std::cout << "status=error\n";
		return 2;
	}
	std::cout << std::fixed << std::setprecision(8) << "requested_provider=" << arguments.provider << '\n'
		  << "windows_ml_package_version=" << WINDOWS_ML_PACKAGE_VERSION << '\n'
		  << "onnxruntime_version=" << OrtGetApiBase()->GetVersionString() << '\n'
		  << "model_sha256=" << WINDOWS_ML_MEDIAPIPE_SHA256 << '\n'
		  << "cycles=3\n";
	std::size_t completed = 0;
	int exit_code = 0;
	try {
		validate_tracked_model(arguments.model);
		const auto input = smoke::make_deterministic_input(smoke::kInputElementCount);
		for (std::size_t cycle = 0; cycle < 3; ++cycle) {
			auto report = run_cycle(arguments, input);
			if (report.exit_code == 0) {
				try {
					const auto baseline = arguments.provider == "cpu"
								      ? report
								      : run_cycle({.provider = "cpu",
										   .model = arguments.model,
										   .require_effective = true},
										  input);
					require(baseline.exit_code == 0,
						"CPU reference failed: " + baseline.diagnostics.error +
							baseline.diagnostics.cpu_error);
					report.comparison =
						smoke::compare_outputs(baseline.output, report.output, 1, 2, 0.5F);
					report.baseline_latency = baseline.latency;
					report.compared = true;
					if (report.comparison.mean_absolute_error > 0.05F ||
					    report.comparison.foreground_iou < 0.95F) {
						report.diagnostics.error = "GPU output failed CPU comparison gates";
						report.exit_code = 4;
					}
				} catch (const std::exception &error) {
					report.diagnostics.error = single_line(error.what());
					report.exit_code = 4;
				}
			}
			print_cycle(report, cycle);
			if (report.exit_code != 0) {
				exit_code = report.exit_code;
				break;
			}
			++completed;
		}
	} catch (const std::exception &error) {
		std::cerr << "error=" << single_line(error.what()) << '\n';
		exit_code = 4;
	}
	std::cout << "completed_cycles=" << completed << '\n' << "status=" << (exit_code == 0 ? "ok" : "error") << '\n';
	return exit_code;
}

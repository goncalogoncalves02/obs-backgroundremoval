// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <winternl.h>

#include <winml/onnxruntime_cxx_api.h>

#include "benchmark-report.hpp"
#include "benchmark.hpp"
#include "cli.hpp"
#include "inference-runner.hpp"
#include "provider-report.hpp"
#include "windows-ml-provider.hpp"

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
constexpr int kProviderExitCode = 5;

void print_error(std::string_view message)
{
	std::cerr << "error=" << windows_ml_smoke::sanitize_single_line(message) << '\n';
}

[[nodiscard]] std::string provider_error(std::string_view message, const std::optional<std::uint32_t> &hresult)
{
	const auto detail = message.empty() ? std::string("provider operation failed without a diagnostic")
					    : std::string(message);
	if (!hresult.has_value()) {
		return detail;
	}
	return "HRESULT " + windows_ml_smoke::format_hresult(*hresult) + ": " + detail;
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

	if (parsed.options->command == windows_ml_smoke::CliCommand::ListProviders) {
		const auto discovery = windows_ml::discover_providers();
		std::cout << windows_ml_smoke::format_provider_discovery_report(discovery);
		if (!discovery.succeeded) {
			print_error(provider_error(discovery.error, discovery.error_hresult));
			return kProviderExitCode;
		}
		return kSuccessExitCode;
	}

	if (parsed.options->command == windows_ml_smoke::CliCommand::PrepareProvider) {
		windows_ml::ProviderPreparationResult preparation;
		preparation.requested_provider_name = parsed.options->provider_name;
		try {
			Ort::Env environment(ORT_LOGGING_LEVEL_WARNING, "windows-ml-smoke-provider");
			preparation = windows_ml::prepare_provider(environment, parsed.options->provider_name);
		} catch (const Ort::Exception &exception) {
			preparation.error = std::string("ONNX Runtime environment failure: ") + exception.what();
		} catch (const std::exception &exception) {
			preparation.error = exception.what();
		} catch (...) {
			preparation.error = "unknown failure while creating the ONNX Runtime environment";
		}

		std::cout << windows_ml_smoke::format_provider_preparation_report(preparation);
		if (!preparation.succeeded) {
			print_error(provider_error(preparation.error, preparation.error_hresult));
			return kProviderExitCode;
		}
		return kSuccessExitCode;
	}

	const auto deterministic_input =
		windows_ml_smoke::make_deterministic_input(windows_ml_smoke::kInputElementCount);
	if (parsed.options->command == windows_ml_smoke::CliCommand::Compare) {
		const auto comparison = windows_ml_smoke::run_comparison(parsed.options->model_path,
									 parsed.options->comparison_provider_name,
									 parsed.options->iterations,
									 deterministic_input);
		std::cout << windows_ml_smoke::format_comparison_report(comparison);
		if (!comparison.succeeded) {
			print_error(provider_error(comparison.error, comparison.error_hresult));
		}
		return windows_ml_smoke::comparison_exit_code(comparison);
	}

	const auto inference = windows_ml_smoke::run_inference(
		parsed.options->model_path, parsed.options->provider_name, parsed.options->iterations,
		parsed.options->benchmark_requested, deterministic_input);
	if (parsed.options->benchmark_requested) {
		std::cout << windows_ml_smoke::format_benchmark_inference_report(inference);
	}
	if (!inference.succeeded) {
		print_error(provider_error(inference.error, inference.error_hresult));
		return windows_ml_smoke::inference_exit_code(inference);
	}
	if (!parsed.options->benchmark_requested) {
		std::cout << windows_ml_smoke::format_legacy_inference_report(inference, *windows_version,
									      WINDOWS_ML_PACKAGE_VERSION);
	}
	return kSuccessExitCode;
#endif
}

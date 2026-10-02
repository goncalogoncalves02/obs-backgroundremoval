// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-session-policy.hpp"

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view message)
{
	if (!condition) {
		std::cerr << "FAIL: " << message << '\n';
		++failures;
	}
}

void cpu_skips_gpu()
{
	const windows_ml::SessionRequest request{"cpu", true, 0};
	const auto resolved = windows_ml::resolve_session_request(request);
	expect(resolved.route == windows_ml::SessionRoute::Cpu, "CPU request uses CPU route");
	expect(resolved.requested_runtime_provider == "CPUExecutionProvider", "CPU exact requested name");
	expect(resolved.runtime_provider == "CPUExecutionProvider", "CPU exact effective name");
	expect(resolved.fallback_reason.empty(), "CPU request is not a fallback");
	expect(resolved.error.empty(), "CPU request has no error");
	expect(request.requested_provider == "cpu" && request.gpu_model_eligible && request.cpu_threads == 0,
	       "resolution leaves stored CPU request unchanged");
}

void exact_gpu_mapping()
{
	const windows_ml::SessionRequest directml{"winml-directml", true, 1};
	const auto dml = windows_ml::resolve_session_request(directml);
	expect(dml.route == windows_ml::SessionRoute::Gpu, "DirectML uses GPU route");
	expect(dml.requested_runtime_provider == "DmlExecutionProvider", "DirectML requested exact mapping");
	expect(dml.runtime_provider == "DmlExecutionProvider", "DirectML exact mapping");
	expect(dml.fallback_reason.empty() && dml.error.empty(), "eligible DirectML has no fallback or error");
	expect(directml.requested_provider == "winml-directml" && directml.gpu_model_eligible &&
		       directml.cpu_threads == 1,
	       "resolution leaves stored DirectML request unchanged");

	const windows_ml::SessionRequest migraphx{"winml-migraphx", true, 2};
	const auto mig = windows_ml::resolve_session_request(migraphx);
	expect(mig.route == windows_ml::SessionRoute::Gpu, "MIGraphX uses GPU route");
	expect(mig.requested_runtime_provider == "MIGraphXExecutionProvider", "MIGraphX requested exact mapping");
	expect(mig.runtime_provider == "MIGraphXExecutionProvider", "MIGraphX exact mapping");
	expect(mig.fallback_reason.empty() && mig.error.empty(), "eligible MIGraphX has no fallback or error");
	expect(migraphx.requested_provider == "winml-migraphx" && migraphx.gpu_model_eligible &&
		       migraphx.cpu_threads == 2,
	       "resolution leaves stored MIGraphX request unchanged");
}

void unsupported_model_uses_cpu()
{
	struct Case {
		const char *stored_name;
		const char *runtime_name;
	};
	constexpr Case cases[] = {
		{"winml-directml", "DmlExecutionProvider"},
		{"winml-migraphx", "MIGraphXExecutionProvider"},
	};
	for (const auto &test_case : cases) {
		const windows_ml::SessionRequest request{test_case.stored_name, false, 0};
		const auto resolved = windows_ml::resolve_session_request(request);
		expect(resolved.route == windows_ml::SessionRoute::Cpu, "ineligible GPU request uses CPU route");
		expect(resolved.requested_runtime_provider == test_case.runtime_name,
		       "fallback preserves GPU runtime request");
		expect(resolved.runtime_provider == "CPUExecutionProvider", "fallback uses exact CPU name");
		expect(resolved.fallback_reason == "unsupported_model", "fallback records unsupported_model");
		expect(resolved.error.empty(), "recognized fallback is not an error");
		expect(request.requested_provider == test_case.stored_name && !request.gpu_model_eligible &&
			       request.cpu_threads == 0,
		       "resolution leaves stored ineligible request unchanged");
	}
}

void unknown_windows_identifier_rejected()
{
	for (const auto *provider : {"", "dml", "directml", "migraphx", "winml-unknown", "cuda", "rocm"}) {
		const windows_ml::SessionRequest request{provider, true, 1};
		const auto resolved = windows_ml::resolve_session_request(request);
		expect(resolved.route == windows_ml::SessionRoute::Reject, "unknown Windows identifier is rejected");
		expect(resolved.requested_runtime_provider.empty() && resolved.runtime_provider.empty(),
		       "rejected identifier maps to no runtime provider");
		expect(resolved.fallback_reason.empty() && !resolved.error.empty(),
		       "rejected identifier records an error without fallback");
		expect(request.requested_provider == provider && request.gpu_model_eligible && request.cpu_threads == 1,
		       "resolution leaves rejected request unchanged");
	}
}

void invalid_thread_count_rejected()
{
	for (const auto *provider : {"cpu", "winml-directml", "winml-migraphx"}) {
		const windows_ml::SessionRequest request{provider, true, -1};
		const auto resolved = windows_ml::resolve_session_request(request);
		expect(resolved.route == windows_ml::SessionRoute::Reject, "negative CPU thread count is rejected");
		expect(!resolved.error.empty(), "negative CPU thread count explains rejection");
		expect(resolved.runtime_provider.empty(), "invalid thread count selects no runtime provider");
	}
	const auto zero_threads = windows_ml::resolve_session_request({"winml-directml", true, 0});
	expect(zero_threads.route == windows_ml::SessionRoute::Gpu &&
		       zero_threads.runtime_provider == "DmlExecutionProvider",
	       "zero CPU threads preserves ORT default behavior and GPU selection");
}

} // namespace

int main()
{
	cpu_skips_gpu();
	exact_gpu_mapping();
	unsupported_model_uses_cpu();
	unknown_windows_identifier_rejected();
	invalid_thread_count_rejected();
	if (failures != 0) {
		std::cerr << failures << " assertion(s) failed\n";
		return 1;
	}
	return 0;
}

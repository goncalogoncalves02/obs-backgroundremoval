// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-session-policy.hpp"

namespace windows_ml {

ResolvedSessionRequest resolve_session_request(const SessionRequest &request)
{
	if (request.cpu_threads < 0) {
		return {.error = "invalid CPU thread count"};
	}
	if (request.requested_provider == "cpu") {
		return {.route = SessionRoute::Cpu,
			.requested_runtime_provider = "CPUExecutionProvider",
			.runtime_provider = "CPUExecutionProvider"};
	}

	std::string runtime_provider;
	if (request.requested_provider == "winml-directml") {
		runtime_provider = "DmlExecutionProvider";
	} else if (request.requested_provider == "winml-migraphx") {
		runtime_provider = "MIGraphXExecutionProvider";
	} else {
		return {.error = "unsupported Windows ML provider identifier"};
	}

	if (!request.gpu_model_eligible) {
		return {.route = SessionRoute::Cpu,
			.requested_runtime_provider = runtime_provider,
			.runtime_provider = "CPUExecutionProvider",
			.fallback_reason = "unsupported_model"};
	}
	return {.route = SessionRoute::Gpu,
		.requested_runtime_provider = runtime_provider,
		.runtime_provider = runtime_provider};
}

} // namespace windows_ml

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "windows-ml-provider.hpp"

#include <optional>
#include <string>

namespace windows_ml {

struct SessionRequest {
	std::string requested_provider;
	bool gpu_model_eligible{};
	int cpu_threads{};
};

enum class SessionRoute {
	Cpu,
	Gpu,
	Reject,
};

struct ResolvedSessionRequest {
	SessionRoute route{SessionRoute::Reject};
	std::string requested_runtime_provider;
	std::string runtime_provider;
	std::string fallback_reason;
	std::string error;
};

[[nodiscard]] ResolvedSessionRequest resolve_session_request(const SessionRequest &request);

enum class SessionOutcome {
	NotInitialized,
	Constructed,
	Ready,
	Failed,
};

struct SessionDiagnostics {
	std::string requested_provider;
	std::string requested_runtime_provider;
	std::string effective_provider;
	std::string fallback_reason;
	std::string error;
	std::string cpu_error;
	std::optional<ProviderSessionResult> provider_attempt;
	SessionOutcome outcome{SessionOutcome::NotInitialized};
};

} // namespace windows_ml

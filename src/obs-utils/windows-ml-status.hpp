// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "../ort-utils/windows-ml-session-policy.hpp"
#include <string_view>

namespace windows_ml {

inline std::string_view session_status_text_key(const SessionDiagnostics &diagnostic)
{
	if (diagnostic.outcome == SessionOutcome::Failed) {
		return "InferenceStatusFailed";
	}
	if (diagnostic.outcome != SessionOutcome::Ready) {
		return "InferenceStatusPending";
	}
	if (diagnostic.effective_provider == "DmlExecutionProvider") {
		return "InferenceStatusDirectML";
	}
	if (diagnostic.effective_provider == "CPUExecutionProvider") {
		if (diagnostic.fallback_reason == "unsupported_model") {
			return "InferenceStatusCpuModel";
		}
		if (diagnostic.requested_provider != "cpu" || !diagnostic.fallback_reason.empty()) {
			return "InferenceStatusCpuFallback";
		}
		return "InferenceStatusCPU";
	}
	return "InferenceStatusUnknown";
}

} // namespace windows_ml

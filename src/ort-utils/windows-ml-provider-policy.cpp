// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-provider-policy.hpp"

#include <algorithm>
#include <utility>

namespace windows_ml {

ProviderActivationAction activation_action(ProviderReadyState ready_state) noexcept
{
	switch (ready_state) {
	case ProviderReadyState::Ready:
		return ProviderActivationAction::RegisterReady;
	case ProviderReadyState::NotReady:
		return ProviderActivationAction::ActivateInstalledThenRegister;
	case ProviderReadyState::NotPresent:
	case ProviderReadyState::Unknown:
		return ProviderActivationAction::UnavailableWithoutActivation;
	}
	return ProviderActivationAction::UnavailableWithoutActivation;
}

void assign_sanitized_diagnostic(std::string &destination, std::string_view first, std::string_view second,
				 std::string_view third) noexcept
{
	try {
		std::string sanitized;
		sanitized.append(first);
		sanitized.append(second);
		sanitized.append(third);
		std::replace_if(
			sanitized.begin(), sanitized.end(),
			[](char character) { return character == '\r' || character == '\n'; }, ' ');
		destination = std::move(sanitized);
	} catch (...) {
		try {
			destination = "provider configuration failed";
		} catch (...) {
			destination.clear();
		}
	}
}

} // namespace windows_ml

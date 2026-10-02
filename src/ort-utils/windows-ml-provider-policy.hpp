// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace windows_ml {

enum class ProviderReadyState {
	Ready,
	NotReady,
	NotPresent,
	Unknown,
};

enum class ProviderActivationAction {
	RegisterReady,
	ActivateInstalledThenRegister,
	UnavailableWithoutActivation,
};

class InstalledProviderActivation {
public:
	virtual ~InstalledProviderActivation() = default;
	virtual void activate() = 0;
	[[nodiscard]] virtual ProviderReadyState read_state() = 0;
};

[[nodiscard]] ProviderReadyState ensure_installed_provider_ready(ProviderReadyState initial,
								 InstalledProviderActivation &activation);

[[nodiscard]] ProviderActivationAction activation_action(ProviderReadyState ready_state) noexcept;

void assign_sanitized_diagnostic(std::string &destination, std::string_view first, std::string_view second = {},
				 std::string_view third = {}) noexcept;

} // namespace windows_ml

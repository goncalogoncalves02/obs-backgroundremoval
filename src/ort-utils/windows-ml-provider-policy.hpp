// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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

[[nodiscard]] ProviderActivationAction activation_action(ProviderReadyState ready_state) noexcept;

} // namespace windows_ml

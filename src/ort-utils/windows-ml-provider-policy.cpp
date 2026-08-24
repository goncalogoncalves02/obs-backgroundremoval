// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-provider-policy.hpp"

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

} // namespace windows_ml

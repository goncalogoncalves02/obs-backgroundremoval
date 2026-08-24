// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-provider-policy.hpp"

#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void expect_equal(windows_ml::ProviderActivationAction actual, windows_ml::ProviderActivationAction expected,
		  std::string_view mutation)
{
	if (actual != expected) {
		std::cerr << "FAIL: " << mutation << '\n';
		++failures;
	}
}

} // namespace

int main()
{
	struct PolicyCase {
		windows_ml::ProviderReadyState ready_state;
		windows_ml::ProviderActivationAction expected_action;
		std::string_view mutation;
	};

	constexpr PolicyCase cases[] = {
		{windows_ml::ProviderReadyState::Ready, windows_ml::ProviderActivationAction::RegisterReady,
		 "Ready incorrectly skips registration or attempts activation"},
		{windows_ml::ProviderReadyState::NotReady,
		 windows_ml::ProviderActivationAction::ActivateInstalledThenRegister,
		 "NotReady incorrectly rejects process-local activation or registers before activation"},
		{windows_ml::ProviderReadyState::NotPresent,
		 windows_ml::ProviderActivationAction::UnavailableWithoutActivation,
		 "NotPresent is security-relevantly mutated to acquire or activate an absent provider"},
		{windows_ml::ProviderReadyState::Unknown,
		 windows_ml::ProviderActivationAction::UnavailableWithoutActivation,
		 "Unknown provider state incorrectly reaches activation or registration"},
	};

	for (const auto &test_case : cases) {
		expect_equal(windows_ml::activation_action(test_case.ready_state), test_case.expected_action,
			     test_case.mutation);
	}

	if (failures != 0) {
		std::cerr << failures << " assertion(s) failed\n";
		return 1;
	}
	return 0;
}

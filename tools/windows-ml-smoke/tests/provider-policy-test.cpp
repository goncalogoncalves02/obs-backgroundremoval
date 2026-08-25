// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-provider-policy.hpp"

#include <iostream>
#include <string>
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

void expect_equal(std::string_view actual, std::string_view expected, std::string_view mutation)
{
	if (actual != expected) {
		std::cerr << "FAIL: " << mutation << "\nexpected: " << expected << "\nactual: " << actual << '\n';
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

	std::string diagnostic;
	static_assert(noexcept(
		windows_ml::assign_sanitized_diagnostic(diagnostic, "provider not found: ", "Bad\r\nProvider")));
	windows_ml::assign_sanitized_diagnostic(diagnostic, "provider not found: ", "Bad\r\nProvider");
	expect_equal(diagnostic, "provider not found: Bad  Provider",
		     "provider lookup diagnostics incorrectly preserve request newlines");

	windows_ml::assign_sanitized_diagnostic(diagnostic, "ONNX Runtime provider failure: ", "first\nsecond");
	expect_equal(diagnostic, "ONNX Runtime provider failure: first second",
		     "exception handlers incorrectly emit multiline diagnostic details");

	if (failures != 0) {
		std::cerr << failures << " assertion(s) failed\n";
		return 1;
	}
	return 0;
}

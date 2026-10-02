// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-provider-policy.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect(bool condition, std::string_view mutation)
{
	if (!condition) {
		std::cerr << "FAIL: " << mutation << '\n';
		++failures;
	}
}

struct FakeInstalledProvider final : windows_ml::InstalledProviderActivation {
	windows_ml::ProviderReadyState after_activation = windows_ml::ProviderReadyState::Ready;
	int activate_calls = 0;
	int read_calls = 0;
	bool throw_on_activate = false;

	void activate() override
	{
		++activate_calls;
		if (throw_on_activate) {
			throw std::runtime_error("activation failed");
		}
	}

	windows_ml::ProviderReadyState read_state() override
	{
		++read_calls;
		return after_activation;
	}
};

void not_present_never_activates()
{
	FakeInstalledProvider provider;
	expect(windows_ml::ensure_installed_provider_ready(windows_ml::ProviderReadyState::NotPresent, provider) ==
		       windows_ml::ProviderReadyState::NotPresent,
	       "NotPresent remains unavailable");
	expect(provider.activate_calls == 0 && provider.read_calls == 0, "NotPresent makes no activation calls");
}

void unknown_never_activates()
{
	FakeInstalledProvider provider;
	expect(windows_ml::ensure_installed_provider_ready(windows_ml::ProviderReadyState::Unknown, provider) ==
		       windows_ml::ProviderReadyState::Unknown,
	       "Unknown remains unavailable");
	expect(provider.activate_calls == 0 && provider.read_calls == 0, "Unknown makes no activation calls");
}

void ready_never_activates()
{
	FakeInstalledProvider provider;
	expect(windows_ml::ensure_installed_provider_ready(windows_ml::ProviderReadyState::Ready, provider) ==
		       windows_ml::ProviderReadyState::Ready,
	       "Ready remains ready");
	expect(provider.activate_calls == 0 && provider.read_calls == 0, "Ready makes no activation calls");
}

void not_ready_requires_ready_after_activation()
{
	FakeInstalledProvider provider;
	expect(windows_ml::ensure_installed_provider_ready(windows_ml::ProviderReadyState::NotReady, provider) ==
		       windows_ml::ProviderReadyState::Ready,
	       "NotReady returns Ready after successful installed activation");
	expect(provider.activate_calls == 1 && provider.read_calls == 1, "NotReady activates and reads exactly once");

	for (const auto state : {windows_ml::ProviderReadyState::NotReady, windows_ml::ProviderReadyState::NotPresent,
				 windows_ml::ProviderReadyState::Unknown}) {
		FakeInstalledProvider still_unavailable;
		still_unavailable.after_activation = state;
		expect(windows_ml::ensure_installed_provider_ready(windows_ml::ProviderReadyState::NotReady,
								   still_unavailable) == state,
		       "non-Ready post-activation state remains unusable");
		expect(still_unavailable.activate_calls == 1 && still_unavailable.read_calls == 1,
		       "non-Ready post-activation is checked once");
	}
}

void activation_exception_propagates()
{
	FakeInstalledProvider provider;
	provider.throw_on_activate = true;
	bool propagated = false;
	try {
		(void)windows_ml::ensure_installed_provider_ready(windows_ml::ProviderReadyState::NotReady, provider);
	} catch (const std::runtime_error &error) {
		propagated = std::string_view(error.what()) == "activation failed";
	}
	expect(propagated, "activation exception propagates to provider boundary");
	expect(provider.activate_calls == 1 && provider.read_calls == 0,
	       "failed activation does not read provider state");
}

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
	not_present_never_activates();
	unknown_never_activates();
	ready_never_activates();
	not_ready_requires_ready_after_activation();
	activation_exception_propagates();

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

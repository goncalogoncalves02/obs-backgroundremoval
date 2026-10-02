// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-session-internal.hpp"
#include "windows-ml-provider-policy.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace windows_ml;
int failures{};
void expect(bool condition, const std::string &message)
{
	if (!condition) {
		std::cerr << message << '\n';
		++failures;
	}
}

class Operations final : public SessionOperations {
public:
	ProviderFailureStage stage{ProviderFailureStage::None};
	bool throw_config{};
	bool failed_without_stage{};
	bool throw_gpu{};
	bool null_gpu{};
	bool throw_cpu{};
	bool null_cpu{};
	int configurations{};
	int gpu_attempts{};
	int cpu_attempts{};
	std::string dispatched;

	ProviderSessionResult configure(Ort::Env &, Ort::SessionOptions &options, std::string_view exact) override
	{
		++configurations;
		dispatched = exact;
		expect(options.GetConfigEntry("session.disable_cpu_ep_fallback") == "1",
		       "GPU must prohibit node CPU fallback");
		options.AddConfigEntry("sprint6.test.gpu_candidate", "1");
		if (throw_config) {
			throw std::runtime_error("injected configuration exception");
		}
		return {.requested_provider_name = std::string(exact),
			.discovered_provider_name = std::string(exact),
			.selected_device = EpDeviceInfo{.ep_name = std::string(exact),
							.hardware_type = "gpu",
							.vendor_id = 0x1002,
							.device_id = 0x7550},
			.succeeded = stage == ProviderFailureStage::None && !failed_without_stage,
			.failure_stage = stage,
			.error_hresult = stage == ProviderFailureStage::None ? std::nullopt
									     : std::optional<std::uint32_t>(0x80004005),
			.error = stage == ProviderFailureStage::None && !failed_without_stage
					 ? ""
					 : "injected provider failure"};
	}

	std::unique_ptr<Ort::Session> construct(Ort::Env &environment, const std::filesystem::path &path,
						const Ort::SessionOptions &options) override
	{
		if (options.HasConfigEntry("session.disable_cpu_ep_fallback")) {
			++gpu_attempts;
			expect(options.GetConfigEntry("session.disable_cpu_ep_fallback") == "1",
			       "GPU constructor must prohibit CPU fallback");
			expect(options.GetConfigEntry("sprint6.test.gpu_candidate") == "1",
			       "GPU candidate marker missing");
			if (throw_gpu) {
				throw std::runtime_error("injected GPU constructor failure");
			}
			if (null_gpu) {
				return nullptr;
			}
			// Simulated GPU success tests control flow only. The object uses the real CPU runtime.
			Ort::SessionOptions cpu_options;
			return std::make_unique<Ort::Session>(environment, path.c_str(), cpu_options);
		}
		++cpu_attempts;
		expect(!options.HasConfigEntry("session.disable_cpu_ep_fallback"),
		       "CPU inherited GPU fallback prohibition");
		expect(!options.HasConfigEntry("sprint6.test.gpu_candidate"),
		       "CPU inherited GPU candidate configuration");
		if (throw_cpu) {
			throw std::runtime_error("injected CPU constructor failure");
		}
		if (null_cpu) {
			return nullptr;
		}
		return std::make_unique<Ort::Session>(environment, path.c_str(), options);
	}
};

void expect_cpu(const SessionCreationResult &result, const std::string &name)
{
	expect(result.session != nullptr, name + ": missing CPU session");
	expect(result.diagnostics.outcome == SessionOutcome::Constructed, name + ": wrong outcome");
	expect(result.diagnostics.effective_provider == "CPUExecutionProvider", name + ": wrong effective provider");
}

void test_dispatch(Ort::Env &env, const std::filesystem::path &model)
{
	auto cpu = create_session(env, model, {"cpu", true, 1});
	expect_cpu(cpu, "real direct CPU");
	expect(cpu.diagnostics.fallback_reason.empty() && !cpu.diagnostics.provider_attempt,
	       "direct CPU must not attempt GPU");
	Operations direct;
	auto injected_cpu = create_session_with_operations(env, model, {"cpu", true, 1}, direct);
	expect_cpu(injected_cpu, "CPU operations");
	expect(direct.configurations == 0 && direct.gpu_attempts == 0 && direct.cpu_attempts == 1,
	       "CPU must skip provider configuration");
	for (const auto &[id, runtime] : std::array<std::pair<const char *, const char *>, 2>{
		     {{"winml-directml", "DmlExecutionProvider"}, {"winml-migraphx", "MIGraphXExecutionProvider"}}}) {
		Operations operations;
		auto result = create_session_with_operations(env, model, {id, true, 1}, operations);
		expect(result.session && result.diagnostics.outcome == SessionOutcome::Constructed,
		       "injected GPU success failed");
		expect(operations.dispatched == runtime && operations.configurations == 1 &&
			       operations.gpu_attempts == 1 && operations.cpu_attempts == 0,
		       "exact GPU dispatch/no cascade failed");
		expect(result.diagnostics.effective_provider == runtime && result.diagnostics.fallback_reason.empty(),
		       "injected GPU diagnostics failed");
		expect(result.diagnostics.provider_attempt && result.diagnostics.provider_attempt->selected_device,
		       "owned device diagnostics missing");
	}
}

void test_failures(Ort::Env &env, const std::filesystem::path &model)
{
	for (const auto &[stage, reason] : std::array<std::pair<ProviderFailureStage, const char *>, 5>{
		     {{ProviderFailureStage::Discovery, "provider_unavailable"},
		      {ProviderFailureStage::Activation, "provider_activation_failed"},
		      {ProviderFailureStage::Registration, "provider_registration_failed"},
		      {ProviderFailureStage::DeviceSelection, "device_unavailable"},
		      {ProviderFailureStage::Attachment, "provider_attachment_failed"}}}) {
		Operations operations;
		operations.stage = stage;
		auto result = create_session_with_operations(env, model, {"winml-migraphx", true, 1}, operations);
		expect_cpu(result, reason);
		expect(result.diagnostics.fallback_reason == reason &&
			       result.diagnostics.error == "injected provider failure",
		       "provider stage mapping/error retention failed");
		expect(result.diagnostics.provider_attempt &&
			       result.diagnostics.provider_attempt->failure_stage == stage &&
			       result.diagnostics.provider_attempt->error_hresult == 0x80004005,
		       "original stage/HRESULT not retained");
		expect(operations.configurations == 1 && operations.gpu_attempts == 0 && operations.cpu_attempts == 1,
		       "provider failure must make one CPU attempt");
	}
	Operations unstaged;
	unstaged.failed_without_stage = true;
	auto unstaged_result = create_session_with_operations(env, model, {"winml-directml", true, 1}, unstaged);
	expect_cpu(unstaged_result, "unstaged provider failure");
	expect(unstaged_result.diagnostics.fallback_reason == "gpu_session_failed" &&
		       unstaged_result.diagnostics.provider_attempt &&
		       unstaged_result.diagnostics.provider_attempt->failure_stage == ProviderFailureStage::None,
	       "an unstaged provider result must not be relabelled as attachment failure");
	expect(unstaged.configurations == 1 && unstaged.gpu_attempts == 0 && unstaged.cpu_attempts == 1,
	       "unstaged provider failure must make one CPU attempt");

	for (int mode = 0; mode < 3; ++mode) {
		Operations operations;
		operations.throw_config = mode == 0;
		operations.throw_gpu = mode == 1;
		operations.null_gpu = mode == 2;
		auto result = create_session_with_operations(env, model, {"winml-directml", true, 1}, operations);
		expect_cpu(result, "gpu_failure_builds_clean_cpu_options");
		expect(result.diagnostics.requested_provider == "winml-directml" && !result.diagnostics.error.empty(),
		       "requested ID/original failure lost");
		expect(result.diagnostics.fallback_reason == "gpu_session_failed",
		       "unknown GPU setup/session failure must not invent an attachment reason");
		if (mode == 0) {
			expect(!result.diagnostics.provider_attempt,
			       "thrown configuration must not invent a provider attempt or attachment stage");
			expect(result.diagnostics.error == "injected configuration exception",
			       "thrown configuration must retain the original exception");
		} else {
			expect(result.diagnostics.provider_attempt && result.diagnostics.provider_attempt->succeeded &&
				       result.diagnostics.provider_attempt->failure_stage == ProviderFailureStage::None,
			       "constructor failure must retain the observed successful provider attempt");
		}
		expect(operations.configurations == 1 && operations.gpu_attempts == (mode == 0 ? 0 : 1) &&
			       operations.cpu_attempts == 1,
		       "fallback attempt count incorrect");
	}
	for (bool gpu : {false, true}) {
		for (bool return_null : {false, true}) {
			Operations operations;
			operations.throw_gpu = gpu;
			operations.throw_cpu = !return_null;
			operations.null_cpu = return_null;
			auto result = create_session_with_operations(
				env, model, {gpu ? "winml-directml" : "cpu", true, 1}, operations);
			expect(!result.session && result.diagnostics.effective_provider.empty() &&
				       result.diagnostics.outcome == SessionOutcome::Failed,
			       "CPU/double failure must clear session/effective claim");
			expect(!result.diagnostics.cpu_error.empty(), "CPU failure missing");
			expect(!gpu || (!result.diagnostics.error.empty() &&
					result.diagnostics.fallback_reason == "gpu_session_failed"),
			       "double failure lost GPU cause");
			expect(operations.cpu_attempts == 1, "CPU failure must not retry");
		}
	}
}

void test_prerequisites(Ort::Env &env, const std::filesystem::path &model)
{
	for (const char *provider : {"winml-directml", "winml-migraphx"}) {
		Operations operations;
		auto result = create_session_with_operations(env, model, {provider, false, 1}, operations);
		expect_cpu(result, "unsupported model");
		expect(result.diagnostics.fallback_reason == "unsupported_model" && operations.configurations == 0 &&
			       operations.gpu_attempts == 0,
		       "unsupported model must bypass GPU");
	}
	for (const auto &request : {SessionRequest{"migraphx", true, 1}, SessionRequest{"cpu", true, -1}}) {
		Operations operations;
		auto result = create_session_with_operations(env, model, request, operations);
		expect(!result.session && result.diagnostics.outcome == SessionOutcome::Failed &&
			       result.diagnostics.effective_provider.empty(),
		       "invalid request must fail");
		expect(operations.configurations == 0 && operations.cpu_attempts == 0,
		       "invalid request reached construction");
	}
	const auto scratch = std::filesystem::temp_directory_path() /
			     ("windows-ml-session-core-test-" +
			      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(scratch);
	const auto empty = scratch / "empty.onnx";
	const auto corrupt = scratch / "corrupt.onnx";
	std::ofstream(empty).close();
	std::ofstream(corrupt) << "invalid ONNX model";
	for (const auto &path : {scratch / "missing.onnx", scratch, empty}) {
		Operations operations;
		auto result = create_session_with_operations(env, path, {"winml-directml", true, 1}, operations);
		expect(!result.session && result.diagnostics.outcome == SessionOutcome::Failed &&
			       result.diagnostics.effective_provider.empty(),
		       "invalid path must fail");
		expect(operations.configurations == 0 && operations.cpu_attempts == 0,
		       "invalid path reached GPU setup");
	}
	auto bad_cpu = create_session(env, corrupt, {"cpu", true, 1});
	expect(!bad_cpu.session && bad_cpu.diagnostics.outcome == SessionOutcome::Failed &&
		       !bad_cpu.diagnostics.cpu_error.empty(),
	       "corrupt real CPU model must fail safely");
	Operations corrupt_gpu;
	auto bad_gpu = create_session_with_operations(env, corrupt, {"winml-directml", true, 1}, corrupt_gpu);
	expect(!bad_gpu.session && bad_gpu.diagnostics.effective_provider.empty() &&
		       bad_gpu.diagnostics.outcome == SessionOutcome::Failed &&
		       bad_gpu.diagnostics.fallback_reason == "gpu_session_failed" &&
		       !bad_gpu.diagnostics.error.empty() && !bad_gpu.diagnostics.cpu_error.empty(),
	       "corrupt model must retain GPU and CPU constructor errors");
	expect(corrupt_gpu.gpu_attempts == 1 && corrupt_gpu.cpu_attempts == 1,
	       "corrupt model constructor attempts incorrect");
	std::filesystem::remove_all(scratch);
}

class UnusableActivation final : public InstalledProviderActivation {
public:
	int activations{};
	void activate() override { ++activations; }
	ProviderReadyState read_state() override { return ProviderReadyState::NotReady; }
};

void test_activation_gate()
{
	UnusableActivation activation;
	int registrations{};
	if (ensure_installed_provider_ready(ProviderReadyState::NotReady, activation) == ProviderReadyState::Ready) {
		++registrations;
	}
	expect(activation.activations == 1 && registrations == 0, "unusable activation must never reach registration");
}
} // namespace

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "usage: windows-ml-session-core-test <model>\n";
		return 2;
	}
	try {
		Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "session-core-test");
		const std::filesystem::path model(argv[1]);
		test_dispatch(env, model);
		test_failures(env, model);
		test_prerequisites(env, model);
		test_activation_gate();
	} catch (const std::exception &exception) {
		std::cerr << "unexpected test exception: " << exception.what() << '\n';
		return 1;
	}
	return failures == 0 ? 0 : 1;
}

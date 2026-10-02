// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ort-utils/ort-session-utils.hpp"
#include "models/ModelMediapipe.hpp"
#include "consts.h"
#include <iostream>
#include <limits>
#include <stdexcept>

extern std::vector<std::string> captured_logs;
namespace {
int failures{};
void expect(bool condition, const std::string &message)
{
	if (!condition) {
		std::cerr << message << '\n';
		++failures;
	}
}
void setup(filter_data &data)
{
	data.env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "plugin-session-test");
	data.model = std::make_unique<ModelMediaPipe>();
	data.modelSelection = MODEL_MEDIAPIPE;
	data.useGPU = USEGPU_CPU;
	data.numThreads = 1;
}
void expect_empty(const filter_data &data, const std::string &name)
{
	expect(!data.session && data.inputNames.empty() && data.outputNames.empty() && data.inputDims.empty() &&
		       data.outputDims.empty() && data.inputTensor.empty() && data.outputTensor.empty() &&
		       data.inputTensorValues.empty() && data.outputTensorValues.empty(),
	       name + ": partial state remains");
	expect(data.sessionDiagnostics.outcome == windows_ml::SessionOutcome::Failed &&
		       data.sessionDiagnostics.effective_provider.empty(),
	       name + ": stale success claim");
}
std::string outcome_log()
{
	std::string outcome;
	int count{};
	for (const auto &log : captured_logs) {
		if (log.find("Windows ML session:") != std::string::npos) {
			outcome = log;
			++count;
		}
	}
	expect(count == 1, "initialization must log exactly one outcome");
	return outcome;
}
class BadMetadata final : public ModelMediaPipe {
public:
	bool populateInputOutputShapes(const std::unique_ptr<Ort::Session> &, std::vector<std::vector<int64_t>> &,
				       std::vector<std::vector<int64_t>> &) override
	{
		return false;
	}
};
class AllocationException final : public ModelMediaPipe {
public:
	void allocateTensorBuffers(const std::vector<std::vector<int64_t>> &in,
				   const std::vector<std::vector<int64_t>> &out,
				   std::vector<std::vector<float>> &out_values,
				   std::vector<std::vector<float>> &in_values, std::vector<Ort::Value> &in_tensor,
				   std::vector<Ort::Value> &out_tensor) override
	{
		ModelMediaPipe::allocateTensorBuffers(in, out, out_values, in_values, in_tensor, out_tensor);
		throw std::runtime_error("injected allocation failure");
	}
};
class EmptyBuffers final : public ModelMediaPipe {
public:
	void allocateTensorBuffers(const std::vector<std::vector<int64_t>> &, const std::vector<std::vector<int64_t>> &,
				   std::vector<std::vector<float>> &, std::vector<std::vector<float>> &,
				   std::vector<Ort::Value> &, std::vector<Ort::Value> &) override
	{
	}
};
void incomplete_buffers_fail(const std::filesystem::path &path)
{
	filter_data data;
	setup(data);
	data.model = std::make_unique<EmptyBuffers>();
	captured_logs.clear();
	expect(createWindowsMlOrtSession(&data, path) == OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT,
	       "incomplete buffers must not publish Ready");
	expect_empty(data, "incomplete buffers");
	expect(outcome_log().find("outcome=Failed") != std::string::npos, "incomplete buffer log");
}
void cpu_initializes_real_mediapipe(const std::filesystem::path &path)
{
	filter_data data;
	setup(data);
	captured_logs.clear();
	expect(createWindowsMlOrtSession(&data, path) == 0, "cpu_initializes_real_mediapipe");
	expect(data.session && data.inputDims == std::vector<std::vector<int64_t>>{{1, 144, 256, 3}} &&
		       data.outputDims == std::vector<std::vector<int64_t>>{{1, 144, 256, 2}} &&
		       data.inputTensor.size() == 1 && data.outputTensor.size() == 1,
	       "real metadata/tensors missing");
	expect(data.sessionDiagnostics.outcome == windows_ml::SessionOutcome::Ready &&
		       data.sessionDiagnostics.effective_provider == "CPUExecutionProvider" &&
		       data.sessionDiagnostics.fallback_reason.empty() && !data.sessionDiagnostics.provider_attempt,
	       "CPU request diagnostics");
	const auto log = outcome_log();
	expect(log.find("outcome=Ready") != std::string::npos && log.find("requested=cpu") != std::string::npos &&
		       log.find("effective=CPUExecutionProvider") != std::string::npos &&
		       log.find("fallback=none") != std::string::npos,
	       "CPU outcome log");
	resetOrtSessionData(data);
	expect(data.env != nullptr && !data.session && data.inputTensorValues.empty(), "reset must preserve env");
}
void bad_metadata_clears_session_and_diagnostics(const std::filesystem::path &path)
{
	filter_data data;
	setup(data);
	data.model = std::make_unique<BadMetadata>();
	captured_logs.clear();
	expect(createWindowsMlOrtSession(&data, path) == OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT,
	       "bad_metadata_clears_session_and_diagnostics return code");
	expect_empty(data, "bad_metadata_clears_session_and_diagnostics");
	const auto log = outcome_log();
	expect(log.find("outcome=Failed") != std::string::npos && log.find("effective=none") != std::string::npos &&
		       log.find("metadata") != std::string::npos,
	       "metadata failure must not log constructed success");
}
void allocation_exception_is_controlled(const std::filesystem::path &path)
{
	filter_data data;
	setup(data);
	data.model = std::make_unique<AllocationException>();
	captured_logs.clear();
	expect(createWindowsMlOrtSession(&data, path) == OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP,
	       "allocation_exception_is_controlled return code");
	expect_empty(data, "allocation_exception_is_controlled");
	expect(data.sessionDiagnostics.fallback_reason.empty(), "allocation failure must not trigger fallback");
	expect(outcome_log().find("injected allocation failure") != std::string::npos, "allocation error log");
}
void unknown_identifier_after_success_clears_old_state(const std::filesystem::path &path)
{
	filter_data data;
	setup(data);
	expect(createWindowsMlOrtSession(&data, path) == 0, "unknown identifier precondition");
	data.useGPU = "migraphx";
	captured_logs.clear();
	expect(createWindowsMlOrtSession(&data, path) != 0, "legacy identifier must fail");
	expect_empty(data, "unknown_identifier_after_success_clears_old_state");
	expect(!data.sessionDiagnostics.provider_attempt, "unknown identifier retained GPU attempt");
	expect(outcome_log().find("outcome=Failed") != std::string::npos, "unknown identifier log");
}
void unsupported_model_cpu_fallback_is_logged(const std::filesystem::path &path)
{
	filter_data data;
	setup(data);
	// Real MediaPipe bytes, deliberately ineligible logical model: tests the adapter eligibility contract.
	data.modelSelection = MODEL_SELFIE;
	data.useGPU = USEGPU_WINML_DIRECTML;
	captured_logs.clear();
	expect(createWindowsMlOrtSession(&data, path) == 0, "unsupported model CPU fallback");
	expect(data.sessionDiagnostics.fallback_reason == "unsupported_model" &&
		       data.sessionDiagnostics.effective_provider == "CPUExecutionProvider" &&
		       !data.sessionDiagnostics.provider_attempt,
	       "unsupported model diagnostics");
	const auto log = outcome_log();
	expect(log.find("fallback=unsupported_model") != std::string::npos &&
		       log.find("effective=CPUExecutionProvider") != std::string::npos &&
		       log.find("model=models/selfie_segmentation.onnx") != std::string::npos,
	       "fallback log");
	data.modelSelection = MODEL_MEDIAPIPE;
	data.useGPU = USEGPU_CPU;
	captured_logs.clear();
	expect(createWindowsMlOrtSession(&data, path) == 0 && data.sessionDiagnostics.fallback_reason.empty() &&
		       !data.sessionDiagnostics.provider_attempt,
	       "reinitialization_clears_previous_fallback");
	expect(outcome_log().find("fallback=none") != std::string::npos, "reinitialization log");
}
void repeated_init_and_teardown(const std::filesystem::path &path)
{
	for (int i = 0; i < 64; ++i) {
		filter_data data;
		setup(data);
		expect(createWindowsMlOrtSession(&data, path) == 0, "repeated_init_and_teardown");
		resetOrtSessionData(data);
		expect(createWindowsMlOrtSession(&data, path) == 0, "same env reinitialization");
	}
}
void logged_provider_context()
{
	windows_ml::SessionDiagnostics diagnostics;
	diagnostics.requested_provider = USEGPU_WINML_DIRECTML;
	diagnostics.requested_runtime_provider = "DmlExecutionProvider";
	diagnostics.effective_provider = "DmlExecutionProvider";
	diagnostics.outcome = windows_ml::SessionOutcome::Ready;
	windows_ml::ProviderSessionResult attempt;
	attempt.requested_provider_name = "DmlExecutionProvider";
	attempt.succeeded = true;
	attempt.selected_device = windows_ml::EpDeviceInfo{"DmlExecutionProvider", "AMD", "gpu", "AMD", 0x1002, 0x7550};
	diagnostics.provider_attempt = attempt;
	captured_logs.clear();
	logWindowsMlSessionOutcome(diagnostics, MODEL_MEDIAPIPE);
	const auto log = outcome_log();
	expect(log.find("activation=false") != std::string::npos &&
		       log.find("registration=false") != std::string::npos &&
		       log.find("vendor=0x00001002") != std::string::npos &&
		       log.find("device=0x00007550") != std::string::npos,
	       "built-in device log must not invent registration");
	diagnostics.outcome = windows_ml::SessionOutcome::Failed;
	diagnostics.effective_provider.clear();
	diagnostics.error = "GPU failed";
	diagnostics.cpu_error = "CPU failed";
	diagnostics.provider_attempt->failure_stage = windows_ml::ProviderFailureStage::Attachment;
	diagnostics.provider_attempt->error = "attachment failed";
	diagnostics.provider_attempt->error_hresult = 0x80004005;
	captured_logs.clear();
	logWindowsMlSessionOutcome(diagnostics, MODEL_MEDIAPIPE);
	const auto failed = outcome_log();
	expect(failed.find("stage=Attachment") != std::string::npos &&
		       failed.find("hresult=0x80004005") != std::string::npos &&
		       failed.find("provider_error=attachment failed") != std::string::npos &&
		       failed.find("cpu_error=CPU failed") != std::string::npos,
	       "separate provider/CPU errors");
}
void invalid_prerequisites(const std::filesystem::path &path)
{
	filter_data data;
	setup(data);
	data.numThreads = std::numeric_limits<uint32_t>::max();
	expect(createWindowsMlOrtSession(&data, path) != 0, "unrepresentable threads accepted");
	expect_empty(data, "invalid threads");
	data.numThreads = 1;
	data.env.reset();
	expect(createWindowsMlOrtSession(&data, path) != 0, "null env accepted");
	expect_empty(data, "null env");
	setup(data);
	expect(createWindowsMlOrtSession(&data, path.parent_path() / "missing.onnx") != 0, "missing path accepted");
	expect_empty(data, "missing path");
}
} // namespace
int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "Expected the tracked MediaPipe model path\n";
		return 1;
	}
	try {
		const auto path = std::filesystem::path(argv[1]);
		cpu_initializes_real_mediapipe(path);
		bad_metadata_clears_session_and_diagnostics(path);
		allocation_exception_is_controlled(path);
		incomplete_buffers_fail(path);
		unknown_identifier_after_success_clears_old_state(path);
		unsupported_model_cpu_fallback_is_logged(path);
		repeated_init_and_teardown(path);
		logged_provider_context();
		invalid_prerequisites(path);
	} catch (const std::exception &error) {
		std::cerr << "Unexpected exception: " << error.what() << '\n';
		return 1;
	}
	return failures == 0 ? 0 : 1;
}

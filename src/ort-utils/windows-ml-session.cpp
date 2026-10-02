// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows-ml-session-internal.hpp"
#include "windows-ml-provider-policy.hpp"

#include <exception>
#include <system_error>
#include <utility>

namespace windows_ml {
namespace {

class ProductionOperations final : public SessionOperations {
public:
	ProviderSessionResult configure(Ort::Env &environment, Ort::SessionOptions &options,
					std::string_view exact_name) override
	{
		return configure_provider_session(environment, options, exact_name);
	}

	std::unique_ptr<Ort::Session> construct(Ort::Env &environment, const std::filesystem::path &model,
						const Ort::SessionOptions &options) override
	{
		return std::make_unique<Ort::Session>(environment, model.c_str(), options);
	}
};

[[nodiscard]] const char *fallback_reason(ProviderFailureStage stage) noexcept
{
	switch (stage) {
	case ProviderFailureStage::Discovery:
		return "provider_unavailable";
	case ProviderFailureStage::Activation:
		return "provider_activation_failed";
	case ProviderFailureStage::Registration:
		return "provider_registration_failed";
	case ProviderFailureStage::DeviceSelection:
		return "device_unavailable";
	case ProviderFailureStage::Attachment:
		return "provider_attachment_failed";
	case ProviderFailureStage::None:
		return "gpu_session_failed";
	}
	return "gpu_session_failed";
}

// Called only inside an exception handler; diagnostics must not throw again.
void capture_exception(std::string &error) noexcept
{
	try {
		throw;
	} catch (const std::exception &exception) {
		assign_sanitized_diagnostic(error, exception.what());
	} catch (...) {
		assign_sanitized_diagnostic(error, "unknown session initialization failure");
	}
}

[[nodiscard]] bool valid_model_file(const std::filesystem::path &model)
{
	std::error_code error;
	if (!std::filesystem::is_regular_file(model, error) || error) {
		return false;
	}
	const auto size = std::filesystem::file_size(model, error);
	return !error && size > 0;
}

} // namespace

SessionCreationResult create_session_with_operations(Ort::Env &environment, const std::filesystem::path &model,
						     const SessionRequest &request, SessionOperations &operations)
{
	SessionCreationResult result;
	auto &diagnostics = result.diagnostics;
	diagnostics.outcome = SessionOutcome::Failed;
	try {
		diagnostics.requested_provider = request.requested_provider;
		const auto resolved = resolve_session_request(request);
		diagnostics.requested_runtime_provider = resolved.requested_runtime_provider;
		diagnostics.fallback_reason = resolved.fallback_reason;
		if (resolved.route == SessionRoute::Reject) {
			diagnostics.error = resolved.error;
			return result;
		}
		if (!valid_model_file(model)) {
			diagnostics.error = "model must be a regular nonempty file";
			return result;
		}

		if (resolved.route == SessionRoute::Gpu) {
			// This scope destroys the entire GPU candidate/options before CPU construction.
			Ort::SessionOptions options{nullptr};
			bool options_ready = false;
			try {
				options = Ort::SessionOptions{};
				options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
				options.DisableMemPattern();
				options.SetExecutionMode(ORT_SEQUENTIAL);
				options.AddConfigEntry("session.disable_cpu_ep_fallback", "1");
				options_ready = true;
			} catch (...) {
				capture_exception(diagnostics.error);
				diagnostics.fallback_reason = "gpu_session_failed";
			}

			if (options_ready) {
				try {
					diagnostics.provider_attempt =
						operations.configure(environment, options, resolved.runtime_provider);
				} catch (...) {
					// No provider result means no failing provider stage was observed.
					capture_exception(diagnostics.error);
					diagnostics.fallback_reason = "gpu_session_failed";
				}
			}

			if (diagnostics.provider_attempt) {
				if (!diagnostics.provider_attempt->succeeded) {
					diagnostics.fallback_reason =
						fallback_reason(diagnostics.provider_attempt->failure_stage);
					diagnostics.error = diagnostics.provider_attempt->error;
				} else {
					try {
						result.session = operations.construct(environment, model, options);
						if (result.session) {
							diagnostics.effective_provider = resolved.runtime_provider;
							diagnostics.outcome = SessionOutcome::Constructed;
							return result;
						}
						diagnostics.fallback_reason = "gpu_session_failed";
						diagnostics.error = "GPU constructor returned a null session";
					} catch (...) {
						capture_exception(diagnostics.error);
						diagnostics.fallback_reason = "gpu_session_failed";
					}
				}
			}
			result.session.reset();
			diagnostics.effective_provider.clear();
		}

		try {
			Ort::SessionOptions cpu_options;
			cpu_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
			cpu_options.SetInterOpNumThreads(request.cpu_threads);
			cpu_options.SetIntraOpNumThreads(request.cpu_threads);
			result.session = operations.construct(environment, model, cpu_options);
			if (!result.session) {
				diagnostics.cpu_error = "CPU constructor returned a null session";
				return result;
			}
			diagnostics.effective_provider = "CPUExecutionProvider";
			diagnostics.outcome = SessionOutcome::Constructed;
		} catch (...) {
			capture_exception(diagnostics.cpu_error);
			result.session.reset();
			diagnostics.effective_provider.clear();
		}
	} catch (...) {
		capture_exception(diagnostics.error);
		result.session.reset();
		diagnostics.effective_provider.clear();
		diagnostics.outcome = SessionOutcome::Failed;
	}
	return result;
}

SessionCreationResult create_session(Ort::Env &environment, const std::filesystem::path &model,
				     const SessionRequest &request)
{
	ProductionOperations operations;
	return create_session_with_operations(environment, model, request, operations);
}

} // namespace windows_ml

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <WinMLEpCatalog.h>
#include <winml/onnxruntime_cxx_api.h>

#include "windows-ml-provider.hpp"
#include "windows-ml-provider-policy.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace windows_ml {
namespace {

constexpr std::uint32_t kAmdVendorId = 0x1002;

class Catalog {
public:
	explicit Catalog(WinMLEpCatalogHandle handle) noexcept : handle_(handle) {}
	~Catalog() { WinMLEpCatalogRelease(handle_); }

	Catalog(const Catalog &) = delete;
	Catalog &operator=(const Catalog &) = delete;

	[[nodiscard]] WinMLEpCatalogHandle get() const noexcept { return handle_; }

private:
	WinMLEpCatalogHandle handle_{};
};

class CatalogHresultFailure {
public:
	CatalogHresultFailure(HRESULT hresult, std::string_view first, std::string_view second = {},
			      std::string_view third = {}) noexcept
		: hresult_(hresult),
		  first_(first),
		  second_(second),
		  third_(third)
	{
	}

	[[nodiscard]] HRESULT hresult() const noexcept { return hresult_; }
	[[nodiscard]] std::string_view first() const noexcept { return first_; }
	[[nodiscard]] std::string_view second() const noexcept { return second_; }
	[[nodiscard]] std::string_view third() const noexcept { return third_; }

private:
	HRESULT hresult_;
	std::string_view first_;
	std::string_view second_;
	std::string_view third_;
};

[[nodiscard]] std::string copy_optional_string(const char *value)
{
	return value == nullptr ? std::string{} : std::string(value);
}

[[nodiscard]] std::string unknown_enum(int value)
{
	return "unknown(" + std::to_string(value) + ')';
}

[[nodiscard]] std::string ready_state_name(WinMLEpReadyState state)
{
	switch (state) {
	case WinMLEpReadyState_Ready:
		return "ready";
	case WinMLEpReadyState_NotReady:
		return "not_ready";
	case WinMLEpReadyState_NotPresent:
		return "not_present";
	default:
		return unknown_enum(static_cast<int>(state));
	}
}

[[nodiscard]] ProviderReadyState provider_ready_state(WinMLEpReadyState state) noexcept
{
	switch (state) {
	case WinMLEpReadyState_Ready:
		return ProviderReadyState::Ready;
	case WinMLEpReadyState_NotReady:
		return ProviderReadyState::NotReady;
	case WinMLEpReadyState_NotPresent:
		return ProviderReadyState::NotPresent;
	default:
		return ProviderReadyState::Unknown;
	}
}

[[nodiscard]] std::string certification_name(WinMLEpCertification certification)
{
	switch (certification) {
	case WinMLEpCertification_Unknown:
		return "unknown";
	case WinMLEpCertification_Certified:
		return "certified";
	case WinMLEpCertification_Uncertified:
		return "uncertified";
	default:
		return unknown_enum(static_cast<int>(certification));
	}
}

[[nodiscard]] std::string hardware_type_name(OrtHardwareDeviceType type)
{
	switch (type) {
	case OrtHardwareDeviceType_CPU:
		return "cpu";
	case OrtHardwareDeviceType_GPU:
		return "gpu";
	case OrtHardwareDeviceType_NPU:
		return "npu";
	default:
		return unknown_enum(static_cast<int>(type));
	}
}

[[nodiscard]] std::string system_hresult_message(HRESULT result)
{
	char *buffer = nullptr;
	const auto length = FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr,
		static_cast<DWORD>(result), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<char *>(&buffer), 0, nullptr);
	if (length == 0 || buffer == nullptr) {
		return {};
	}

	std::string message(buffer, length);
	LocalFree(buffer);
	while (!message.empty() &&
	       (message.back() == '\r' || message.back() == '\n' || message.back() == ' ' || message.back() == '\t')) {
		message.pop_back();
	}
	return message;
}

template<typename Result>
void set_hresult_error(Result &result, HRESULT hresult, std::string_view first, std::string_view second = {},
		       std::string_view third = {}) noexcept
{
	result.error_hresult = static_cast<std::uint32_t>(hresult);
	assign_sanitized_diagnostic(result.error, first, second, third);
	try {
		const auto system_message = system_hresult_message(hresult);
		if (!system_message.empty()) {
			assign_sanitized_diagnostic(result.error, result.error, ": ", system_message);
		}
	} catch (...) {
		// Keep the best diagnostic that the no-throw assignment stored above.
	}
}

[[nodiscard]] ProviderInfo copy_provider_info(const WinMLEpInfo &info)
{
	return {
		.name = copy_optional_string(info.name),
		.version = copy_optional_string(info.version),
		.package_family_name = copy_optional_string(info.packageFamilyName),
		.library_path = copy_optional_string(info.libraryPath),
		.package_root_path = copy_optional_string(info.packageRootPath),
		.ready_state = ready_state_name(info.readyState),
		.certification = certification_name(info.certification),
	};
}

struct EnumerationContext {
	std::vector<ProviderInfo> *providers{};
	std::exception_ptr exception;
};

BOOL CALLBACK copy_provider_callback(WinMLEpHandle, const WinMLEpInfo *info, void *opaque_context)
{
	auto &context = *static_cast<EnumerationContext *>(opaque_context);
	if (info == nullptr) {
		return TRUE;
	}

	try {
		context.providers->push_back(copy_provider_info(*info));
		return TRUE;
	} catch (...) {
		context.exception = std::current_exception();
		return FALSE;
	}
}

template<typename SizeFunction, typename CopyFunction>
[[nodiscard]] std::string copy_provider_string(WinMLEpHandle provider, SizeFunction size_function,
					       CopyFunction copy_function, std::string_view field_name)
{
	size_t size = 0;
	HRESULT result = size_function(provider, &size);
	if (FAILED(result)) {
		throw CatalogHresultFailure(result, "failed to read provider ", field_name, " size");
	}
	if (size == 0) {
		return {};
	}

	std::string value(size, '\0');
	size_t used = 0;
	result = copy_function(provider, value.size(), value.data(), &used);
	if (FAILED(result)) {
		throw CatalogHresultFailure(result, "failed to read provider ", field_name);
	}
	if (used > value.size()) {
		throw std::runtime_error(std::string("provider ") + std::string(field_name) + " length is invalid");
	}

	const auto terminator = value.find('\0');
	if (terminator != std::string::npos) {
		value.resize(terminator);
	} else if (used < value.size()) {
		value.resize(used);
	}
	return value;
}

[[nodiscard]] std::string provider_name(WinMLEpHandle provider)
{
	return copy_provider_string(provider, WinMLEpGetNameSize, WinMLEpGetName, "name");
}

[[nodiscard]] std::string provider_library_path(WinMLEpHandle provider)
{
	return copy_provider_string(provider, WinMLEpGetLibraryPathSize, WinMLEpGetLibraryPath, "library path");
}

[[nodiscard]] EpDeviceInfo copy_device(const Ort::ConstEpDevice &device)
{
	const auto hardware = device.Device();
	if (static_cast<const OrtHardwareDevice *>(hardware) == nullptr) {
		throw std::runtime_error("ONNX Runtime returned an EP device without a hardware device");
	}

	return {
		.ep_name = copy_optional_string(device.EpName()),
		.ep_vendor = copy_optional_string(device.EpVendor()),
		.hardware_type = hardware_type_name(hardware.Type()),
		.hardware_vendor = copy_optional_string(hardware.Vendor()),
		.vendor_id = hardware.VendorId(),
		.device_id = hardware.DeviceId(),
	};
}

struct DeviceCandidate {
	Ort::ConstEpDevice device;
	EpDeviceInfo info;
};

[[nodiscard]] bool attach_existing_amd_gpu_device(Ort::Env &environment, Ort::SessionOptions &session_options,
						  std::string_view exact_provider_name, ProviderSessionResult &result)
{
	std::vector<DeviceCandidate> candidates;
	for (const auto &device : environment.GetEpDevices()) {
		auto info = copy_device(device);
		if (info.ep_name == exact_provider_name && info.hardware_type == "gpu" &&
		    info.vendor_id == kAmdVendorId) {
			candidates.push_back({device, std::move(info)});
		}
	}
	if (candidates.empty()) {
		return false;
	}

	const auto selected =
		std::min_element(candidates.begin(), candidates.end(), [](const auto &left, const auto &right) {
			return std::tie(left.info.vendor_id, left.info.device_id) <
			       std::tie(right.info.vendor_id, right.info.device_id);
		});
	std::vector<Ort::ConstEpDevice> selected_devices{selected->device};
	Ort::KeyValuePairs empty_options;
	session_options.AppendExecutionProvider_V2(environment, selected_devices, empty_options);

	result.discovered_provider_name = selected->info.ep_name;
	result.selected_device = selected->info;
	result.succeeded = true;
	return true;
}

} // namespace

ProviderDiscoveryResult discover_providers() noexcept
{
	ProviderDiscoveryResult result;
	try {
		WinMLEpCatalogHandle raw_catalog = nullptr;
		const HRESULT create_result = WinMLEpCatalogCreate(&raw_catalog);
		if (FAILED(create_result)) {
			set_hresult_error(result, create_result, "failed to create the Windows ML provider catalog");
			return result;
		}
		Catalog catalog(raw_catalog);

		EnumerationContext context;
		context.providers = &result.providers;
		const HRESULT enumerate_result =
			WinMLEpCatalogEnumProviders(catalog.get(), copy_provider_callback, &context);
		if (context.exception) {
			std::rethrow_exception(context.exception);
		}
		if (FAILED(enumerate_result)) {
			set_hresult_error(result, enumerate_result, "failed to enumerate Windows ML providers");
			return result;
		}

		result.succeeded = true;
	} catch (const std::exception &exception) {
		result.error = exception.what();
	} catch (...) {
		result.error = "unknown failure while discovering Windows ML providers";
	}
	return result;
}

ProviderPreparationResult prepare_provider(Ort::Env &environment, std::string_view exact_provider_name) noexcept
{
	ProviderPreparationResult result;
	result.requested_provider_name = exact_provider_name;
	try {
		WinMLEpCatalogHandle raw_catalog = nullptr;
		const HRESULT create_result = WinMLEpCatalogCreate(&raw_catalog);
		if (FAILED(create_result)) {
			set_hresult_error(result, create_result, "failed to create the Windows ML provider catalog");
			return result;
		}
		Catalog catalog(raw_catalog);

		WinMLEpHandle provider = nullptr;
		const HRESULT find_result = WinMLEpCatalogFindProvider(
			catalog.get(), result.requested_provider_name.c_str(), nullptr, &provider);
		if (FAILED(find_result) || provider == nullptr) {
			result.error = "provider not found: " + result.requested_provider_name;
			if (FAILED(find_result)) {
				result.error_hresult = static_cast<std::uint32_t>(find_result);
				const auto system_message = system_hresult_message(find_result);
				if (!system_message.empty()) {
					result.error += ": " + system_message;
				}
			}
			return result;
		}
		result.provider_found = true;

		result.discovered_provider_name = provider_name(provider);
		if (result.discovered_provider_name != result.requested_provider_name) {
			result.error = "catalog returned a provider name that does not exactly match the request";
			return result;
		}

		WinMLEpReadyState ready_state{};
		HRESULT state_result = WinMLEpGetReadyState(provider, &ready_state);
		if (FAILED(state_result)) {
			set_hresult_error(result, state_result, "failed to read the provider ready state");
			return result;
		}
		result.ready_state_before = ready_state_name(ready_state);

		std::optional<HRESULT> ensure_failure;
		if (ready_state != WinMLEpReadyState_Ready) {
			const HRESULT ensure_result = WinMLEpEnsureReady(provider);
			if (FAILED(ensure_result)) {
				ensure_failure = ensure_result;
			}
		}

		state_result = WinMLEpGetReadyState(provider, &ready_state);
		if (FAILED(state_result)) {
			set_hresult_error(result, state_result, "failed to re-read the provider ready state");
			return result;
		}
		result.ready_state_after = ready_state_name(ready_state);
		if (ensure_failure.has_value()) {
			set_hresult_error(result, *ensure_failure, "failed to prepare the requested provider");
			return result;
		}
		if (ready_state != WinMLEpReadyState_Ready) {
			result.error = "provider is not ready after preparation";
			return result;
		}

		const auto library_path = provider_library_path(provider);
		if (library_path.empty()) {
			result.error = "ready provider has an empty library path";
			return result;
		}

		environment.RegisterExecutionProviderLibrary(result.discovered_provider_name.c_str(),
							     std::filesystem::path(library_path).wstring());
		result.registration_succeeded = true;

		for (const auto &device : environment.GetEpDevices()) {
			result.devices.push_back(copy_device(device));
			const auto &copied = result.devices.back();
			if (copied.ep_name == result.discovered_provider_name) {
				++result.matching_device_count;
				if (copied.hardware_type == "gpu" && copied.vendor_id == kAmdVendorId) {
					result.matching_amd_gpu = true;
				}
			}
		}

		if (result.matching_device_count == 0) {
			result.error = "registered provider has no exact-name ONNX Runtime EP device";
			return result;
		}

		result.succeeded = true;
	} catch (const CatalogHresultFailure &failure) {
		set_hresult_error(result, failure.hresult(), failure.first(), failure.second(), failure.third());
	} catch (const Ort::Exception &exception) {
		result.error = std::string("ONNX Runtime provider failure: ") + exception.what();
	} catch (const std::exception &exception) {
		result.error = exception.what();
	} catch (...) {
		result.error = "unknown failure while preparing the Windows ML provider";
	}
	return result;
}

ProviderSessionResult configure_provider_session(Ort::Env &environment, Ort::SessionOptions &session_options,
						 std::string_view exact_provider_name) noexcept
{
	ProviderSessionResult result;
	try {
		result.requested_provider_name = exact_provider_name;
		if (attach_existing_amd_gpu_device(environment, session_options, exact_provider_name, result)) {
			return result;
		}

		WinMLEpCatalogHandle raw_catalog = nullptr;
		const HRESULT create_result = WinMLEpCatalogCreate(&raw_catalog);
		if (FAILED(create_result)) {
			set_hresult_error(result, create_result, "failed to create the Windows ML provider catalog");
			return result;
		}
		Catalog catalog(raw_catalog);

		WinMLEpHandle provider = nullptr;
		const HRESULT find_result = WinMLEpCatalogFindProvider(
			catalog.get(), result.requested_provider_name.c_str(), nullptr, &provider);
		if (FAILED(find_result)) {
			set_hresult_error(result, find_result, "provider not found: ", result.requested_provider_name);
			return result;
		}
		if (provider == nullptr) {
			assign_sanitized_diagnostic(result.error,
						    "provider not found: ", result.requested_provider_name);
			return result;
		}

		result.discovered_provider_name = provider_name(provider);
		if (result.discovered_provider_name != result.requested_provider_name) {
			result.error = "catalog returned a provider name that does not exactly match the request";
			return result;
		}

		WinMLEpReadyState ready_state{};
		HRESULT state_result = WinMLEpGetReadyState(provider, &ready_state);
		if (FAILED(state_result)) {
			set_hresult_error(result, state_result, "failed to read the provider ready state");
			return result;
		}
		result.ready_state_before = ready_state_name(ready_state);
		result.ready_state_after = result.ready_state_before;

		switch (activation_action(provider_ready_state(ready_state))) {
		case ProviderActivationAction::UnavailableWithoutActivation:
			result.error = "provider is unavailable without activation: " + result.ready_state_before;
			return result;
		case ProviderActivationAction::ActivateInstalledThenRegister: {
			result.process_activation_attempted = true;
			const HRESULT ensure_result = WinMLEpEnsureReady(provider);
			if (FAILED(ensure_result)) {
				set_hresult_error(result, ensure_result,
						  "failed to activate the installed provider in this process");
				return result;
			}

			state_result = WinMLEpGetReadyState(provider, &ready_state);
			if (FAILED(state_result)) {
				set_hresult_error(result, state_result, "failed to re-read the provider ready state");
				return result;
			}
			result.ready_state_after = ready_state_name(ready_state);
			if (provider_ready_state(ready_state) != ProviderReadyState::Ready) {
				result.error = "provider is not ready after process-local activation";
				return result;
			}
			break;
		}
		case ProviderActivationAction::RegisterReady:
			break;
		}

		const auto library_path = provider_library_path(provider);
		if (library_path.empty()) {
			result.error = "ready provider has an empty library path";
			return result;
		}

		environment.RegisterExecutionProviderLibrary(result.discovered_provider_name.c_str(),
							     std::filesystem::path(library_path).wstring());
		result.provider_registration_succeeded = true;

		if (!attach_existing_amd_gpu_device(environment, session_options, result.discovered_provider_name,
						    result)) {
			result.error = "registered provider has no exact-name AMD GPU ONNX Runtime EP device";
			return result;
		}
	} catch (const CatalogHresultFailure &failure) {
		set_hresult_error(result, failure.hresult(), failure.first(), failure.second(), failure.third());
	} catch (HRESULT hresult) {
		set_hresult_error(result, hresult, "Windows ML provider configuration failed");
	} catch (const Ort::Exception &exception) {
		assign_sanitized_diagnostic(result.error, "ONNX Runtime provider failure: ", exception.what());
	} catch (const std::exception &exception) {
		assign_sanitized_diagnostic(result.error, exception.what());
	} catch (...) {
		assign_sanitized_diagnostic(result.error, "unknown failure while configuring the Windows ML provider");
	}
	return result;
}

} // namespace windows_ml

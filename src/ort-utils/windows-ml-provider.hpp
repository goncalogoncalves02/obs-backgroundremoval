// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Ort {
struct Env;
}

namespace windows_ml {

struct ProviderInfo {
	std::string name;
	std::string version;
	std::string package_family_name;
	std::string library_path;
	std::string package_root_path;
	std::string ready_state;
	std::string certification;
};

struct EpDeviceInfo {
	std::string ep_name;
	std::string ep_vendor;
	std::string hardware_type;
	std::string hardware_vendor;
	std::uint32_t vendor_id{};
	std::uint32_t device_id{};
};

struct ProviderDiscoveryResult {
	bool succeeded{};
	std::vector<ProviderInfo> providers;
	std::optional<std::uint32_t> error_hresult;
	std::string error;
};

struct ProviderPreparationResult {
	std::string requested_provider_name;
	std::string discovered_provider_name;
	bool provider_found{};
	std::string ready_state_before;
	std::string ready_state_after;
	bool registration_succeeded{};
	std::vector<EpDeviceInfo> devices;
	std::size_t matching_device_count{};
	bool matching_amd_gpu{};
	bool succeeded{};
	std::optional<std::uint32_t> error_hresult;
	std::string error;
};

[[nodiscard]] ProviderDiscoveryResult discover_providers() noexcept;

[[nodiscard]] ProviderPreparationResult prepare_provider(Ort::Env &environment,
							 std::string_view exact_provider_name) noexcept;

} // namespace windows_ml

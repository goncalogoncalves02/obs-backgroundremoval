// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "provider-report.hpp"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <vector>

namespace windows_ml_smoke {
namespace {

[[nodiscard]] std::string boolean_value(bool value)
{
	return value ? "true" : "false";
}

void append_hresult(std::ostringstream &report, const std::optional<std::uint32_t> &hresult)
{
	if (hresult.has_value()) {
		report << "error_hresult=" << format_hresult(*hresult) << '\n';
	}
}

} // namespace

std::string sanitize_single_line(std::string_view value)
{
	std::string sanitized(value);
	std::replace_if(
		sanitized.begin(), sanitized.end(),
		[](char character) { return character == '\r' || character == '\n'; }, ' ');
	return sanitized;
}

std::string format_hresult(std::uint32_t value)
{
	std::ostringstream formatted;
	formatted << "0x" << std::hex << std::nouppercase << value;
	return formatted.str();
}

std::string format_provider_discovery_report(const windows_ml::ProviderDiscoveryResult &result)
{
	auto providers = result.providers;
	std::sort(providers.begin(), providers.end(), [](const auto &left, const auto &right) {
		return std::tie(left.name, left.package_family_name) < std::tie(right.name, right.package_family_name);
	});

	std::ostringstream report;
	report << "operation=list-providers\n";
	report << "provider_count=" << providers.size() << '\n';
	for (std::size_t index = 0; index < providers.size(); ++index) {
		const auto &provider = providers[index];
		const auto prefix = "provider." + std::to_string(index) + '.';
		report << prefix << "name=" << sanitize_single_line(provider.name) << '\n';
		report << prefix << "version=" << sanitize_single_line(provider.version) << '\n';
		report << prefix << "ready_state=" << sanitize_single_line(provider.ready_state) << '\n';
		report << prefix << "certification=" << sanitize_single_line(provider.certification) << '\n';
		report << prefix << "package_family_name=" << sanitize_single_line(provider.package_family_name)
		       << '\n';
		report << prefix << "library_path=" << sanitize_single_line(provider.library_path) << '\n';
		report << prefix << "package_root_path=" << sanitize_single_line(provider.package_root_path) << '\n';
	}
	append_hresult(report, result.error_hresult);
	report << "status=" << (result.succeeded ? "ok" : "unavailable") << '\n';
	return report.str();
}

std::string format_provider_preparation_report(const windows_ml::ProviderPreparationResult &result)
{
	auto devices = result.devices;
	std::sort(devices.begin(), devices.end(), [](const auto &left, const auto &right) {
		return std::tie(left.ep_name, left.hardware_type, left.vendor_id, left.device_id) <
		       std::tie(right.ep_name, right.hardware_type, right.vendor_id, right.device_id);
	});

	std::ostringstream report;
	report << "operation=prepare-provider\n";
	report << "requested_provider_name=" << sanitize_single_line(result.requested_provider_name) << '\n';
	report << "provider_found=" << boolean_value(result.provider_found) << '\n';
	report << "discovered_provider_name=" << sanitize_single_line(result.discovered_provider_name) << '\n';
	report << "ready_state_before=" << sanitize_single_line(result.ready_state_before) << '\n';
	report << "ready_state_after=" << sanitize_single_line(result.ready_state_after) << '\n';
	report << "registration_succeeded=" << boolean_value(result.registration_succeeded) << '\n';
	report << "device_count=" << devices.size() << '\n';
	for (std::size_t index = 0; index < devices.size(); ++index) {
		const auto &device = devices[index];
		const auto prefix = "device." + std::to_string(index) + '.';
		report << prefix << "ep_name=" << sanitize_single_line(device.ep_name) << '\n';
		report << prefix << "ep_vendor=" << sanitize_single_line(device.ep_vendor) << '\n';
		report << prefix << "hardware_type=" << sanitize_single_line(device.hardware_type) << '\n';
		report << prefix << "hardware_vendor=" << sanitize_single_line(device.hardware_vendor) << '\n';
		report << prefix << "vendor_id=" << format_hresult(device.vendor_id) << '\n';
		report << prefix << "device_id=" << format_hresult(device.device_id) << '\n';
	}
	report << "matching_device_count=" << result.matching_device_count << '\n';
	report << "matching_amd_gpu=" << boolean_value(result.matching_amd_gpu) << '\n';
	append_hresult(report, result.error_hresult);
	report << "status=" << (result.succeeded ? "ok" : "unavailable") << '\n';
	return report.str();
}

} // namespace windows_ml_smoke

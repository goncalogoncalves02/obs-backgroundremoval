// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "provider-report.hpp"

#include <iostream>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void expect_equal(std::string_view actual, std::string_view expected, std::string_view message)
{
	if (actual != expected) {
		std::cerr << "FAIL: " << message << "\nexpected:\n" << expected << "\nactual:\n" << actual << '\n';
		++failures;
	}
}

} // namespace

int main()
{
	windows_ml::ProviderDiscoveryResult discovery;
	discovery.succeeded = true;
	discovery.providers = {
		{
			.name = "ZuluProvider",
			.version = "2",
			.package_family_name = "family.z",
			.library_path = "Z:/provider.dll",
			.package_root_path = "Z:/",
			.ready_state = "not_present",
			.certification = "unknown(17)",
		},
		{
			.name = "AlphaProvider",
			.version = "1",
			.package_family_name = "family.b",
			.library_path = {},
			.package_root_path = {},
			.ready_state = "ready",
			.certification = "certified",
		},
		{
			.name = "AlphaProvider",
			.version = "0",
			.package_family_name = "family.a",
			.library_path = "A:/provider.dll",
			.package_root_path = "A:/",
			.ready_state = "not_ready",
			.certification = "uncertified",
		},
	};

	expect_equal(windows_ml_smoke::format_provider_discovery_report(discovery),
		     "operation=list-providers\n"
		     "provider_count=3\n"
		     "provider.0.name=AlphaProvider\n"
		     "provider.0.version=0\n"
		     "provider.0.ready_state=not_ready\n"
		     "provider.0.certification=uncertified\n"
		     "provider.0.package_family_name=family.a\n"
		     "provider.0.library_path=A:/provider.dll\n"
		     "provider.0.package_root_path=A:/\n"
		     "provider.1.name=AlphaProvider\n"
		     "provider.1.version=1\n"
		     "provider.1.ready_state=ready\n"
		     "provider.1.certification=certified\n"
		     "provider.1.package_family_name=family.b\n"
		     "provider.1.library_path=\n"
		     "provider.1.package_root_path=\n"
		     "provider.2.name=ZuluProvider\n"
		     "provider.2.version=2\n"
		     "provider.2.ready_state=not_present\n"
		     "provider.2.certification=unknown(17)\n"
		     "provider.2.package_family_name=family.z\n"
		     "provider.2.library_path=Z:/provider.dll\n"
		     "provider.2.package_root_path=Z:/\n"
		     "status=ok\n",
		     "sorts provider records and keeps status last");

	windows_ml::ProviderPreparationResult preparation;
	preparation.requested_provider_name = "MIGraphXExecutionProvider";
	preparation.discovered_provider_name = "MIGraphXExecutionProvider";
	preparation.provider_found = true;
	preparation.ready_state_before = "not_present";
	preparation.ready_state_after = "ready";
	preparation.registration_succeeded = true;
	preparation.devices = {
		{
			.ep_name = "MIGraphXExecutionProvider",
			.ep_vendor = "AMD",
			.hardware_type = "gpu",
			.hardware_vendor = "AMD",
			.vendor_id = 0x1002,
			.device_id = 0x744c,
		},
		{
			.ep_name = "CPUExecutionProvider",
			.ep_vendor = "Microsoft",
			.hardware_type = "cpu",
			.hardware_vendor = "",
			.vendor_id = 0,
			.device_id = 0,
		},
	};
	preparation.matching_device_count = 1;
	preparation.matching_amd_gpu = true;
	preparation.succeeded = true;

	expect_equal(windows_ml_smoke::format_provider_preparation_report(preparation),
		     "operation=prepare-provider\n"
		     "requested_provider_name=MIGraphXExecutionProvider\n"
		     "provider_found=true\n"
		     "discovered_provider_name=MIGraphXExecutionProvider\n"
		     "ready_state_before=not_present\n"
		     "ready_state_after=ready\n"
		     "registration_succeeded=true\n"
		     "device_count=2\n"
		     "device.0.ep_name=CPUExecutionProvider\n"
		     "device.0.ep_vendor=Microsoft\n"
		     "device.0.hardware_type=cpu\n"
		     "device.0.hardware_vendor=\n"
		     "device.0.vendor_id=0x0\n"
		     "device.0.device_id=0x0\n"
		     "device.1.ep_name=MIGraphXExecutionProvider\n"
		     "device.1.ep_vendor=AMD\n"
		     "device.1.hardware_type=gpu\n"
		     "device.1.hardware_vendor=AMD\n"
		     "device.1.vendor_id=0x1002\n"
		     "device.1.device_id=0x744c\n"
		     "matching_device_count=1\n"
		     "matching_amd_gpu=true\n"
		     "status=ok\n",
		     "sorts device records and keeps status last");

	windows_ml::ProviderPreparationResult missing;
	missing.requested_provider_name = "__missing__";
	missing.provider_found = false;
	missing.error_hresult = 0x80070490;
	missing.error = "provider not found";
	expect_equal(windows_ml_smoke::format_provider_preparation_report(missing),
		     "operation=prepare-provider\n"
		     "requested_provider_name=__missing__\n"
		     "provider_found=false\n"
		     "discovered_provider_name=\n"
		     "ready_state_before=\n"
		     "ready_state_after=\n"
		     "registration_succeeded=false\n"
		     "device_count=0\n"
		     "matching_device_count=0\n"
		     "matching_amd_gpu=false\n"
		     "error_hresult=0x80070490\n"
		     "status=unavailable\n",
		     "reports controlled missing-provider evidence");

	expect_equal(windows_ml_smoke::sanitize_single_line("first\r\nsecond\nthird"), "first  second third",
		     "sanitizes errors to one line");

	if (failures != 0) {
		std::cerr << failures << " assertion(s) failed\n";
		return 1;
	}
	return 0;
}

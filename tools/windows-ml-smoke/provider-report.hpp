// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "windows-ml-provider.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace windows_ml_smoke {

[[nodiscard]] std::string sanitize_single_line(std::string_view value);
[[nodiscard]] std::string format_hresult(std::uint32_t value);

[[nodiscard]] std::string format_provider_discovery_report(const windows_ml::ProviderDiscoveryResult &result);

[[nodiscard]] std::string format_provider_preparation_report(const windows_ml::ProviderPreparationResult &result);

} // namespace windows_ml_smoke

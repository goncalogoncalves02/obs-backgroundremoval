// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "windows-ml-session-policy.hpp"
#include <winml/onnxruntime_cxx_api.h>

#include <filesystem>
#include <memory>

namespace windows_ml {

struct SessionCreationResult {
	std::unique_ptr<Ort::Session> session;
	SessionDiagnostics diagnostics;
};

// The caller owns environment and must destroy the returned session before it.
[[nodiscard]] SessionCreationResult create_session(Ort::Env &environment, const std::filesystem::path &model,
						   const SessionRequest &request);

} // namespace windows_ml

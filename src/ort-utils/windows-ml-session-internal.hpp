// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "windows-ml-session.hpp"

namespace windows_ml {

// Native test seam. Production callers use create_session().
class SessionOperations {
public:
	virtual ~SessionOperations() = default;
	[[nodiscard]] virtual ProviderSessionResult configure(Ort::Env &environment, Ort::SessionOptions &options,
							      std::string_view exact_name) = 0;
	[[nodiscard]] virtual std::unique_ptr<Ort::Session>
	construct(Ort::Env &environment, const std::filesystem::path &model, const Ort::SessionOptions &options) = 0;
};

[[nodiscard]] SessionCreationResult create_session_with_operations(Ort::Env &environment,
								   const std::filesystem::path &model,
								   const SessionRequest &request,
								   SessionOperations &operations);

} // namespace windows_ml

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "gpu-image-pipeline.hpp"
#include <string_view>
namespace gpu_image {
inline std::string_view processing_status_text_key(const ProcessingSnapshot &snapshot)
{
	switch (snapshot.state) {
	case ProcessingState::Off:
		return "GPUImageProcessingOff";
	case ProcessingState::Pending:
		return "GPUImageProcessingPending";
	case ProcessingState::PreprocessOnly:
		return "GPUImageProcessingPreprocessOnly";
	case ProcessingState::Active:
		return "GPUImageProcessingActive";
	case ProcessingState::Unavailable:
		return "GPUImageProcessingUnavailable";
	case ProcessingState::CpuProcessingFallback:
		return "GPUImageProcessingCpuFallback";
	}
	return "GPUImageProcessingUnavailable";
}
} // namespace gpu_image

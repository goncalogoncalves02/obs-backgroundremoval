// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <iostream>
#include <stdexcept>
#if __has_include("gpu-image-status.hpp")
#include "gpu-image-status.hpp"
#include <array>
#include <string_view>
#include <utility>
#endif

int main()
{
#if __has_include("gpu-image-status.hpp")
	try {
		using namespace gpu_image;
		const std::array cases{
			std::pair{ProcessingState::Off, std::string_view{"GPUImageProcessingOff"}},
			std::pair{ProcessingState::Pending, std::string_view{"GPUImageProcessingPending"}},
			std::pair{ProcessingState::PreprocessOnly,
				  std::string_view{"GPUImageProcessingPreprocessOnly"}},
			std::pair{ProcessingState::Active, std::string_view{"GPUImageProcessingActive"}},
			std::pair{ProcessingState::Unavailable, std::string_view{"GPUImageProcessingUnavailable"}},
			std::pair{ProcessingState::CpuProcessingFallback,
				  std::string_view{"GPUImageProcessingCpuFallback"}},
		};
		for (const auto &[state, key] : cases) {
			ProcessingSnapshot snapshot{};
			snapshot.state = state;
			snapshot.requested = state != ProcessingState::Off;
			if (processing_status_text_key(snapshot) != key)
				throw std::runtime_error("Processing status conflates requested and active states");
			snapshot.similarity_full_readback = true;
			if (processing_status_text_key(snapshot) != key)
				throw std::runtime_error("Compatibility readback rewrote the actual processing state");
		}
		std::cout << "GPU image status policy PASS (pure mapping, no hardware evidence)\n";
		return 0;
	} catch (const std::exception &error) {
		std::cerr << error.what() << '\n';
		return 1;
	}
#else
	std::cerr << "Task4 expected RED: gpu-image-status.hpp absent (pure status interface only)\n";
	return 1;
#endif
}

// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu-image-policy.hpp"

#include <limits>

namespace gpu_image {

static bool valid_dimensions(Dimensions dimensions)
{
	return dimensions.width > 0 && dimensions.height > 0 &&
	       dimensions.width <= static_cast<uint32_t>(std::numeric_limits<int>::max()) &&
	       dimensions.height <= static_cast<uint32_t>(std::numeric_limits<int>::max());
}

ProcessingDecision evaluate_processing_request(const PipelineConfig &config)
{
	const bool eligible = config.requested && config.windows && config.mediapipe && config.session_ready &&
			      config.effective_directml && valid_dimensions(config.source) &&
			      valid_dimensions(config.input);
	return {eligible, eligible, eligible && config.image_similarity};
}

bool accept_frame(const FrameStamp &stamp, const PipelineConfig &config)
{
	return evaluate_processing_request(config).eligible && config.generation != 0 && stamp.frame_id != 0 &&
	       stamp.generation == config.generation && stamp.source == config.source && stamp.input == config.input;
}

} // namespace gpu_image

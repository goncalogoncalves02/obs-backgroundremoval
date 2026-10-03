// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once
#include "gpu-image-policy.hpp"
#include <opencv2/core.hpp>

namespace gpu_image {
struct SmallMaskPreparation {
	cv::Mat mask;
	cv::Mat temporal_history;
};
// Both outputs own independent storage; previous means the prior uncontoured history.
SmallMaskPreparation prepare_small_mask(const cv::Mat &probability, const cv::Mat &previous,
					const MaskSettings &settings);
// Retains the original threshold-dependent size and in-place OpenCV operations.
cv::Mat finish_mask_cpu(const cv::Mat &small, Dimensions source, const MaskSettings &settings);
} // namespace gpu_image

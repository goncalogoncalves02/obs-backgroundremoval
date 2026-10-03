// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "background-mask-cpu.hpp"
#include <opencv2/imgproc.hpp>

namespace gpu_image {
SmallMaskPreparation prepare_small_mask(const cv::Mat &probability, const cv::Mat &previous,
					const MaskSettings &settings)
{
	cv::Mat backgroundMask;
	// If we have a threshold, apply it. Otherwise, just use the output image as the mask
	if (settings.enable_threshold) {
		// We need to make settings.threshold (float [0,1]) be in that range
		const uint8_t threshold_value = (uint8_t)(settings.threshold * 255.0f);
		backgroundMask = probability < threshold_value;
	} else {
		backgroundMask = 255 - probability;
	}

	// Temporal smoothing
	if (settings.temporal_smooth_factor > 0.0 && settings.temporal_smooth_factor < 1.0 && !previous.empty() &&
	    previous.size() == backgroundMask.size()) {

		float temporalSmoothFactor = settings.temporal_smooth_factor;
		if (settings.enable_threshold) {
			// The temporal smooth factor can't be smaller than the threshold
			temporalSmoothFactor = std::max(temporalSmoothFactor, settings.threshold);
		}

		cv::addWeighted(backgroundMask, temporalSmoothFactor, previous, 1.0 - temporalSmoothFactor, 0.0,
				backgroundMask);
	}

	cv::Mat history = backgroundMask.clone();

	// Contour processing
	// Only applicable if we are thresholding (and get a binary image)
	if (settings.enable_threshold) {
		if (settings.contour_filter > 0.0 && settings.contour_filter < 1.0) {
			std::vector<std::vector<cv::Point>> contours;
			findContours(backgroundMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
			std::vector<std::vector<cv::Point>> filteredContours;
			const double contourSizeThreshold = (double)(backgroundMask.total()) * settings.contour_filter;
			for (auto &contour : contours) {
				if (cv::contourArea(contour) > (double)contourSizeThreshold) {
					filteredContours.push_back(contour);
				}
			}
			backgroundMask.setTo(0);
			drawContours(backgroundMask, filteredContours, -1, cv::Scalar(255), -1);
		}
	}
	return {backgroundMask, history};
}
cv::Mat finish_mask_cpu(const cv::Mat &small, Dimensions source, const MaskSettings &settings)
{
	cv::Mat backgroundMask = small.clone();
	if (settings.enable_threshold) {

		if (settings.smooth_contour > 0.0) {
			int k_size = (int)(3 + 11 * settings.smooth_contour);
			k_size += k_size % 2 == 0 ? 1 : 0;
			cv::stackBlur(backgroundMask, backgroundMask, cv::Size(k_size, k_size));
		}

		// Resize the size of the mask back to the size of the original input.
		cv::resize(backgroundMask, backgroundMask,
			   cv::Size(static_cast<int>(source.width), static_cast<int>(source.height)));

		// Additional contour processing at full resolution
		if (settings.smooth_contour > 0.0) {
			// If the mask was smoothed, apply a threshold to get a binary mask
			backgroundMask = backgroundMask > 128;
		}

		// Expand or shrink the mask
		if (settings.mask_expansion > 0.0) {
			cv::erode(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1),
				  settings.mask_expansion);
		} else if (settings.mask_expansion < 0.0) {
			cv::dilate(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1),
				   -settings.mask_expansion);
		}

		if (settings.feather > 0.0) {
			// Feather (blur) the mask
			int k_size = (int)(40 * settings.feather);
			k_size += k_size % 2 == 0 ? 1 : 0;
			cv::dilate(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1), k_size / 3);
			cv::boxFilter(backgroundMask, backgroundMask, CV_8U, cv::Size(k_size, k_size));
		}
	}
	return backgroundMask;
}
} // namespace gpu_image

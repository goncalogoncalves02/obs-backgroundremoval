// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu-image-policy.hpp"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <stdexcept>

#if __has_include("background-mask-cpu.hpp")
#include "background-mask-cpu.hpp"
#endif

using namespace gpu_image;

// Frozen baseline from background-filter.cpp, before extracting the production helper.
namespace legacy {
struct Preparation {
	cv::Mat mask, temporal_history;
};
Preparation prepare(const cv::Mat &probability, const cv::Mat &previous, const MaskSettings &settings)
{
	cv::Mat backgroundMask;
	// If we have a threshold, apply it. Otherwise, just use the output image as the mask
	if (settings.enable_threshold) {
		// We need to make settings.threshold (float [0,1]) be in that range
		const uint8_t threshold_value = (uint8_t)(settings.threshold * 255.0f);
		backgroundMask = probability < threshold_value;
	} else {
		backgroundMask = 255 - probability;
	} // Temporal smoothing
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
cv::Mat finish(const cv::Mat &small, Dimensions source, const MaskSettings &settings)
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
} // namespace legacy

static void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
static void equal(const cv::Mat &actual, const cv::Mat &expected, const char *message)
{
	require(actual.type() == CV_8UC1 && actual.size() == expected.size() &&
			cv::norm(actual, expected, cv::NORM_INF) == 0,
		message);
}

template<typename Prepare, typename Finish> static void fixed_expectations(Prepare prepare, Finish finish)
{
	MaskSettings settings{};
	cv::Mat values = (cv::Mat_<uint8_t>(1, 7) << 0, 126, 127, 128, 129, 254, 255);
	const auto saved = values.clone();
	auto result = prepare(values, {}, settings);
	equal(result.mask, (cv::Mat_<uint8_t>(1, 7) << 255, 129, 128, 127, 126, 1, 0), "Inversion bytes changed");
	equal(result.temporal_history, result.mask, "Uncontoured history must contain prepared bytes");
	settings.enable_threshold = true;
	settings.threshold = 0.5f;
	result = prepare(values, {}, settings);
	equal(result.mask, (cv::Mat_<uint8_t>(1, 7) << 255, 255, 0, 0, 0, 0, 0),
	      "Initial threshold must truncate127 and compare strictly less");
	settings.enable_threshold = false;
	settings.temporal_smooth_factor = 0.5f;
	result = prepare((cv::Mat_<uint8_t>(1, 4) << 255, 254, 253, 252), cv::Mat::zeros(1, 4, CV_8UC1), settings);
	equal(result.mask, (cv::Mat_<uint8_t>(1, 4) << 0, 0, 1, 2), "Temporal8U ties must round to even");
	settings.enable_threshold = true;
	settings.threshold = 0.75f;
	settings.temporal_smooth_factor = 0.1f;
	result = prepare(cv::Mat::zeros(1, 4, CV_8UC1), cv::Mat::zeros(1, 4, CV_8UC1), settings);
	equal(result.mask, cv::Mat(1, 4, CV_8UC1, cv::Scalar(191)), "Threshold must bound temporal factor");
	settings.temporal_smooth_factor = 0.5f;
	result = prepare(values, cv::Mat::zeros(2, 2, CV_8UC1), settings);
	equal(result.mask, (cv::Mat_<uint8_t>(1, 7) << 255, 255, 255, 255, 255, 0, 0),
	      "Mismatched previous dimensions must skip temporal smoothing");
	settings.threshold = 0.5f;
	settings.temporal_smooth_factor = 1.0f;
	settings.contour_filter = 0.05f;
	cv::Mat probability(10, 10, CV_8UC1, cv::Scalar(255));
	probability(cv::Rect(1, 1, 4, 4)).setTo(0);
	probability.at<uint8_t>(8, 8) = 0;
	cv::Mat expected = cv::Mat::zeros(10, 10, CV_8UC1);
	expected(cv::Rect(1, 1, 4, 4)).setTo(255);
	result = prepare(probability, {}, settings);
	equal(result.mask, expected, "Small contour must be removed");
	expected.at<uint8_t>(8, 8) = 255;
	equal(result.temporal_history, expected, "History must precede contour removal");
	result.mask.setTo(17);
	equal(result.temporal_history, expected, "History must own independent storage");
	equal(values, saved, "Probability input must remain unchanged");

	settings = {};
	settings.enable_threshold = true;
	cv::Mat edge = (cv::Mat_<uint8_t>(1, 3) << 0, 128, 255);
	equal(finish(edge, {3, 1}, settings), edge, "Zero smoothing must not apply postthreshold");
	settings.smooth_contour = 0.001f; // k=3, in-place small-kernel scalar row
	cv::Mat impulse = cv::Mat::zeros(3, 7, CV_8UC1);
	impulse.col(1).setTo(255);
	cv::Mat binary_expected = cv::Mat::zeros(3, 7, CV_8UC1);
	binary_expected.col(1).setTo(255);
	equal(finish(impulse, {7, 3}, settings), binary_expected, "In-place stack blur and >128 ordering changed");
	settings.smooth_contour = 0;
	settings.mask_expansion = 1;
	cv::Mat square = cv::Mat::zeros(5, 5, CV_8UC1);
	square(cv::Rect(1, 1, 3, 3)).setTo(255);
	cv::Mat eroded = cv::Mat::zeros(5, 5, CV_8UC1);
	eroded.at<uint8_t>(2, 2) = 255;
	equal(finish(square, {5, 5}, settings), eroded, "Positive expansion must erode");
	settings.mask_expansion = -1;
	equal(finish(eroded, {5, 5}, settings), square, "Negative expansion must dilate");
	settings.mask_expansion = 0;
	settings.feather = 0.05f; // k=3, dilate1 then normalized box BORDER_REFLECT_101
	cv::Mat corner = cv::Mat::zeros(3, 3, CV_8UC1);
	corner.at<uint8_t>(0, 0) = 255;
	equal(finish(corner, {3, 3}, settings),
	      (cv::Mat_<uint8_t>(3, 3) << 255, 170, 170, 170, 113, 113, 170, 113, 113),
	      "Feather dilation and reflect101 box changed");
	settings.enable_threshold = false;
	settings.smooth_contour = 1;
	settings.feather = 1;
	settings.mask_expansion = 30;
	equal(finish(edge, {1280, 720}, settings), edge,
	      "Disabled threshold must retain input size and skip all spatial processing");
	std::cout << "fixed-mask-reference PASS" << std::endl;
}

int main()
{
	try {
		std::cout << "opencv-mask-reference version=" << CV_VERSION << std::endl;
		fixed_expectations(legacy::prepare, legacy::finish);
#if __has_include("background-mask-cpu.hpp")
		fixed_expectations(prepare_small_mask, finish_mask_cpu);
		return 0;
#else
		throw std::runtime_error("Task3 RED: missing background-mask-cpu.hpp helper implementation");
#endif
	} catch (const std::exception &error) {
		std::cerr << "mask-reference FAIL: " << error.what() << std::endl;
		return 1;
	}
}

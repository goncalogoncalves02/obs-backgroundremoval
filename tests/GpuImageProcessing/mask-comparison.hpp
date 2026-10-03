// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <opencv2/core.hpp>
#include <cmath>
#include <stdexcept>

namespace gpu_test {
struct MaskComparison {
	double normalized_mae;
	double retained_alpha_iou;
	double positive_background_iou;
	int retained_alpha_union;
	int positive_background_union;
	bool passes(bool require_both_classes = false) const
	{
		return std::isfinite(normalized_mae) && normalized_mae <= 0.01 && std::isfinite(retained_alpha_iou) &&
		       retained_alpha_iou >= 0.98 && std::isfinite(positive_background_iou) &&
		       positive_background_iou >= 0.98 &&
		       (!require_both_classes || (retained_alpha_union > 0 && positive_background_union > 0));
	}
};
inline MaskComparison compare_masks(const cv::Mat &actual, const cv::Mat &reference)
{
	if (actual.empty() || actual.size() != reference.size() || actual.type() != CV_8UC1 ||
	    reference.type() != CV_8UC1)
		throw std::invalid_argument("Final mask comparison requires nonempty matching byte masks");
	const auto iou = [](const cv::Mat &a, const cv::Mat &b, int &union_pixels) {
		cv::Mat both, either;
		cv::bitwise_and(a, b, both);
		cv::bitwise_or(a, b, either);
		union_pixels = cv::countNonZero(either);
		// Synthetic empty/full masks: an empty union means both masks lack this class.
		// Real portrait callers additionally require nonempty unions for both classes.
		return union_pixels == 0 ? 1.0 : static_cast<double>(cv::countNonZero(both)) / union_pixels;
	};
	MaskComparison result{};
	result.normalized_mae =
		cv::norm(actual, reference, cv::NORM_L1) / (static_cast<double>(reference.total()) * 255.0);
	// Production alpha = 1 - mask.r. Strict alpha > 0.5 is byte mask < 128 (0..127).
	result.retained_alpha_iou = iou(actual < 128, reference < 128, result.retained_alpha_union);
	// Preserve the previous positive-mask gate exactly (>128), now labelled as background.
	// Byte 128 is excluded from both strict gates; the retained threshold regression pins this boundary.
	result.positive_background_iou = iou(actual > 128, reference > 128, result.positive_background_union);
	return result;
}
} // namespace gpu_test

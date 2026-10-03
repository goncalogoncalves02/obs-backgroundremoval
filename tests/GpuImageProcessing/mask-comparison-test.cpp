// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "mask-comparison.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
} // namespace
int main()
{
	try {
		cv::Mat person(100, 100, CV_8UC1, cv::Scalar(255));
		person(cv::Rect(0, 0, 10, 10)).setTo(0);
		const cv::Mat removed(100, 100, CV_8UC1, cv::Scalar(255));
		auto lost = gpu_test::compare_masks(removed, person);
		require(lost.normalized_mae == 0.01 && lost.positive_background_iou == 0.99,
			"Small retained-person fixture must pass legacy MAE/background gates");
		require(lost.retained_alpha_iou == 0 && !lost.passes(), "Complete retained-person loss must fail");
		auto slight = person.clone();
		slight.at<uint8_t>(0, 0) = 255;
		slight.at<uint8_t>(0, 1) = 255;
		require(gpu_test::compare_masks(slight, person).passes(), "Exact 0.98 retained IoU must pass");
		slight.at<uint8_t>(0, 2) = 255;
		require(!gpu_test::compare_masks(slight, person).passes(), "0.97 retained IoU must fail");
		cv::Mat boundary(1, 4, CV_8UC1);
		boundary.at<uint8_t>(0, 0) = 0;
		boundary.at<uint8_t>(0, 1) = 127;
		boundary.at<uint8_t>(0, 2) = 128;
		boundary.at<uint8_t>(0, 3) = 255;
		auto exact = gpu_test::compare_masks(boundary, boundary);
		require(exact.retained_alpha_union == 2 && exact.positive_background_union == 1 && exact.passes(),
			"Alpha > 0.5 is mask < 128; the retained legacy positive gate is mask > 128");
		auto shifted = boundary.clone();
		shifted.at<uint8_t>(0, 1) = 128;
		auto crossing = gpu_test::compare_masks(shifted, boundary);
		require(crossing.retained_alpha_iou == 0.5 && crossing.positive_background_iou == 1 &&
				!crossing.passes(),
			"127 to 128 loses retained alpha at the binary boundary");
		for (const uint8_t value : {uint8_t{0}, uint8_t{255}}) {
			cv::Mat uniform(4, 4, CV_8UC1, cv::Scalar(value));
			const auto same = gpu_test::compare_masks(uniform, uniform);
			require(same.passes() && same.retained_alpha_iou == 1 && same.positive_background_iou == 1,
				"Identical synthetic empty/full classes explicitly match");
			require(!same.passes(true), "Portrait cannot use empty-class success");
			cv::Mat opposite(4, 4, CV_8UC1, cv::Scalar(255 - value));
			require(!gpu_test::compare_masks(opposite, uniform).passes(),
				"Opposite empty/full masks must fail");
		}
		std::cout << "Shared mask comparator retained-alpha/background/MAE/boundary/empty/full PASS"
			  << std::endl;
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << std::endl;
		return 1;
	}
}

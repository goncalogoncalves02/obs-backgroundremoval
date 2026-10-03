// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <opencv2/core.hpp>
#include <new>
#include <functional>
#include <utility>

namespace gpu_test {
// Real OpenCV allocation failure, scoped to the calling test thread. Other OBS threads delegate normally.
// OpenCV may retry its default allocator after an exception, so failure remains armed until scope exit.
class MatAllocationFault final : public cv::MatAllocator {
public:
	static inline thread_local std::function<void()> before_allocate;
	static inline thread_local int remaining = -1;
	static inline thread_local unsigned allocations = 0;
	static inline thread_local int matched_type = -1;
	cv::UMatData *allocate(int dims, const int *sizes, int type, void *data, size_t *step, cv::AccessFlag flags,
			       cv::UMatUsageFlags usage) const override
	{
		if (!data && (matched_type < 0 || type == matched_type)) {
			++allocations;
			if (auto callback = std::exchange(before_allocate, {}))
				callback();
			if (remaining == 0)
				throw std::bad_alloc();
			if (remaining > 0)
				--remaining;
		}
		return cv::Mat::getStdAllocator()->allocate(dims, sizes, type, data, step, flags, usage);
	}
	bool allocate(cv::UMatData *data, cv::AccessFlag flags, cv::UMatUsageFlags usage) const override
	{
		return cv::Mat::getStdAllocator()->allocate(data, flags, usage);
	}
	void deallocate(cv::UMatData *data) const override { cv::Mat::getStdAllocator()->deallocate(data); }
};
class MatAllocationScope {
public:
	explicit MatAllocationScope(int after = 0, int type = -1) : previous_(cv::Mat::getDefaultAllocator())
	{
		static MatAllocationFault allocator;
		MatAllocationFault::remaining = after;
		MatAllocationFault::matched_type = type;
		MatAllocationFault::allocations = 0;
		cv::Mat::setDefaultAllocator(&allocator);
	}
	~MatAllocationScope()
	{
		cv::Mat::setDefaultAllocator(previous_);
		MatAllocationFault::before_allocate = {};
		MatAllocationFault::remaining = -1;
		MatAllocationFault::matched_type = -1;
	}
	MatAllocationScope(const MatAllocationScope &) = delete;
	MatAllocationScope &operator=(const MatAllocationScope &) = delete;

private:
	cv::MatAllocator *previous_;
};
} // namespace gpu_test

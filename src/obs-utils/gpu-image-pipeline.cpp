// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpu-image-pipeline.hpp"

#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace gpu_image {

static bool matches_image(const cv::Mat &image, Dimensions dimensions, int type)
{
	return !image.empty() && image.dims == 2 && image.type() == type &&
	       image.cols == static_cast<int>(dimensions.width) && image.rows == static_cast<int>(dimensions.height);
}

uint64_t ImagePipeline::configure(PipelineConfig config)
{
	std::lock_guard lock(mutex_);
	// The mailbox owns generation assignment, regardless of the caller's snapshot.
	config.generation = config_.generation;
	if (configured_ && config == config_)
		return config_.generation;
	if (config_.generation == std::numeric_limits<uint64_t>::max())
		throw std::overflow_error("GPU image pipeline generation exhausted");
	config.generation = config_.generation + 1;
	config_ = config;
	configured_ = true;
	valid_ = true;
	frame_.reset();
	mask_.reset();
	if (!config.requested) {
		state_ = ProcessingState::Off;
		reason_.clear();
	} else if (evaluate_processing_request(config).eligible) {
		state_ = ProcessingState::Pending;
		reason_ = "GPU image processing preparation pending";
	} else {
		state_ = ProcessingState::Unavailable;
		reason_ =
			"GPU image processing requires a ready Windows MediaPipe DirectML session and valid dimensions";
	}
	return config_.generation;
}

PipelineConfig ImagePipeline::snapshot() const
{
	std::lock_guard lock(mutex_);
	return config_;
}

bool ImagePipeline::publish_frame(FramePacket packet)
{
	try {
		packet.input_bgra = packet.input_bgra.clone();
		packet.similarity_bgra = packet.similarity_bgra.clone();
	} catch (const cv::Exception &) {
		return false;
	} catch (const std::bad_alloc &) {
		return false;
	}
	std::lock_guard lock(mutex_);
	if (!valid_ || state_ == ProcessingState::CpuProcessingFallback || !accept_frame(packet.stamp, config_) ||
	    (frame_ && packet.stamp.frame_id <= frame_->stamp.frame_id) ||
	    !matches_image(packet.input_bgra, config_.input, CV_8UC4) ||
	    (config_.image_similarity && !matches_image(packet.similarity_bgra, config_.source, CV_8UC4)) ||
	    (!packet.similarity_bgra.empty() && !matches_image(packet.similarity_bgra, config_.source, CV_8UC4)))
		return false;
	frame_ = std::move(packet);
	return true;
}

std::optional<FramePacket> ImagePipeline::latest_frame() const
{
	std::optional<FramePacket> packet;
	{
		std::lock_guard lock(mutex_);
		packet = frame_;
	}
	if (!packet)
		return std::nullopt;
	packet->input_bgra = packet->input_bgra.clone();
	packet->similarity_bgra = packet->similarity_bgra.clone();
	return packet;
}

bool ImagePipeline::publish_mask(MaskPacket packet)
{
	try {
		packet.mask = packet.mask.clone();
	} catch (const cv::Exception &) {
		return false;
	} catch (const std::bad_alloc &) {
		return false;
	}
	std::lock_guard lock(mutex_);
	const Dimensions expected = packet.gpu_postprocess || !config_.mask.enable_threshold ? config_.input
											     : config_.source;
	if (!valid_ || state_ == ProcessingState::CpuProcessingFallback || !accept_frame(packet.stamp, config_) ||
	    (mask_ && packet.stamp.frame_id <= mask_->stamp.frame_id) || !matches_image(packet.mask, expected, CV_8UC1))
		return false;
	mask_ = std::move(packet);
	return true;
}

std::optional<MaskPacket> ImagePipeline::latest_mask() const
{
	std::optional<MaskPacket> packet;
	{
		std::lock_guard lock(mutex_);
		packet = mask_;
	}
	if (!packet)
		return std::nullopt;
	packet->mask = packet->mask.clone();
	return packet;
}

bool ImagePipeline::set_processing_state(uint64_t generation, ProcessingState state, std::string reason)
{
	std::lock_guard lock(mutex_);
	const bool needs_gpu_input = state == ProcessingState::Active || state == ProcessingState::PreprocessOnly ||
				     state == ProcessingState::CpuProcessingFallback;
	if (!valid_ || generation != config_.generation ||
	    (needs_gpu_input && !evaluate_processing_request(config_).eligible))
		return false;
	// Processing fallback changes the image route while keeping DirectML inference configured.
	// Late optimized work must not become usable in either direction of this transition.
	const bool route_changed = (state_ == ProcessingState::CpuProcessingFallback) !=
				   (state == ProcessingState::CpuProcessingFallback);
	if (route_changed) {
		if (config_.generation == std::numeric_limits<uint64_t>::max())
			return false;
		++config_.generation;
		frame_.reset();
		mask_.reset();
	}
	state_ = state;
	reason_ = std::move(reason);
	return true;
}

ProcessingSnapshot ImagePipeline::processing_snapshot() const
{
	std::lock_guard lock(mutex_);
	const bool preprocess = valid_ && evaluate_processing_request(config_).eligible &&
				(state_ == ProcessingState::PreprocessOnly || state_ == ProcessingState::Active);
	return {config_.requested,
		state_,
		config_.generation,
		config_.source,
		config_.input,
		preprocess,
		preprocess && state_ == ProcessingState::Active,
		preprocess && config_.image_similarity,
		reason_};
}

void ImagePipeline::invalidate()
{
	std::lock_guard lock(mutex_);
	valid_ = false;
	frame_.reset();
	mask_.reset();
	state_ = config_.requested ? ProcessingState::Unavailable : ProcessingState::Off;
	reason_ = "GPU image pipeline invalidated";
}

} // namespace gpu_image

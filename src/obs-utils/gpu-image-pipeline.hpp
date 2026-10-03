// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "gpu-image-policy.hpp"

#include <opencv2/core.hpp>

#include <mutex>
#include <optional>
#include <string>

namespace gpu_image {

struct FramePacket {
	FrameStamp stamp;
	cv::Mat input_bgra;
	cv::Mat similarity_bgra;
};

struct MaskPacket {
	FrameStamp stamp;
	// Prepared shader Stage2 input is input-sized CV_8UC1. Finished legacy output is CV_8UC1,
	// source-sized with threshold enabled and input-sized with threshold disabled.
	cv::Mat mask;
	bool gpu_postprocess = false;
};

struct ProcessingSnapshot {
	bool requested = false;
	ProcessingState state = ProcessingState::Off;
	uint64_t generation = 0;
	Dimensions source;
	Dimensions input;
	bool preprocess_active = false;
	bool mask_active = false;
	bool similarity_full_readback = false;
	std::string reason;
};

// Publication and reads clone pixels outside the mailbox lock using immutable owned snapshots. Returned packets may be
// mutated by consumers without changing the stored snapshot; caller buffers must stay stable
// for the duration of a publication call.
class ImagePipeline {
public:
	uint64_t configure(PipelineConfig config);
	PipelineConfig snapshot() const;
	// False means rejected authority/shape/order; clone allocation exceptions propagate to the caller.
	bool publish_frame(FramePacket packet);
	std::optional<FramePacket> latest_frame() const;
	bool publish_mask(MaskPacket packet);
	std::optional<MaskPacket> latest_mask() const;
	// Entering/leaving CpuProcessingFallback assigns a new generation; read snapshot() after success.
	bool set_processing_state(uint64_t generation, ProcessingState state, std::string reason);
	ProcessingSnapshot processing_snapshot() const;
	void invalidate();

private:
	mutable std::mutex mutex_;
	PipelineConfig config_;
	bool configured_ = false;
	bool valid_ = false;
	std::optional<FramePacket> frame_;
	std::optional<MaskPacket> mask_;
	ProcessingState state_ = ProcessingState::Off;
	std::string reason_;
};

} // namespace gpu_image

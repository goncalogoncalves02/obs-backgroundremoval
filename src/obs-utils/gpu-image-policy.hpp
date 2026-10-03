// SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

namespace gpu_image {

struct Dimensions {
	uint32_t width = 0;
	uint32_t height = 0;
	bool operator==(const Dimensions &) const = default;
};

struct FrameStamp {
	uint64_t generation = 0;
	uint64_t frame_id = 0;
	Dimensions source;
	Dimensions input;
};

struct MaskSettings {
	bool enable_threshold = false;
	float threshold = 0;
	float temporal_smooth_factor = 0;
	float contour_filter = 0;
	float smooth_contour = 0;
	float feather = 0;
	int mask_expansion = 0;
	bool operator==(const MaskSettings &) const = default;
};

struct PipelineConfig {
	uint64_t generation = 0;
	Dimensions source;
	Dimensions input;
	bool requested = false;
	bool windows = false;
	bool mediapipe = false;
	bool session_ready = false;
	bool effective_directml = false;
	bool image_similarity = false;
	uint32_t mask_every_x_frames = 1;
	double similarity_threshold = 0;
	MaskSettings mask;
	bool operator==(const PipelineConfig &) const = default;
};

struct ProcessingDecision {
	bool eligible = false;
	bool small_readback = false;
	bool full_similarity_readback = false;
};

enum class ProcessingState { Off, Pending, PreprocessOnly, Active, Unavailable, CpuProcessingFallback };

ProcessingDecision evaluate_processing_request(const PipelineConfig &config);
bool accept_frame(const FrameStamp &stamp, const PipelineConfig &config);

} // namespace gpu_image

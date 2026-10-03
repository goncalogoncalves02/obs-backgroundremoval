// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "background-filter.h"

#if __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#else
#error "onnxruntime_cxx_api.h was not found"
#endif

#ifdef _WIN32
#include <wchar.h>
#endif // _WIN32

#include <opencv2/core/version.hpp>

#if CV_VERSION_MAJOR >= 5
#include <opencv2/geometry.hpp>
#endif

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <memory>
#include <exception>
#include <new>
#include <fstream>
#include <new>
#include <mutex>
#include <regex>
#include <thread>

#include "plugin-support.h"
#include "UI/AboutDialogIntegration.hpp"
#include "UpdateConfig/UpdateConfig.hpp"
#include "models/ModelSINET.hpp"
#include "models/ModelMediapipe.hpp"
#include "models/ModelSelfie.hpp"
#include "models/ModelSelfieMulticlass.hpp"
#include "models/ModelRVM.hpp"
#include "models/ModelPPHumanSeg.hpp"
#include "models/ModelTCMonoDepth.hpp"
#include "FilterData.hpp"
#include "ort-utils/ort-session-utils.hpp"
#include "obs-utils/obs-utils.hpp"
#include "consts.h"
#ifdef _WIN32
#include "obs-utils/windows-ml-status.hpp"
#include "obs-utils/gpu-image-status.hpp"
#include "obs-utils/gpu-input-preprocessor.hpp"
#include "obs-utils/gpu-mask-processor.hpp"
#include "obs-utils/background-mask-cpu.hpp"
#endif

#ifdef _WIN32
namespace image_filter_detail {
using ProcessingClock = std::chrono::steady_clock;
struct ImageRenderSettings {
	int64_t blur = 0;
	bool focal = false;
	float focus_point = 0.1f, focus_depth = 0.0f;
};
struct ImageTelemetry {
	uint64_t processed = 0, skipped = 0, stale = 0, frames = 0;
	uint64_t input_pixels = 0, similarity_pixels = 0;
	double capture_ms = 0, inference_ms = 0, mask_ms = 0;
	ProcessingClock::time_point since = ProcessingClock::now();
};
uint64_t image_hash(std::string_view text, uint64_t hash = 14695981039346656037ULL)
{
	for (unsigned char character : text) {
		hash ^= character;
		hash *= 1099511628211ULL;
	}
	return hash;
}
std::string image_hash_text(uint64_t hash)
{
	std::ostringstream text;
	text << std::hex << std::setw(16) << std::setfill('0') << hash;
	return text.str();
}
std::string image_settings_fingerprint(obs_data_t *settings)
{
	// Include saved and default settings, except the comparison's single varied control.
	obs_data_t *copy = obs_data_create_from_json(obs_data_get_json_with_defaults(settings));
	if (!copy)
		throw std::bad_alloc();
	obs_data_erase(copy, "gpu_image_processing");
	const auto hash = image_hash(obs_data_get_json(copy));
	obs_data_release(copy);
	return image_hash_text(hash);
}
const char *image_state_name(gpu_image::ProcessingState state)
{
	switch (state) {
	case gpu_image::ProcessingState::Off:
		return "Off";
	case gpu_image::ProcessingState::Pending:
		return "Pending";
	case gpu_image::ProcessingState::PreprocessOnly:
		return "PreprocessOnly";
	case gpu_image::ProcessingState::Active:
		return "Active";
	case gpu_image::ProcessingState::Unavailable:
		return "Unavailable";
	case gpu_image::ProcessingState::CpuProcessingFallback:
		return "CpuProcessingFallback";
	}
	return "Unavailable";
}
double image_elapsed_ms(ProcessingClock::time_point start)
{
	return std::chrono::duration<double, std::milli>(ProcessingClock::now() - start).count();
}
} // namespace image_filter_detail
using namespace image_filter_detail;
#endif

struct background_removal_filter : public filter_data, public std::enable_shared_from_this<background_removal_filter> {
	bool enableThreshold = true;
	std::atomic<bool> stopWhenSourceIsInactive{true};
	float threshold = 0.5f;
	cv::Scalar backgroundColor{0, 0, 0, 0};
	float contourFilter = 0.05f;
	float smoothContour = 0.5f;
	float feather = 0.0f;
	int maskExpansion = 0;

	cv::Mat backgroundMask;
	cv::Mat lastBackgroundMask;
	cv::Mat lastImageBGRA;
	float temporalSmoothFactor = 0.0f;
	float imageSimilarityThreshold = 35.0f;
	bool enableImageSimilarity = true;
	int maskEveryXFrames = 1;
	int maskEveryXFramesCount = 0;
	int64_t blurBackground = 0;
	bool enableFocalBlur = false;
	float blurFocusPoint = 0.1f;
	float blurFocusDepth = 0.1f;

	gs_effect_t *effect;
	gs_effect_t *kawaseBlurEffect;

	std::mutex modelMutex;
#ifdef _WIN32
	// State is acquired only briefly from graphics/model callbacks. It never acquires either.
	std::mutex imageStateMutex;
	std::mutex updateMutex;
	gpu_image::ImagePipeline imagePipeline;
	gpu_image::GpuInputPreprocessor imagePreprocessor;
	gpu_image::GpuMaskProcessor imageMaskProcessor;
	std::optional<gpu_image::FramePacket> legacyFrame;
	std::optional<gpu_image::MaskPacket> legacyMask;
	cv::Mat imageHistory, imageSimilarityHistory;
	uint64_t historyGeneration = 0, consumedFrame = 0, imageFrameId = 0, displayEpoch = 0;
	uint32_t imageMaskCounter = 0;
	bool imageTerminal = false, imageProcessingFailed = false;
	bool refreshImageProperties = false;
	ImageRenderSettings imageRenderSettings;
	std::string imageEffectiveProvider = "pending", imageSettingsHash, imageSourceHash;
	uint32_t imageFpsNum = 0, imageFpsDen = 0;
	ImageTelemetry imageTelemetry;
	uint64_t imageLoggedGeneration = 0;
	gpu_image::ProcessingState imageLoggedState = gpu_image::ProcessingState::Unavailable;
	inline static std::atomic<uint64_t> nextImageFilterId{0};
	const uint64_t imageFilterId = ++nextImageFilterId;
	// Graphics-owned independent copy; helper textures are borrowed until the next helper call.
	gs_texture_t *imageDisplayTexture = nullptr;
	gs_texture_t *imageCandidateTexture = nullptr;
	bool imageDisplayValid = false;
	uint64_t imageDisplayEpoch = 0, imageDisplayFrame = 0;
#endif

	~background_removal_filter() { obs_log(LOG_INFO, "Background removal filter destructor called"); }
};

void background_removal_thread(void *data); // Forward declaration

const char *background_filter_getname(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("BackgroundRemoval");
}

/**                   PROPERTIES                     */

static bool visible_on_bool(obs_properties_t *ppts, obs_data_t *settings, const char *bool_prop, const char *prop_name)
{
	const bool enabled = obs_data_get_bool(settings, bool_prop);
	obs_property_t *p = obs_properties_get(ppts, prop_name);
	obs_property_set_visible(p, enabled);
	return true;
}

static bool enable_threshold_modified(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	return visible_on_bool(ppts, settings, "enable_threshold", "threshold_group");
}

static bool enable_focal_blur(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	return visible_on_bool(ppts, settings, "enable_focal_blur", "focal_blur_group");
}

static bool enable_image_similarity(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	UNUSED_PARAMETER(p);
	return visible_on_bool(ppts, settings, "enable_image_similarity", "image_similarity_threshold");
}

static bool enable_advanced_settings(obs_properties_t *ppts, obs_property_t *p, obs_data_t *settings)
{
	const bool enabled = obs_data_get_bool(settings, "advanced");
	p = obs_properties_get(ppts, "blur_background");
	obs_property_set_visible(p, true);

	for (const char *prop_name :
	     {"model_select", "useGPU", "mask_every_x_frames", "numThreads", "enable_focal_blur", "enable_threshold",
	      "threshold_group", "focal_blur_group", "temporal_smooth_factor", "image_similarity_threshold",
	      "enable_image_similarity", "mask_expansion"}) {
		p = obs_properties_get(ppts, prop_name);
		obs_property_set_visible(p, enabled);
	}
#ifdef _WIN32
	// Keep the device switch accessible without enabling advanced settings.
	obs_property_set_visible(obs_properties_get(ppts, "useGPU"), true);
#endif

	if (enabled) {
		enable_threshold_modified(ppts, p, settings);
		enable_focal_blur(ppts, p, settings);
		enable_image_similarity(ppts, p, settings);
	}

	return true;
}

obs_properties_t *background_filter_properties(void *data)
try {
	std::unique_ptr<obs_properties_t, decltype(&obs_properties_destroy)> propsOwner(obs_properties_create(),
											&obs_properties_destroy);
	obs_properties_t *props = propsOwner.get();

	obs_property_t *advanced = obs_properties_add_bool(props, "advanced", obs_module_text("Advanced"));

	obs_properties_add_bool(props, "stop_when_source_is_inactive",
				obs_module_text("Stop filter when source is inactive"));

	// If advanced is selected show the advanced settings, otherwise hide them
	obs_property_set_modified_callback(advanced, enable_advanced_settings);

	/* Threshold props */
	obs_property_t *p_enable_threshold =
		obs_properties_add_bool(props, "enable_threshold", obs_module_text("EnableThreshold"));
	obs_property_set_modified_callback(p_enable_threshold, enable_threshold_modified);

	// Threshold props group
	obs_properties_t *threshold_props = obs_properties_create();

	obs_properties_add_float_slider(threshold_props, "threshold", obs_module_text("Threshold"), 0.0, 1.0, 0.025);

	obs_properties_add_float_slider(threshold_props, "contour_filter",
					obs_module_text("ContourFilterPercentOfImage"), 0.0, 1.0, 0.025);

	obs_properties_add_float_slider(threshold_props, "smooth_contour", obs_module_text("SmoothSilhouette"), 0.0,
					1.0, 0.05);

	obs_properties_add_float_slider(threshold_props, "feather", obs_module_text("FeatherBlendSilhouette"), 0.0, 1.0,
					0.05);

	obs_properties_add_group(props, "threshold_group", obs_module_text("ThresholdGroup"), OBS_GROUP_NORMAL,
				 threshold_props);

	/* Mask expansion slider - in advanced settings */
	obs_properties_add_int_slider(props, "mask_expansion", obs_module_text("MaskExpansion"), -30, 30, 1);

	/* GPU, CPU and performance Props */
	obs_property_t *p_use_gpu = obs_properties_add_list(props, "useGPU", obs_module_text("InferenceDevice"),
							    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p_use_gpu, obs_module_text("CPU"), USEGPU_CPU);
#ifdef _WIN32
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUDirectML"), USEGPU_WINML_DIRECTML);
	obs_property_set_long_description(p_use_gpu, obs_module_text("DirectMLMediaPipeOnly"));
	std::string_view statusKey = "InferenceStatusPending";
	auto *ptr = static_cast<std::shared_ptr<background_removal_filter> *>(data);
	if (ptr && *ptr) {
		std::unique_lock<std::mutex> lock((*ptr)->modelMutex);
		statusKey = windows_ml::session_status_text_key((*ptr)->sessionDiagnostics);
	}
	obs_properties_add_text(props, "inference_status", obs_module_text(statusKey.data()), OBS_TEXT_INFO);
	auto *processing =
		obs_properties_add_bool(props, "gpu_image_processing", obs_module_text("GPUImageProcessing"));
	obs_property_set_long_description(processing, obs_module_text("GPUImageProcessingDescription"));
	gpu_image::ProcessingSnapshot processingSnapshot{};
	gpu_image::PipelineConfig processingConfig{};
	if (ptr && *ptr) {
		std::lock_guard lock((*ptr)->imageStateMutex);
		processingSnapshot = (*ptr)->imagePipeline.processing_snapshot();
		processingConfig = (*ptr)->imagePipeline.snapshot();
	}
	// Qualification is independent of the saved checkbox: an eligible Off control must remain actionable.
	processingConfig.requested = true;
	obs_property_set_enabled(processing, gpu_image::evaluate_processing_request(processingConfig).eligible);
	obs_properties_add_text(props, "gpu_image_processing_status",
				obs_module_text(gpu_image::processing_status_text_key(processingSnapshot).data()),
				OBS_TEXT_INFO);
	if (processingSnapshot.similarity_full_readback)
		obs_properties_add_text(props, "gpu_image_similarity_status",
					obs_module_text("GPUImageProcessingSimilarity"), OBS_TEXT_INFO);
#endif
#ifdef HAVE_ONNXRUNTIME_CUDA_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUCUDA"), USEGPU_CUDA);
#endif
#ifdef HAVE_ONNXRUNTIME_ROCM_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUROCM"), USEGPU_ROCM);
#endif
#ifdef HAVE_ONNXRUNTIME_MIGRAPHX_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("GPUMIGRAPHX"), USEGPU_MIGRAPHX);
#endif
#ifdef HAVE_ONNXRUNTIME_TENSORRT_EP
	obs_property_list_add_string(p_use_gpu, obs_module_text("TENSORRT"), USEGPU_TENSORRT);
#endif
#if defined(__APPLE__)
	obs_property_list_add_string(p_use_gpu, obs_module_text("CoreML"), USEGPU_COREML);
#endif

	obs_properties_add_int(props, "mask_every_x_frames", obs_module_text("CalculateMaskEveryXFrame"), 1, 300, 1);
	obs_properties_add_int_slider(props, "numThreads", obs_module_text("NumThreads"), 0, 8, 1);

	/* Model selection Props */
	obs_property_t *p_model_select = obs_properties_add_list(props, "model_select",
								 obs_module_text("SegmentationModel"),
								 OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_STRING);

	obs_property_list_add_string(p_model_select, obs_module_text("SINet"), MODEL_SINET);
	obs_property_list_add_string(p_model_select, obs_module_text("MediaPipe"), MODEL_MEDIAPIPE);
	obs_property_list_add_string(p_model_select, obs_module_text("Selfie Segmentation"), MODEL_SELFIE);
	obs_property_list_add_string(p_model_select, obs_module_text("Selfie Multiclass"), MODEL_SELFIE_MULTICLASS);
	obs_property_list_add_string(p_model_select, obs_module_text("PPHumanSeg"), MODEL_PPHUMANSEG);
	obs_property_list_add_string(p_model_select, obs_module_text("Robust Video Matting"), MODEL_RVM);
	obs_property_list_add_string(p_model_select, obs_module_text("TCMonoDepth"), MODEL_DEPTH_TCMONODEPTH);

	obs_properties_add_float_slider(props, "temporal_smooth_factor", obs_module_text("TemporalSmoothFactor"), 0.0,
					1.0, 0.01);

	obs_property_t *p_enable_image_similarity =
		obs_properties_add_bool(props, "enable_image_similarity", obs_module_text("EnableImageSimilarity"));
	obs_property_set_modified_callback(p_enable_image_similarity, enable_image_similarity);

	obs_properties_add_float_slider(props, "image_similarity_threshold",
					obs_module_text("ImageSimilarityThreshold"), 0.0, 100.0, 1.0);

	/* Background Blur Props */
	obs_properties_add_int_slider(props, "blur_background", obs_module_text("BlurBackgroundFactor0NoBlurUseColor"),
				      0, 20, 1);

	obs_property_t *p_enable_focal_blur =
		obs_properties_add_bool(props, "enable_focal_blur", obs_module_text("EnableFocalBlur"));
	obs_property_set_modified_callback(p_enable_focal_blur, enable_focal_blur);

	obs_properties_t *focal_blur_props = obs_properties_create();

	obs_properties_add_float_slider(focal_blur_props, "blur_focus_point", obs_module_text("BlurFocusPoint"), 0.0,
					1.0, 0.05);
	obs_properties_add_float_slider(focal_blur_props, "blur_focus_depth", obs_module_text("BlurFocusDepth"), 0.0,
					0.3, 0.02);

	obs_properties_add_group(props, "focal_blur_group", obs_module_text("FocalBlurGroup"), OBS_GROUP_NORMAL,
				 focal_blur_props);

	// Add a informative text about the plugin
	// replace the placeholder with the current version
	// use std::regex_replace instead of QString::arg because the latter doesn't work on Linux
	std::string basic_info = std::regex_replace(PLUGIN_INFO_TEMPLATE, std::regex("%1"), PLUGIN_VERSION);
	// Check for update
	if (const std::optional<std::string> latestVersion = UpdateConfig::getLatestVersion();
	    latestVersion && *latestVersion != PLUGIN_VERSION) {
		basic_info +=
			std::regex_replace(PLUGIN_INFO_TEMPLATE_UPDATE_AVAILABLE, std::regex("%1"), *latestVersion);
	}
	obs_properties_add_text(props, "info", basic_info.c_str(), OBS_TEXT_INFO);
	AboutDialogIntegration::addButton(props);

	UNUSED_PARAMETER(data);
	return propsOwner.release();
}

catch (const std::exception &error) {
	obs_log(LOG_ERROR, "Background filter properties: %s", error.what());
	return nullptr;
}

void background_filter_defaults(obs_data_t *settings)
{
#ifdef _WIN32
	obs_data_set_default_bool(settings, "gpu_image_processing", false);
#endif
	obs_data_set_default_bool(settings, "advanced", false);
	obs_data_set_default_bool(settings, "stop_when_source_is_inactive", true);
	obs_data_set_default_bool(settings, "enable_threshold", true);
	obs_data_set_default_double(settings, "threshold", 0.5);
	obs_data_set_default_double(settings, "contour_filter", 0.05);
	obs_data_set_default_double(settings, "smooth_contour", 0.5);
	obs_data_set_default_double(settings, "mask_expansion", 0);
	obs_data_set_default_double(settings, "feather", 0.0);
#if defined(__APPLE__)
	obs_data_set_default_string(settings, "useGPU", USEGPU_CPU);
#else
	// Linux
	obs_data_set_default_string(settings, "useGPU", USEGPU_CPU);
#endif
	obs_data_set_default_string(settings, "model_select", MODEL_MEDIAPIPE);
	obs_data_set_default_int(settings, "mask_every_x_frames", 1);
	obs_data_set_default_int(settings, "blur_background", 0);
	obs_data_set_default_int(settings, "numThreads", 1);
	obs_data_set_default_bool(settings, "enable_focal_blur", false);
	obs_data_set_default_double(settings, "temporal_smooth_factor", 0.85);
	obs_data_set_default_double(settings, "image_similarity_threshold", 35.0);
	obs_data_set_default_bool(settings, "enable_image_similarity", true);
	obs_data_set_default_double(settings, "blur_focus_point", 0.1);
	obs_data_set_default_double(settings, "blur_focus_depth", 0.0);
}

void background_filter_update(void *data, obs_data_t *settings)
try {
	obs_log(LOG_INFO, "Background filter updated");

	// Cast to shared_ptr pointer and create a local shared_ptr
	auto *ptr = static_cast<std::shared_ptr<background_removal_filter> *>(data);
	if (!ptr) {
		return;
	}

	std::shared_ptr<background_removal_filter> tf = *ptr;
	if (!tf) {
		return;
	}

#ifdef _WIN32
	std::lock_guard updateLock(tf->updateMutex);
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		if (tf->imageTerminal)
			return;
	}
#endif
	tf->isDisabled = true;

#ifdef _WIN32
	{
		std::lock_guard settingsLock(tf->modelMutex);
#endif
		tf->stopWhenSourceIsInactive = obs_data_get_bool(settings, "stop_when_source_is_inactive");
		tf->enableThreshold = (float)obs_data_get_bool(settings, "enable_threshold");
		tf->threshold = (float)obs_data_get_double(settings, "threshold");

		tf->contourFilter = (float)obs_data_get_double(settings, "contour_filter");
		tf->smoothContour = (float)obs_data_get_double(settings, "smooth_contour");
		tf->maskExpansion = (int)obs_data_get_double(settings, "mask_expansion");
		tf->feather = (float)obs_data_get_double(settings, "feather");
		tf->maskEveryXFrames = (int)obs_data_get_int(settings, "mask_every_x_frames");
		tf->maskEveryXFramesCount = (int)(0);
		tf->blurBackground = obs_data_get_int(settings, "blur_background");
		tf->enableFocalBlur = (float)obs_data_get_bool(settings, "enable_focal_blur");
		tf->blurFocusPoint = (float)obs_data_get_double(settings, "blur_focus_point");
		tf->blurFocusDepth = (float)obs_data_get_double(settings, "blur_focus_depth");
		tf->temporalSmoothFactor = (float)obs_data_get_double(settings, "temporal_smooth_factor");
		tf->imageSimilarityThreshold = (float)obs_data_get_double(settings, "image_similarity_threshold");
		tf->enableImageSimilarity = (float)obs_data_get_bool(settings, "enable_image_similarity");

#ifdef _WIN32
	}
#endif
	const std::string newUseGpu = obs_data_get_string(settings, "useGPU");
	const std::string newModel = obs_data_get_string(settings, "model_select");
	const uint32_t newNumThreads = (uint32_t)obs_data_get_int(settings, "numThreads");
#ifdef _WIN32
	bool sessionChanged = false;
	const auto settingsHash = image_settings_fingerprint(settings);
#endif

	{
		// Serialize settings comparisons, replacement and inference with the subclass mutex.
		std::unique_lock<std::mutex> lock(tf->modelMutex);
		if (!tf->session || tf->modelSelection.empty() || tf->modelSelection != newModel ||
		    tf->useGPU != newUseGpu || tf->numThreads != newNumThreads) {
#ifdef _WIN32
			{
				std::lock_guard stateLock(tf->imageStateMutex);
				auto initializing = tf->imagePipeline.snapshot();
				initializing.requested = obs_data_get_bool(settings, "gpu_image_processing");
				initializing.session_ready = false;
				initializing.effective_directml = false;
				tf->imagePipeline.configure(initializing);
				tf->imageEffectiveProvider = "pending";
				++tf->displayEpoch;
				tf->legacyFrame.reset();
				tf->legacyMask.reset();
				tf->refreshImageProperties = true;
				tf->imageTelemetry = {};
			}
#endif

			// Re-initialize model if it's not already the selected one or switching inference device
			tf->modelSelection = newModel;
			tf->useGPU = newUseGpu;
			tf->numThreads = newNumThreads;

			if (tf->modelSelection == MODEL_SINET) {
				tf->model.reset(new ModelSINET);
			}
			if (tf->modelSelection == MODEL_SELFIE) {
				tf->model.reset(new ModelSelfie);
			}
			if (tf->modelSelection == MODEL_SELFIE_MULTICLASS) {
				tf->model.reset(new ModelSelfieMulticlass);
			}
			if (tf->modelSelection == MODEL_MEDIAPIPE) {
				tf->model.reset(new ModelMediaPipe);
			}
			if (tf->modelSelection == MODEL_RVM) {
				tf->model.reset(new ModelRVM);
			}
			if (tf->modelSelection == MODEL_PPHUMANSEG) {
				tf->model.reset(new ModelPPHumanSeg);
			}
			if (tf->modelSelection == MODEL_DEPTH_TCMONODEPTH) {
				tf->model.reset(new ModelTCMonoDepth);
			}

			int ortSessionResult = createOrtSession(tf.get());
#ifdef _WIN32
			sessionChanged = true;
#endif
			if (ortSessionResult != OBS_BGREMOVAL_ORT_SESSION_SUCCESS) {
				obs_log(LOG_ERROR, "Failed to create ONNXRuntime session. Error code: %d",
					ortSessionResult);
				// disable filter
				tf->isDisabled = true;
				resetOrtSessionData(*tf);
				tf->model.reset();
#ifdef _WIN32
				{
					std::lock_guard stateLock(tf->imageStateMutex);
					auto config = tf->imagePipeline.snapshot();
					config.requested = obs_data_get_bool(settings, "gpu_image_processing");
					config.session_ready = false;
					config.effective_directml = false;
					tf->imagePipeline.configure(config);
					++tf->displayEpoch;
					tf->legacyFrame.reset();
					tf->legacyMask.reset();
				}
#endif
				lock.unlock();
#ifdef _WIN32
				obs_source_update_properties(tf->source);
#endif
				return;
			}
		}

#ifdef _WIN32
		{
			std::lock_guard stateLock(tf->imageStateMutex);
			auto config = tf->imagePipeline.snapshot();
			config.requested = obs_data_get_bool(settings, "gpu_image_processing");
			config.windows = true;
			config.mediapipe = tf->modelSelection == MODEL_MEDIAPIPE;
			config.session_ready = tf->session && tf->model &&
					       tf->sessionDiagnostics.outcome == windows_ml::SessionOutcome::Ready;
			config.effective_directml = config.session_ready &&
						    tf->sessionDiagnostics.effective_provider == "DmlExecutionProvider";
			if (config.mediapipe && !tf->inputDims.empty() && tf->inputDims[0].size() == 4)
				config.input = {static_cast<uint32_t>(tf->inputDims[0][2]),
						static_cast<uint32_t>(tf->inputDims[0][1])};
			else
				config.input = {};
			config.image_similarity = tf->enableImageSimilarity;
			config.similarity_threshold = tf->imageSimilarityThreshold;
			config.mask_every_x_frames = static_cast<uint32_t>(std::max(1, tf->maskEveryXFrames));
			config.mask = {tf->enableThreshold, tf->threshold, tf->temporalSmoothFactor, tf->contourFilter,
				       tf->smoothContour,   tf->feather,   tf->maskExpansion};
			tf->imageRenderSettings = {tf->blurBackground, tf->enableFocalBlur, tf->blurFocusPoint,
						   tf->blurFocusDepth};
			tf->imageEffectiveProvider = tf->sessionDiagnostics.effective_provider;
			const bool comparisonSettingsChanged = tf->imageSettingsHash != settingsHash;
			tf->imageSettingsHash = settingsHash;
			if (sessionChanged)
				tf->imageProcessingFailed = false;
			if (sessionChanged || comparisonSettingsChanged) {
				// Comparison settings and explicit initialization invalidate work even when tensor metadata is unchanged.
				auto pending = config;
				pending.session_ready = false;
				tf->imagePipeline.configure(pending);
			}
			const auto oldGeneration = tf->imagePipeline.snapshot().generation;
			const auto generation = tf->imagePipeline.configure(config);
			if (sessionChanged || generation != oldGeneration) {
				++tf->displayEpoch;
				tf->imageTelemetry = {};
				tf->legacyFrame.reset();
				tf->legacyMask.reset();
				tf->refreshImageProperties = true;
			}
			if (tf->imageProcessingFailed && gpu_image::evaluate_processing_request(config).eligible)
				tf->imagePipeline.set_processing_state(
					generation, gpu_image::ProcessingState::CpuProcessingFallback,
					"processing-failure-explicit-reinitialization-required");
		}
#endif
	}
#ifdef _WIN32
	if (sessionChanged) {
		obs_source_update_properties(tf->source);
	}
#endif

	obs_enter_graphics();
#ifdef _WIN32
	bool terminal;
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		terminal = tf->imageTerminal;
	}
	if (terminal) {
		obs_leave_graphics();
		return;
	}
#endif

	char *effect_path = obs_module_file(EFFECT_PATH);
	gs_effect_destroy(tf->effect);
	tf->effect = gs_effect_create_from_file(effect_path, NULL);
	bfree(effect_path);

	char *kawaseBlurEffectPath = obs_module_file(KAWASE_BLUR_EFFECT_PATH);
	gs_effect_destroy(tf->kawaseBlurEffect);
	tf->kawaseBlurEffect = gs_effect_create_from_file(kawaseBlurEffectPath, NULL);
	bfree(kawaseBlurEffectPath);

	obs_leave_graphics();

	// Log the currently selected options
	obs_log(LOG_INFO, "Background Removal Filter Options:");
	// name of the source that the filter is attached to
	obs_log(LOG_INFO, "  Source: %s", obs_source_get_name(tf->source));
	obs_log(LOG_INFO, "  Model: %s", tf->modelSelection.c_str());
	obs_log(LOG_INFO, "  Inference Device: %s", tf->useGPU.c_str());
	obs_log(LOG_INFO, "  Num Threads: %d", tf->numThreads);
	obs_log(LOG_INFO, "  Enable Threshold: %s", tf->enableThreshold ? "true" : "false");
	obs_log(LOG_INFO, "  Threshold: %f", tf->threshold);
	obs_log(LOG_INFO, "  Contour Filter: %f", tf->contourFilter);
	obs_log(LOG_INFO, "  Smooth Contour: %f", tf->smoothContour);
	obs_log(LOG_INFO, "  Mask Expansion: %f", tf->maskExpansion);
	obs_log(LOG_INFO, "  Feather: %f", tf->feather);
	obs_log(LOG_INFO, "  Mask Every X Frames: %d", tf->maskEveryXFrames);
	obs_log(LOG_INFO, "  Enable Image Similarity: %s", tf->enableImageSimilarity ? "true" : "false");
	obs_log(LOG_INFO, "  Image Similarity Threshold: %f", tf->imageSimilarityThreshold);
	obs_log(LOG_INFO, "  Blur Background: %d", tf->blurBackground);
	obs_log(LOG_INFO, "  Enable Focal Blur: %s", tf->enableFocalBlur ? "true" : "false");
	obs_log(LOG_INFO, "  Blur Focus Point: %f", tf->blurFocusPoint);
	obs_log(LOG_INFO, "  Blur Focus Depth: %f", tf->blurFocusDepth);
	obs_log(LOG_INFO, "  Disabled: %s", tf->isDisabled ? "true" : "false");
#ifdef _WIN32
	obs_log(LOG_INFO, "  Model file path: %S", tf->modelFilepath.c_str());
#else
	obs_log(LOG_INFO, "  Model file path: %s", tf->modelFilepath.c_str());
#endif

	// Enable only a completely initialized session.
	{
		std::unique_lock<std::mutex> lock(tf->modelMutex);
#ifdef _WIN32
		std::lock_guard stateLock(tf->imageStateMutex);
		tf->isDisabled = tf->imageTerminal || !tf->session || !tf->model;
#else
		tf->isDisabled = !tf->session || !tf->model;
#endif
	}
}

catch (const std::exception &error) {
	auto *ptr = static_cast<std::shared_ptr<background_removal_filter> *>(data);
	if (ptr && *ptr)
		(*ptr)->isDisabled = true;
	obs_log(LOG_ERROR, "Background filter update: %s", error.what());
}

void background_filter_activate(void *data)
{
	auto *ptr = static_cast<std::shared_ptr<background_removal_filter> *>(data);
	if (!ptr) {
		return;
	}

	std::shared_ptr<background_removal_filter> tf = *ptr;
	if (tf && tf->stopWhenSourceIsInactive) {
		std::unique_lock<std::mutex> lock(tf->modelMutex);
		obs_log(LOG_INFO, "Background filter activated");
#ifdef _WIN32
		std::lock_guard stateLock(tf->imageStateMutex);
		tf->isDisabled = tf->imageTerminal || !tf->session || !tf->model;
#else
		tf->isDisabled = !tf->session || !tf->model;
#endif
	}
}

void background_filter_deactivate(void *data)
{
	auto *ptr = static_cast<std::shared_ptr<background_removal_filter> *>(data);
	if (!ptr) {
		return;
	}

	std::shared_ptr<background_removal_filter> tf = *ptr;
	if (tf && tf->stopWhenSourceIsInactive) {
		obs_log(LOG_INFO, "Background filter deactivated");
		tf->isDisabled = true;
	}
}

/**                   FILTER CORE                     */

void *background_filter_create(obs_data_t *settings, obs_source_t *source)
{
	obs_log(LOG_INFO, "Background filter created");
	try {
		// Create the instance as a shared_ptr
		auto instance = std::make_shared<background_removal_filter>();

		instance->source = source;
		instance->texrender = gs_texrender_create(GS_BGRA, GS_ZS_NONE);

		std::string instanceName{"background-removal-inference"};
		instance->env.reset(new Ort::Env(OrtLoggingLevel::ORT_LOGGING_LEVEL_ERROR, instanceName.c_str()));

		instance->modelSelection = MODEL_MEDIAPIPE;

		// Create pointer to shared_ptr for the update call
		auto ptr = new std::shared_ptr<background_removal_filter>(instance);
		background_filter_update(ptr, settings);

		// Return the pointer to the shared_ptr
		// This keeps the reference count at least 1 until destroy is called
		return ptr;
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "Failed to create background filter: %s", e.what());
		return nullptr;
	}
}

void background_filter_destroy(void *data)
{
	obs_log(LOG_INFO, "Background filter destroyed");

	// Cast back to shared_ptr pointer
	auto *ptr = static_cast<std::shared_ptr<background_removal_filter> *>(data);
	if (ptr) {
		if (*ptr) {
			// Mark as disabled to prevent further processing
			(*ptr)->isDisabled = true;
#ifdef _WIN32
			{
				std::lock_guard stateLock((*ptr)->imageStateMutex);
				(*ptr)->imageTerminal = true;
				(*ptr)->imagePipeline.invalidate();
				(*ptr)->legacyFrame.reset();
				(*ptr)->legacyMask.reset();
				++(*ptr)->displayEpoch;
			}
#endif

			// Perform cleanup
			obs_enter_graphics();
#ifdef _WIN32
			(*ptr)->imagePreprocessor.release();
			(*ptr)->imageMaskProcessor.release();
			gs_texture_destroy((*ptr)->imageDisplayTexture);
			(*ptr)->imageDisplayTexture = nullptr;
			gs_texture_destroy((*ptr)->imageCandidateTexture);
			(*ptr)->imageCandidateTexture = nullptr;
			(*ptr)->imageDisplayValid = false;
#endif
			gs_texrender_destroy((*ptr)->texrender);
			if ((*ptr)->stagesurface) {
				gs_stagesurface_destroy((*ptr)->stagesurface);
			}
			gs_effect_destroy((*ptr)->effect);
			gs_effect_destroy((*ptr)->kawaseBlurEffect);
			obs_leave_graphics();
		}
		// Delete the pointer to shared_ptr
		// This decrements the ref count. If no other threads hold a shared_ptr, the instance is deleted
		delete ptr;
	}
}

#ifndef _WIN32
static void processImageForBackground(struct background_removal_filter *tf, const cv::Mat &imageBGRA,
				      cv::Mat &backgroundMask)
{
	cv::Mat outputImage;
	if (!runFilterModelInference(tf, imageBGRA, outputImage)) {
		return;
	}
	// Assume outputImage is now a single channel, uint8 image with values between 0 and 255

	// If we have a threshold, apply it. Otherwise, just use the output image as the mask
	if (tf->enableThreshold) {
		// We need to make tf->threshold (float [0,1]) be in that range
		const uint8_t threshold_value = (uint8_t)(tf->threshold * 255.0f);
		backgroundMask = outputImage < threshold_value;
	} else {
		backgroundMask = 255 - outputImage;
	}
}

#endif

#ifdef _WIN32
// Caller holds imageStateMutex; every read/modify/configure is one transaction.
static bool image_current(background_removal_filter &tf, const gpu_image::FrameStamp &stamp)
{
	const auto config = tf.imagePipeline.snapshot();
	return !tf.imageTerminal && !tf.isDisabled && stamp.generation == config.generation &&
	       stamp.source == config.source && stamp.input == config.input;
}
static void image_fail(background_removal_filter &tf, uint64_t generation, const char *reason)
{
	std::lock_guard lock(tf.imageStateMutex);
	if (tf.imageTerminal || tf.isDisabled || tf.imagePipeline.snapshot().generation != generation)
		return;
	if (tf.imagePipeline.set_processing_state(generation, gpu_image::ProcessingState::CpuProcessingFallback,
						  reason)) {
		tf.imageProcessingFailed = true;
		tf.imageTelemetry = {};
		tf.legacyFrame.reset();
		tf.legacyMask.reset();
		tf.refreshImageProperties = true;
		// Deliberately preserve only the independently owned, already accepted same-source display.
	}
}
static void image_log(background_removal_filter &tf, bool stats, const obs_video_info &video, bool counters)
{
	// Called from tick outside graphics/model. State is only held while copying a record/resetting counters.
	gpu_image::ProcessingSnapshot snapshot;
	gpu_image::PipelineConfig config;
	ImageTelemetry telemetry;
	std::string provider, fingerprint, sourceHash;
	ImageRenderSettings render;
	uint64_t frameId;
	{
		std::lock_guard lock(tf.imageStateMutex);
		if (tf.imageTerminal)
			return;
		snapshot = tf.imagePipeline.processing_snapshot();
		if (!stats && tf.imageLoggedGeneration == snapshot.generation && tf.imageLoggedState == snapshot.state)
			return;
		if (stats && ProcessingClock::now() - tf.imageTelemetry.since < std::chrono::seconds(5))
			return;
		tf.imageLoggedGeneration = snapshot.generation;
		tf.imageLoggedState = snapshot.state;
		config = tf.imagePipeline.snapshot();
		provider = tf.imageEffectiveProvider;
		fingerprint = tf.imageSettingsHash;
		sourceHash = tf.imageSourceHash;
		render = tf.imageRenderSettings;
		frameId = tf.imageFrameId;
		telemetry = tf.imageTelemetry;
		if (stats)
			tf.imageTelemetry = {};
	}
	std::string reason = snapshot.reason;
	for (auto &character : reason)
		if (character == ' ' || character == '\t' || character == '\n' || character == '\r')
			character = '-';
	fingerprint = image_hash_text(
		image_hash(sourceHash + ":" + std::to_string(video.fps_num) + ":" + std::to_string(video.fps_den),
			   image_hash(fingerprint)));
	// Structured, versioned collector API. Identifiers/settings are fingerprints, never raw camera IDs/JSON.
	obs_log(LOG_INFO,
		"GPUImageProcessing %s processing_version=1 filter_id=%llu settings_fingerprint=%s source_fingerprint=%s generation=%llu frame_id=%llu requested=%d state=%s effective_inference=%s preprocess_active=%d mask_active=%d similarity_full_readback=%d source_width=%u source_height=%u input_width=%u input_height=%u reason=%s fps_num=%u fps_den=%u enable_threshold=%d threshold=%.9g temporal_smooth_factor=%.9g contour_filter=%.9g smooth_contour=%.9g feather=%.9g mask_expansion=%d image_similarity=%d similarity_threshold=%.9g mask_every_x_frames=%u blur_background=%lld focal_blur=%d focus_point=%.9g focus_depth=%.9g processed=%llu skipped=%llu stale=%llu captured=%llu input_readback_pixels=%llu similarity_readback_pixels=%llu capture_host_elapsed_ms=%.6f inference_host_elapsed_ms=%.6f mask_host_elapsed_ms=%.6f interval_host_elapsed_ms=%.6f obs_counters_available=%d obs_rendered_frames=%u obs_lagged_frames=%u obs_average_frame_host_ns=%llu",
		stats ? "stats" : "state", static_cast<unsigned long long>(tf.imageFilterId), fingerprint.c_str(),
		sourceHash.c_str(), static_cast<unsigned long long>(snapshot.generation),
		static_cast<unsigned long long>(frameId), snapshot.requested, image_state_name(snapshot.state),
		provider.empty() ? "pending" : provider.c_str(), snapshot.preprocess_active, snapshot.mask_active,
		snapshot.similarity_full_readback, snapshot.source.width, snapshot.source.height, snapshot.input.width,
		snapshot.input.height, reason.empty() ? "none" : reason.c_str(), video.fps_num, video.fps_den,
		config.mask.enable_threshold, config.mask.threshold, config.mask.temporal_smooth_factor,
		config.mask.contour_filter, config.mask.smooth_contour, config.mask.feather, config.mask.mask_expansion,
		config.image_similarity, config.similarity_threshold, config.mask_every_x_frames,
		static_cast<long long>(render.blur), render.focal, render.focus_point, render.focus_depth,
		static_cast<unsigned long long>(telemetry.processed),
		static_cast<unsigned long long>(telemetry.skipped), static_cast<unsigned long long>(telemetry.stale),
		static_cast<unsigned long long>(telemetry.frames),
		static_cast<unsigned long long>(telemetry.input_pixels),
		static_cast<unsigned long long>(telemetry.similarity_pixels), telemetry.capture_ms,
		telemetry.inference_ms, telemetry.mask_ms, image_elapsed_ms(telemetry.since), counters,
		counters ? obs_get_total_frames() : 0, counters ? obs_get_lagged_frames() : 0,
		static_cast<unsigned long long>(counters ? obs_get_average_frame_time_ns() : 0));
}
static void image_tick(const std::shared_ptr<background_removal_filter> &tf)
{
	std::unique_ptr<obs_source_t, decltype(&obs_source_release)> sourceOwner(nullptr, &obs_source_release);
	bool refresh;
	{
		std::lock_guard lock(tf->imageStateMutex);
		if (tf->imageTerminal)
			return;
		sourceOwner.reset(obs_source_get_ref(tf->source));
		refresh = std::exchange(tf->refreshImageProperties, false);
	}
	if (!sourceOwner)
		return;
	// OBS invokes video_tick outside its graphics context. No model/state locks are held here.
	if (refresh)
		obs_source_update_properties(tf->source);
	obs_video_info video{};
	const bool counters = obs_get_video_info(&video);
	image_log(*tf, false, video, counters);
	image_log(*tf, true, video, counters);
	if (tf->isDisabled || !obs_source_enabled(tf->source))
		return;

	std::unique_lock modelLock(tf->modelMutex);
	if (tf->isDisabled || !tf->session || !tf->model)
		return;
	gpu_image::PipelineConfig config;
	std::optional<gpu_image::FramePacket> frame;
	cv::Mat history, similarityHistory;
	bool gpu;
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		if (tf->imageTerminal || tf->isDisabled)
			return;
		config = tf->imagePipeline.snapshot();
		gpu = tf->imagePipeline.processing_snapshot().preprocess_active;
		if (!gpu)
			frame = tf->legacyFrame; // immutable owned storage, shallow lifetime snapshot
	}
	if (gpu) {
		try {
			frame = tf->imagePipeline.latest_frame(); // full compatibility clone outside state gate
		} catch (const cv::Exception &) {
			image_fail(*tf, config.generation, "input-packet-read-failed");
			return;
		} catch (const std::bad_alloc &) {
			image_fail(*tf, config.generation, "input-packet-read-failed");
			return;
		}
	}
	if (!frame)
		return;
	const auto packet = *frame;
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		if (!image_current(*tf, packet.stamp) || packet.stamp.generation != config.generation ||
		    packet.stamp.frame_id <= tf->consumedFrame)
			return;
		tf->consumedFrame = packet.stamp.frame_id;
		if (tf->historyGeneration != config.generation) {
			tf->imageHistory.release();
			tf->imageSimilarityHistory.release();
			tf->imageMaskCounter = 0;
			tf->historyGeneration = config.generation;
		}
		history = tf->imageHistory;
		similarityHistory = tf->imageSimilarityHistory;
	}
	const cv::Mat &similarity = gpu ? packet.similarity_bgra : packet.input_bgra;
	const bool similaritySkip = config.image_similarity && !similarityHistory.empty() &&
				    similarityHistory.size() == similarity.size() &&
				    cv::PSNR(similarityHistory, similarity) > config.similarity_threshold;
	cv::Mat nextSimilarity;
	if (config.image_similarity && !similaritySkip)
		nextSimilarity = similarity.clone();
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		if (!image_current(*tf, packet.stamp)) {
			++tf->imageTelemetry.stale;
			return;
		}
		if (similaritySkip) {
			++tf->imageTelemetry.skipped;
			return;
		}
		if (config.image_similarity)
			tf->imageSimilarityHistory = std::move(nextSimilarity);
		tf->imageMaskCounter = (tf->imageMaskCounter + 1) % config.mask_every_x_frames;
		if (tf->imageMaskCounter != 0) {
			++tf->imageTelemetry.skipped;
			return;
		}
	}
	const auto inferenceStart = ProcessingClock::now();
	cv::Mat bytes;
	if (!runFilterModelInference(tf.get(), packet.input_bgra, bytes) || bytes.empty())
		return;
	const double inferenceMs = image_elapsed_ms(inferenceStart);
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		if (!image_current(*tf, packet.stamp)) {
			++tf->imageTelemetry.stale;
			return;
		}
	}
	const auto maskStart = ProcessingClock::now();
	gpu_image::SmallMaskPreparation prepared;
	try {
		prepared = gpu_image::prepare_small_mask(bytes, history, config.mask);
	} catch (const std::exception &) {
		if (gpu)
			image_fail(*tf, config.generation, "small-mask-preparation-failed");
		throw;
	}
	gpu_image::MaskPacket mask{
		packet.stamp,
		gpu ? prepared.mask : gpu_image::finish_mask_cpu(prepared.mask, config.source, config.mask), gpu};
	bool published = !gpu;
	if (gpu) {
		try {
			published = tf->imagePipeline.publish_mask(mask);
		} catch (const cv::Exception &) {
			image_fail(*tf, config.generation, "mask-publication-failed");
			return;
		} catch (const std::bad_alloc &) {
			image_fail(*tf, config.generation, "mask-publication-failed");
			return;
		}
	}
	std::lock_guard stateLock(tf->imageStateMutex);
	if (!image_current(*tf, packet.stamp) || !published) {
		++tf->imageTelemetry.stale;
		return;
	}
	if (!gpu) {
		tf->legacyMask = mask;
		// Retained legacy output remains available to existing filter observers.
		std::lock_guard outputLock(tf->outputLock);
		tf->backgroundMask = mask.mask;
	}
	// Commit uncontoured history only after publication has been accepted for this generation.
	tf->imageHistory = std::move(prepared.temporal_history);
	++tf->imageTelemetry.processed;
	tf->imageTelemetry.inference_ms += inferenceMs;
	tf->imageTelemetry.mask_ms += image_elapsed_ms(maskStart);
}
// Render full source without forcing a CPU readback; eligible preprocessing samples this texture.
static bool image_render_source(background_removal_filter &tf, uint32_t &width, uint32_t &height)
{
	auto *target = obs_filter_get_target(tf.source);
	if (!target || !obs_source_enabled(tf.source))
		return false;
	width = obs_source_get_base_width(target);
	height = obs_source_get_base_height(target);
	if (!width || !height)
		return false;
	gs_texrender_reset(tf.texrender);
	if (!gs_texrender_begin(tf.texrender, width, height))
		return false;
	vec4 clear;
	vec4_zero(&clear);
	gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
	gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
	obs_source_video_render(target);
	gs_blend_state_pop();
	gs_texrender_end(tf.texrender);
	return true;
}
static cv::Mat image_full_readback(background_removal_filter &tf, gpu_image::Dimensions source)
{
	if (tf.stagesurface && (gs_stagesurface_get_width(tf.stagesurface) != source.width ||
				gs_stagesurface_get_height(tf.stagesurface) != source.height)) {
		gs_stagesurface_destroy(tf.stagesurface);
		tf.stagesurface = nullptr;
	}
	if (!tf.stagesurface)
		tf.stagesurface = gs_stagesurface_create(source.width, source.height, GS_BGRA);
	if (!tf.stagesurface)
		return {};
	gs_stage_texture(tf.stagesurface, gs_texrender_get_texture(tf.texrender));
	uint8_t *data = nullptr;
	uint32_t pitch = 0;
	if (!gs_stagesurface_map(tf.stagesurface, &data, &pitch))
		return {};
	cv::Mat copy;
	try {
		if (data && uint64_t{pitch} >= uint64_t{source.width} * 4)
			copy = cv::Mat(static_cast<int>(source.height), static_cast<int>(source.width), CV_8UC4, data,
				       pitch)
				       .clone();
	} catch (...) {
		gs_stagesurface_unmap(tf.stagesurface);
		throw;
	}
	gs_stagesurface_unmap(tf.stagesurface);
	return copy;
}
static bool image_capture(const std::shared_ptr<background_removal_filter> &tf, uint32_t &width, uint32_t &height,
			  ImageRenderSettings &render)
{
	// Already in graphics. No model consultation, including when source dimensions change.
	if (!image_render_source(*tf, width, height))
		return false;
	auto *target = obs_filter_get_target(tf->source);
	obs_data_t *sourceSettings = obs_source_get_settings(target);
	const auto sourceHash = image_hash_text(
		image_hash(obs_data_get_json_with_defaults(sourceSettings), image_hash(obs_source_get_uuid(target))));
	obs_data_release(sourceSettings);
	obs_video_info video{};
	obs_get_video_info(&video);
	gpu_image::PipelineConfig config;
	gpu_image::FrameStamp stamp;
	bool gpu;
	uint64_t epoch;
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		if (tf->imageTerminal || tf->isDisabled)
			return false;
		config = tf->imagePipeline.snapshot();
		const auto oldGeneration = config.generation;
		config.source = {width, height};
		// Source/FPS changes are authoritative even if the source size is unchanged.
		if ((!tf->imageSourceHash.empty() && tf->imageSourceHash != sourceHash) ||
		    (tf->imageFpsNum && (tf->imageFpsNum != video.fps_num || tf->imageFpsDen != video.fps_den))) {
			auto pending = config;
			pending.session_ready = false;
			tf->imagePipeline.configure(pending);
		}
		tf->imagePipeline.configure(config);
		config = tf->imagePipeline.snapshot();
		if (oldGeneration != config.generation) {
			++tf->displayEpoch;
			tf->imageTelemetry = {};
			tf->legacyFrame.reset();
			tf->legacyMask.reset();
			tf->refreshImageProperties = true;
		}
		tf->imageSourceHash = sourceHash;
		tf->imageFpsNum = video.fps_num;
		tf->imageFpsDen = video.fps_den;
		if (tf->imageProcessingFailed && gpu_image::evaluate_processing_request(config).eligible &&
		    tf->imagePipeline.processing_snapshot().state !=
			    gpu_image::ProcessingState::CpuProcessingFallback) {
			tf->imagePipeline.set_processing_state(config.generation,
							       gpu_image::ProcessingState::CpuProcessingFallback,
							       "processing-failure-explicit-reinitialization-required");
			config = tf->imagePipeline.snapshot();
		}
		gpu = gpu_image::evaluate_processing_request(config).eligible && !tf->imageProcessingFailed;
		stamp = {config.generation, ++tf->imageFrameId, config.source, config.input};
		render = tf->imageRenderSettings;
		epoch = tf->displayEpoch;
	}
	if (tf->imageDisplayEpoch != epoch) {
		tf->imageDisplayValid = false;
		tf->imageDisplayFrame = 0;
		tf->imageDisplayEpoch = epoch;
	}
	const auto start = ProcessingClock::now();
	if (gpu) {
		char *inputPath = obs_module_file("effects/input_downscale.effect");
		const bool ready = tf->imagePreprocessor.prepare(config, inputPath);
		bfree(inputPath);
		if (!ready) {
			image_fail(*tf, config.generation, "input-preparation-failed");
			return true;
		}
		auto frame = tf->imagePreprocessor.capture(gs_texrender_get_texture(tf->texrender), stamp,
							   config.image_similarity);
		if (!frame) {
			image_fail(*tf, config.generation, "input-readback-failed");
			return true;
		}
		bool published;
		try {
			published = tf->imagePipeline.publish_frame(*frame);
		} catch (const cv::Exception &) {
			image_fail(*tf, config.generation, "input-publication-failed");
			return true;
		} catch (const std::bad_alloc &) {
			image_fail(*tf, config.generation, "input-publication-failed");
			return true;
		}
		std::lock_guard stateLock(tf->imageStateMutex);
		if (!image_current(*tf, stamp) || !published) {
			++tf->imageTelemetry.stale;
			return true;
		}
		if (tf->imagePipeline.processing_snapshot().state == gpu_image::ProcessingState::Pending) {
			tf->imagePipeline.set_processing_state(
				config.generation, gpu_image::ProcessingState::PreprocessOnly, "mask-pending");
			tf->refreshImageProperties = true;
		}
		tf->imageTelemetry.input_pixels += uint64_t{config.input.width} * config.input.height;
		if (config.image_similarity)
			tf->imageTelemetry.similarity_pixels += uint64_t{width} * height;
		++tf->imageTelemetry.frames;
		tf->imageTelemetry.capture_ms += image_elapsed_ms(start);
	} else {
		auto pixels = image_full_readback(*tf, config.source);
		if (pixels.empty())
			return false;
		std::lock_guard stateLock(tf->imageStateMutex);
		if (!image_current(*tf, stamp)) {
			++tf->imageTelemetry.stale;
			return true;
		}
		tf->legacyFrame = gpu_image::FramePacket{stamp, pixels, {}};
		{
			std::lock_guard inputLock(tf->inputBGRALock);
			tf->inputBGRA = pixels;
		}
		tf->imageTelemetry.input_pixels += uint64_t{width} * height;
		++tf->imageTelemetry.frames;
		tf->imageTelemetry.capture_ms += image_elapsed_ms(start);
	}
	return true;
}
static gs_texture_t *image_cached_display(background_removal_filter &tf, uint64_t epoch)
{
	std::lock_guard stateLock(tf.imageStateMutex);
	if (tf.imageTerminal || tf.isDisabled || tf.displayEpoch != epoch || tf.imageDisplayEpoch != epoch ||
	    !tf.imageDisplayValid)
		return nullptr;
	return tf.imageDisplayTexture;
}
// Graphics ownership only. Accepted pixels stay independent until a completed candidate is committed.
static bool image_prepare_candidate(background_removal_filter &tf, uint32_t width, uint32_t height,
				    gs_color_format format)
{
	auto *&candidate = tf.imageCandidateTexture;
	if (candidate && (gs_texture_get_width(candidate) != width || gs_texture_get_height(candidate) != height ||
			  gs_texture_get_color_format(candidate) != format)) {
		gs_texture_destroy(candidate);
		candidate = nullptr;
	}
	if (!candidate)
		candidate = gs_texture_create(width, height, format, 1, nullptr, format == GS_R8 ? GS_DYNAMIC : 0);
	return candidate != nullptr;
}
static bool image_upload_candidate(background_removal_filter &tf, const cv::Mat &pixels)
{
	uint8_t *mapped = nullptr;
	uint32_t pitch = 0;
	if (!gs_texture_map(tf.imageCandidateTexture, &mapped, &pitch))
		return false;
	if (!mapped || pitch < static_cast<uint32_t>(pixels.cols)) {
		gs_texture_unmap(tf.imageCandidateTexture);
		return false;
	}
	for (int row = 0; row < pixels.rows; ++row)
		memcpy(mapped + size_t{pitch} * row, pixels.ptr(row), static_cast<size_t>(pixels.cols));
	gs_texture_unmap(tf.imageCandidateTexture);
	return true;
}
static gs_texture_t *image_display(const std::shared_ptr<background_removal_filter> &tf)
{
	gpu_image::PipelineConfig config;
	std::optional<gpu_image::MaskPacket> mask;
	uint64_t epoch;
	bool gpu;
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		if (tf->imageTerminal || tf->isDisabled)
			return nullptr;
		config = tf->imagePipeline.snapshot();
		epoch = tf->displayEpoch;
		gpu = tf->imagePipeline.processing_snapshot().preprocess_active;
		if (!gpu)
			mask = tf->legacyMask;
	}
	if (gpu) {
		try {
			mask = tf->imagePipeline.latest_mask();
		} catch (const cv::Exception &) {
			image_fail(*tf, config.generation, "mask-packet-read-failed");
			return image_cached_display(*tf, epoch);
		} catch (const std::bad_alloc &) {
			image_fail(*tf, config.generation, "mask-packet-read-failed");
			return image_cached_display(*tf, epoch);
		}
	}
	{
		std::lock_guard stateLock(tf->imageStateMutex);
		if (mask && (!image_current(*tf, mask->stamp) || mask->stamp.generation != config.generation))
			mask.reset();
	}
	if (tf->imageDisplayEpoch != epoch) {
		tf->imageDisplayValid = false;
		tf->imageDisplayEpoch = epoch;
		tf->imageDisplayFrame = 0;
	}
	if (mask && mask->stamp.frame_id > tf->imageDisplayFrame) {
		gs_texture_t *texture = nullptr;
		const auto start = ProcessingClock::now();
		if (mask->gpu_postprocess) {
			char *path = obs_module_file("effects/gpu_mask_processing.effect");
			const bool ready = tf->imageMaskProcessor.prepare(config.input, config.source, path);
			bfree(path);
			if (ready)
				texture = tf->imageMaskProcessor.process(*mask, config.mask);
			if (!texture) {
				image_fail(*tf, config.generation, "mask-processing-failed");
				return image_cached_display(*tf, epoch);
			}
		}
		const auto candidateWidth = texture ? gs_texture_get_width(texture)
						    : static_cast<uint32_t>(mask->mask.cols);
		const auto candidateHeight = texture ? gs_texture_get_height(texture)
						     : static_cast<uint32_t>(mask->mask.rows);
		const auto format = texture ? gs_texture_get_color_format(texture) : GS_R8;
		if (!image_prepare_candidate(*tf, candidateWidth, candidateHeight, format)) {
			if (mask->gpu_postprocess)
				image_fail(*tf, config.generation, "display-copy-allocation-failed");
			return image_cached_display(*tf, epoch);
		}
		if (texture)
			gs_copy_texture(tf->imageCandidateTexture, texture);
		else if (!image_upload_candidate(*tf, mask->mask))
			return image_cached_display(*tf, epoch);
		{
			std::lock_guard stateLock(tf->imageStateMutex);
			if (image_current(*tf, mask->stamp) && epoch == tf->displayEpoch) {
				std::swap(tf->imageDisplayTexture, tf->imageCandidateTexture);
				tf->imageDisplayValid = true;
				tf->imageDisplayEpoch = epoch;
				tf->imageDisplayFrame = mask->stamp.frame_id;
				if (mask->gpu_postprocess) {
					const auto before = tf->imagePipeline.processing_snapshot();
					if ((before.state != gpu_image::ProcessingState::Active ||
					     before.reason != "none") &&
					    tf->imagePipeline.set_processing_state(
						    config.generation, gpu_image::ProcessingState::Active, "none"))
						tf->refreshImageProperties = true;
				}
				tf->imageTelemetry.mask_ms += image_elapsed_ms(start);
			} else {
				++tf->imageTelemetry.stale;
			}
		}
	}
	// Initialize a new epoch without showing old-source pixels; reuse storage of the same size/format.
	if (!tf->imageDisplayValid) {
		if (!image_prepare_candidate(*tf, config.source.width, config.source.height, GS_R8))
			return nullptr;
		uint8_t *pixels = nullptr;
		uint32_t pitch = 0;
		if (!gs_texture_map(tf->imageCandidateTexture, &pixels, &pitch))
			return nullptr;
		const bool valid = pixels && pitch >= config.source.width;
		if (valid)
			for (uint32_t row = 0; row < config.source.height; ++row)
				memset(pixels + size_t{pitch} * row, 255, config.source.width);
		gs_texture_unmap(tf->imageCandidateTexture);
		if (!valid)
			return nullptr;
		std::lock_guard stateLock(tf->imageStateMutex);
		if (!tf->imageTerminal && !tf->isDisabled && tf->displayEpoch == epoch &&
		    tf->imagePipeline.snapshot().generation == config.generation) {
			std::swap(tf->imageDisplayTexture, tf->imageCandidateTexture);
			tf->imageDisplayEpoch = epoch;
			tf->imageDisplayValid = true;
		}
	}
	return image_cached_display(*tf, epoch);
}
#endif

void background_filter_video_tick(void *data, float seconds)
{
	UNUSED_PARAMETER(seconds);

	// Cast to shared_ptr pointer and create a local shared_ptr
	auto *ptr = static_cast<std::shared_ptr<background_removal_filter> *>(data);
	if (!ptr) {
		return;
	}

	// Create a local shared_ptr
	// This guarantees the object stays alive for the duration of this function scope
	// even if filter_destroy is called on the main thread
	std::shared_ptr<background_removal_filter> tf = *ptr;

#ifdef _WIN32
	if (tf) {
		try {
			image_tick(tf);
		} catch (const std::exception &error) {
			obs_log(LOG_ERROR, "GPU image tick: %s", error.what());
		}
	}
	return;
#else
	if (!tf || tf->isDisabled) {
		return;
	}

	if (!obs_source_enabled(tf->source)) {
		return;
	}

	cv::Mat imageBGRA;
	{
		std::unique_lock<std::mutex> lock(tf->inputBGRALock, std::try_to_lock);
		if (!lock.owns_lock()) {
			// No data to process
			return;
		}
		if (tf->inputBGRA.empty()) {
			// No data to process
			return;
		}
		imageBGRA = tf->inputBGRA.clone();
	}

	if (tf->enableImageSimilarity) {
		if (!tf->lastImageBGRA.empty() && !imageBGRA.empty() && tf->lastImageBGRA.size() == imageBGRA.size()) {
			// calculate PSNR
			double psnr = cv::PSNR(tf->lastImageBGRA, imageBGRA);

			if (psnr > tf->imageSimilarityThreshold) {
				// The image is almost the same as the previous one. Skip processing.
				return;
			}
		}
		tf->lastImageBGRA = imageBGRA.clone();
	}

	if (tf->backgroundMask.empty()) {
		// First frame. Initialize the background mask.
		tf->backgroundMask = cv::Mat(imageBGRA.size(), CV_8UC1, cv::Scalar(255));
	}

	tf->maskEveryXFramesCount++;
	tf->maskEveryXFramesCount %= tf->maskEveryXFrames;

	try {
		if (tf->maskEveryXFramesCount != 0 && !tf->backgroundMask.empty()) {
			// We are skipping processing of the mask for this frame.
			// Get the background mask previously generated.
			; // Do nothing
		} else {
			cv::Mat backgroundMask;

			{
				std::unique_lock<std::mutex> lock(tf->modelMutex);
				// Recheck after waiting for initialization, which may have failed.
				if (tf->isDisabled || !tf->session || !tf->model) {
					return;
				}
				// Process the image to find the mask.
				processImageForBackground(tf.get(), imageBGRA, backgroundMask);
			}

			if (backgroundMask.empty()) {
				// Something went wrong. Just use the previous mask.
				obs_log(LOG_WARNING,
					"Background mask is empty. This shouldn't happen. Using previous mask.");
				return;
			}

			// Temporal smoothing
			if (tf->temporalSmoothFactor > 0.0 && tf->temporalSmoothFactor < 1.0 &&
			    !tf->lastBackgroundMask.empty() && tf->lastBackgroundMask.size() == backgroundMask.size()) {

				float temporalSmoothFactor = tf->temporalSmoothFactor;
				if (tf->enableThreshold) {
					// The temporal smooth factor can't be smaller than the threshold
					temporalSmoothFactor = std::max(temporalSmoothFactor, tf->threshold);
				}

				cv::addWeighted(backgroundMask, temporalSmoothFactor, tf->lastBackgroundMask,
						1.0 - temporalSmoothFactor, 0.0, backgroundMask);
			}

			tf->lastBackgroundMask = backgroundMask.clone();

			// Contour processing
			// Only applicable if we are thresholding (and get a binary image)
			if (tf->enableThreshold) {
				if (tf->contourFilter > 0.0 && tf->contourFilter < 1.0) {
					std::vector<std::vector<cv::Point>> contours;
					findContours(backgroundMask, contours, cv::RETR_EXTERNAL,
						     cv::CHAIN_APPROX_SIMPLE);
					std::vector<std::vector<cv::Point>> filteredContours;
					const double contourSizeThreshold =
						(double)(backgroundMask.total()) * tf->contourFilter;
					for (auto &contour : contours) {
						if (cv::contourArea(contour) > (double)contourSizeThreshold) {
							filteredContours.push_back(contour);
						}
					}
					backgroundMask.setTo(0);
					drawContours(backgroundMask, filteredContours, -1, cv::Scalar(255), -1);
				}

				if (tf->smoothContour > 0.0) {
					int k_size = (int)(3 + 11 * tf->smoothContour);
					k_size += k_size % 2 == 0 ? 1 : 0;
					cv::stackBlur(backgroundMask, backgroundMask, cv::Size(k_size, k_size));
				}

				// Resize the size of the mask back to the size of the original input.
				cv::resize(backgroundMask, backgroundMask, imageBGRA.size());

				// Additional contour processing at full resolution
				if (tf->smoothContour > 0.0) {
					// If the mask was smoothed, apply a threshold to get a binary mask
					backgroundMask = backgroundMask > 128;
				}

				// Expand or shrink the mask
				if (tf->maskExpansion > 0.0) {
					cv::erode(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1),
						  tf->maskExpansion);
				} else if (tf->maskExpansion < 0.0) {
					cv::dilate(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1),
						   -tf->maskExpansion);
				}

				if (tf->feather > 0.0) {
					// Feather (blur) the mask
					int k_size = (int)(40 * tf->feather);
					k_size += k_size % 2 == 0 ? 1 : 0;
					cv::dilate(backgroundMask, backgroundMask, cv::Mat(), cv::Point(-1, -1),
						   k_size / 3);
					cv::boxFilter(backgroundMask, backgroundMask, tf->backgroundMask.depth(),
						      cv::Size(k_size, k_size));
				}
			}

			// Save the mask for the next frame
			{
				std::lock_guard<std::mutex> lock(tf->outputLock);
				backgroundMask.copyTo(tf->backgroundMask);
			}
		}
	} catch (const Ort::Exception &e) {
		obs_log(LOG_ERROR, "ONNXRuntime Exception: %s", e.what());
		// TODO: Fall back to CPU if it makes sense
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "%s", e.what());
	}
#endif
}

static gs_texture_t *blur_background(std::shared_ptr<background_removal_filter> tf, uint32_t width, uint32_t height,
				     gs_texture_t *alphaTexture
#ifdef _WIN32
				     ,
				     const ImageRenderSettings &render
#endif
)
{
#ifdef _WIN32
	const auto blur = render.blur;
	const auto focal = render.focal;
	const auto point = render.focus_point;
	const auto depth = render.focus_depth;
#else
	const auto blur = tf->blurBackground;
	const auto focal = tf->enableFocalBlur;
	const auto point = tf->blurFocusPoint;
	const auto depth = tf->blurFocusDepth;
#endif
	if (blur == 0 || !tf->kawaseBlurEffect) {
		return nullptr;
	}
	gs_texture_t *blurredTexture = gs_texture_create(width, height, GS_BGRA, 1, nullptr, 0);
#ifdef _WIN32
	if (!blurredTexture)
		return nullptr;
#endif
	gs_copy_texture(blurredTexture, gs_texrender_get_texture(tf->texrender));
	gs_eparam_t *image = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "image");
	gs_eparam_t *focalmask = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "focalmask");
	gs_eparam_t *xOffset = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "xOffset");
	gs_eparam_t *yOffset = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "yOffset");
	gs_eparam_t *blurIter = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "blurIter");
	gs_eparam_t *blurTotal = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "blurTotal");
	gs_eparam_t *blurFocusPointParam = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "blurFocusPoint");
	gs_eparam_t *blurFocusDepthParam = gs_effect_get_param_by_name(tf->kawaseBlurEffect, "blurFocusDepth");

	for (int i = 0; i < (int)blur; i++) {
		gs_texrender_reset(tf->texrender);
		if (!gs_texrender_begin(tf->texrender, width, height)) {
			obs_log(LOG_INFO, "Could not open background blur texrender!");
			return blurredTexture;
		}

		gs_effect_set_texture(image, blurredTexture);
		gs_effect_set_texture(focalmask, alphaTexture);
		gs_effect_set_float(xOffset, ((float)i + 0.5f) / (float)width);
		gs_effect_set_float(yOffset, ((float)i + 0.5f) / (float)height);
		gs_effect_set_int(blurIter, i);
		gs_effect_set_int(blurTotal, (int)blur);
		gs_effect_set_float(blurFocusPointParam, point);
		gs_effect_set_float(blurFocusDepthParam, depth);

		struct vec4 background;
		vec4_zero(&background);
		gs_clear(GS_CLEAR_COLOR, &background, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

		const char *blur_type = (focal) ? "DrawFocalBlur" : "Draw";

		while (gs_effect_loop(tf->kawaseBlurEffect, blur_type)) {
			gs_draw_sprite(blurredTexture, 0, width, height);
		}
		gs_blend_state_pop();
		gs_texrender_end(tf->texrender);
		gs_copy_texture(blurredTexture, gs_texrender_get_texture(tf->texrender));
	}
	return blurredTexture;
}

void background_filter_video_render(void *data, gs_effect_t *_effect)
{
	UNUSED_PARAMETER(_effect);

	// Cast to shared_ptr pointer and create a local shared_ptr
	auto *ptr = static_cast<std::shared_ptr<background_removal_filter> *>(data);
	if (!ptr) {
		return;
	}

	// Create a local shared_ptr
	std::shared_ptr<background_removal_filter> tf = *ptr;

#ifdef _WIN32
	std::unique_ptr<obs_source_t, decltype(&obs_source_release)> sourceOwner(nullptr, &obs_source_release);
	if (tf) {
		std::lock_guard stateLock(tf->imageStateMutex);
		if (tf->imageTerminal)
			return;
		sourceOwner.reset(obs_source_get_ref(tf->source));
	}
	if (!sourceOwner)
		return;
#endif

	if (!tf || tf->isDisabled) {
#ifdef _WIN32
		if (tf) {
			std::lock_guard stateLock(tf->imageStateMutex);
			if (tf->imageTerminal)
				return;
		}
#endif
		if (tf && tf->source)
			obs_source_skip_video_filter(tf->source);
		return;
	}

	uint32_t width, height;
#ifdef _WIN32
	ImageRenderSettings render;
	try {
		if (!image_capture(tf, width, height, render)) {
			obs_source_skip_video_filter(tf->source);
			return;
		}
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "GPU image capture: %s", error.what());
		obs_source_skip_video_filter(tf->source);
		return;
	}
#else
	if (!getRGBAFromStageSurface(tf.get(), width, height)) {
		if (tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
		return;
	}

#endif
	if (!tf->effect) {
		// Effect failed to load, skip rendering
		if (tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
		return;
	}

	gs_texture_t *alphaTexture = nullptr;
#ifdef _WIN32
	try {
		alphaTexture = image_display(tf);
	} catch (const std::exception &error) {
		obs_log(LOG_ERROR, "GPU image mask: %s", error.what());
	}
	if (!alphaTexture) {
		obs_source_skip_video_filter(tf->source);
		return;
	}
#else
	{
		std::lock_guard<std::mutex> lock(tf->outputLock);

		if (tf->backgroundMask.empty()) {
			obs_log(LOG_WARNING, "Background mask is empty during render, skipping frame.");
			if (tf->source) {
				obs_source_skip_video_filter(tf->source);
			}
			return;
		}

		alphaTexture = gs_texture_create(tf->backgroundMask.cols, tf->backgroundMask.rows, GS_R8, 1,
						 (const uint8_t **)&tf->backgroundMask.data, 0);

		if (!alphaTexture) {
			obs_log(LOG_ERROR, "Failed to create alpha texture");
			if (tf->source) {
				obs_source_skip_video_filter(tf->source);
			}
			return;
		}
	}

#endif
	// Output the masked image
	gs_texture_t *blurredTexture = blur_background(tf, width, height, alphaTexture
#ifdef _WIN32
						       ,
						       render
#endif
	);

	if (!obs_source_process_filter_begin(tf->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)) {
		if (tf->source) {
			obs_source_skip_video_filter(tf->source);
		}
#ifndef _WIN32
		gs_texture_destroy(alphaTexture);
#endif
		gs_texture_destroy(blurredTexture);
		return;
	}

	gs_eparam_t *alphamask = gs_effect_get_param_by_name(tf->effect, "alphamask");
	gs_eparam_t *blurredBackground = gs_effect_get_param_by_name(tf->effect, "blurredBackground");

	gs_effect_set_texture(alphamask, alphaTexture);

	if (
#ifdef _WIN32
		render.blur > 0
#else
		tf->blurBackground > 0
#endif
	) {
		gs_effect_set_texture(blurredBackground, blurredTexture);
	}

	gs_blend_state_push();
	gs_reset_blend_state();

	const char *techName;
	if (
#ifdef _WIN32
		render.blur > 0
#else
		tf->blurBackground > 0
#endif
	) {
		if (
#ifdef _WIN32
			render.focal
#else
			tf->enableFocalBlur
#endif
		)
			techName = "DrawWithFocalBlur";
		else
			techName = "DrawWithBlur";
	} else {
		techName = "DrawWithoutBlur";
	}

	obs_source_process_filter_tech_end(tf->source, tf->effect, 0, 0, techName);

	gs_blend_state_pop();

#ifndef _WIN32
	gs_texture_destroy(alphaTexture);
#endif
	gs_texture_destroy(blurredTexture);
}

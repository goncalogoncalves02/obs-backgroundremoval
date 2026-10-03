// SPDX-FileCopyrightText: 2021-2026 Roy Shilkrot <roy.shil@gmail.com>
// SPDX-FileCopyrightText: 2023-2026 Kaito Udagawa <umireon@kaito.tokyo>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#if __has_include(<onnxruntime/onnxruntime_cxx_api.h>)
#include <onnxruntime/onnxruntime_cxx_api.h>
#elif __has_include(<onnxruntime_cxx_api.h>)
#include <onnxruntime_cxx_api.h>
#else
#error "onnxruntime_cxx_api.h was not found"
#endif

#if __has_include(<onnxruntime/cpu_provider_factory.h>)
#include <onnxruntime/cpu_provider_factory.h>
#elif __has_include(<cpu_provider_factory.h>)
#include <cpu_provider_factory.h>
#endif

#ifdef HAVE_ONNXRUNTIME_CUDA_EP
#if __has_include(<onnxruntime/cuda_provider_factory.h>)
#include <onnxruntime/cuda_provider_factory.h>
#elif __has_include(<cuda_provider_factory.h>)
#include <cuda_provider_factory.h>
#elif __has_include(<onnxruntime/core/providers/cuda/cuda_provider_factory.h>)
#include <onnxruntime/core/providers/cuda/cuda_provider_factory.h>
#endif
#endif // HAVE_ONNXRUNTIME_CUDA_EP

#ifdef HAVE_ONNXRUNTIME_ROCM_EP
#if __has_include(<onnxruntime/rocm_provider_factory.h>)
#include <onnxruntime/rocm_provider_factory.h>
#elif __has_include(<rocm_provider_factory.h>)
#include <rocm_provider_factory.h>
#elif __has_include(<onnxruntime/core/providers/rocm/rocm_provider_factory.h>)
#include <onnxruntime/core/providers/rocm/rocm_provider_factory.h>
#endif
#endif // HAVE_ONNXRUNTIME_ROCM_EP

#if defined(__APPLE__)
#if __has_include(<onnxruntime/coreml_provider_factory.h>)
#include <onnxruntime/coreml_provider_factory.h>
#elif __has_include(<coreml_provider_factory.h>)
#include <coreml_provider_factory.h>
#else
#error "coreml_provider_factory.h was not found"
#endif
#endif // __APPLE__

#ifdef _WIN32
#include "windows-ml-session.hpp"
#include "windows-ml-provider-policy.hpp"
#include <limits>
#include <cstdio>
#endif // _WIN32

#include <obs-module.h>

#include "ort-session-utils.hpp"
#include "../consts.h"
#include "../plugin-support.h"

void resetOrtSessionData(ORTModelData &data) noexcept
{
	data.inputTensor.clear();
	data.outputTensor.clear();
	data.inputNames.clear();
	data.outputNames.clear();
	data.inputDims.clear();
	data.outputDims.clear();
	data.session.reset();
	data.inputTensorValues.clear();
	data.outputTensorValues.clear();
}

#ifdef _WIN32
namespace {
const char *present(const std::string &value) noexcept
{
	return value.empty() ? "none" : value.c_str();
}
const char *outcomeName(windows_ml::SessionOutcome outcome) noexcept
{
	switch (outcome) {
	case windows_ml::SessionOutcome::NotInitialized:
		return "NotInitialized";
	case windows_ml::SessionOutcome::Constructed:
		return "Constructed";
	case windows_ml::SessionOutcome::Ready:
		return "Ready";
	case windows_ml::SessionOutcome::Failed:
		return "Failed";
	}
	return "Unknown";
}
const char *stageName(windows_ml::ProviderFailureStage stage) noexcept
{
	switch (stage) {
	case windows_ml::ProviderFailureStage::None:
		return "None";
	case windows_ml::ProviderFailureStage::Discovery:
		return "Discovery";
	case windows_ml::ProviderFailureStage::Activation:
		return "Activation";
	case windows_ml::ProviderFailureStage::Registration:
		return "Registration";
	case windows_ml::ProviderFailureStage::DeviceSelection:
		return "DeviceSelection";
	case windows_ml::ProviderFailureStage::Attachment:
		return "Attachment";
	}
	return "Unknown";
}
bool validMetadata(const std::vector<Ort::AllocatedStringPtr> &names,
		   const std::vector<std::vector<int64_t>> &dimensions)
{
	if (names.empty() || names.size() != dimensions.size()) {
		return false;
	}
	for (size_t i = 0; i < names.size(); ++i) {
		if (!names[i] || !*names[i] || dimensions[i].empty()) {
			return false;
		}
		int64_t elements = 1;
		for (const auto dimension : dimensions[i]) {
			if (dimension <= 0 || elements > std::numeric_limits<int64_t>::max() / dimension) {
				return false;
			}
			elements *= dimension;
		}
		if (static_cast<uint64_t>(elements) > std::numeric_limits<size_t>::max() / sizeof(float)) {
			return false;
		}
	}
	return true;
}
bool validBuffers(const std::vector<std::vector<int64_t>> &dimensions, const std::vector<std::vector<float>> &values,
		  const std::vector<Ort::Value> &tensors)
{
	if (dimensions.size() != values.size() || dimensions.size() != tensors.size()) {
		return false;
	}
	for (size_t i = 0; i < dimensions.size(); ++i) {
		// Metadata was checked before allocation; its product cannot overflow.
		if (!tensors[i] || !tensors[i].IsTensor() ||
		    values[i].size() != static_cast<size_t>(vectorProduct(dimensions[i]))) {
			return false;
		}
	}
	return true;
}
// Retain a GPU constructor error as well as the later adapter error.
int failWindowsMlInitialization(filter_data &data, int code, std::string_view error) noexcept
{
	resetOrtSessionData(data);
	auto &diagnostics = data.sessionDiagnostics;
	diagnostics.outcome = windows_ml::SessionOutcome::Failed;
	diagnostics.effective_provider.clear();
	windows_ml::assign_sanitized_diagnostic(
		diagnostics.error, diagnostics.error,
		diagnostics.error.empty() || error.empty() ? "" : "; model initialization: ", error);
	return code;
}
void beginWindowsMlInitialization(filter_data &data) noexcept
{
	resetOrtSessionData(data);
	data.modelFilepath.clear();
	data.sessionDiagnostics = {};
	data.sessionDiagnostics.outcome = windows_ml::SessionOutcome::Failed;
	windows_ml::assign_sanitized_diagnostic(data.sessionDiagnostics.requested_provider, data.useGPU);
}
int initializeWindowsMlSession(filter_data &data, const std::filesystem::path &modelPath)
{
	if (!data.model || !data.env) {
		return failWindowsMlInitialization(data, OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_MODEL,
						   "model or environment is not initialized");
	}
	if (data.numThreads > static_cast<uint32_t>(std::numeric_limits<int>::max())) {
		return failWindowsMlInitialization(data, OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP,
						   "CPU thread count is not representable");
	}
	std::error_code pathError;
	if (modelPath.empty() || !std::filesystem::is_regular_file(modelPath, pathError) || pathError) {
		return failWindowsMlInitialization(data, OBS_BGREMOVAL_ORT_SESSION_ERROR_FILE_NOT_FOUND,
						   "model file is missing or invalid");
	}
	data.modelFilepath = modelPath.native();
	auto result = windows_ml::create_session(*data.env, modelPath,
						 {data.useGPU, data.modelSelection == MODEL_MEDIAPIPE,
						  static_cast<int>(data.numThreads)});
	data.sessionDiagnostics = std::move(result.diagnostics);
	data.session = std::move(result.session);
	if (!data.session) {
		return failWindowsMlInitialization(data, OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP, "");
	}
	data.model->populateInputOutputNames(data.session, data.inputNames, data.outputNames);
	if (!data.model->populateInputOutputShapes(data.session, data.inputDims, data.outputDims) ||
	    !validMetadata(data.inputNames, data.inputDims) || !validMetadata(data.outputNames, data.outputDims)) {
		return failWindowsMlInitialization(data, OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT,
						   "invalid model metadata");
	}
	data.model->allocateTensorBuffers(data.inputDims, data.outputDims, data.outputTensorValues,
					  data.inputTensorValues, data.inputTensor, data.outputTensor);
	if (!validBuffers(data.inputDims, data.inputTensorValues, data.inputTensor) ||
	    !validBuffers(data.outputDims, data.outputTensorValues, data.outputTensor)) {
		return failWindowsMlInitialization(data, OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT,
						   "incomplete model tensor buffers");
	}
	data.sessionDiagnostics.outcome = windows_ml::SessionOutcome::Ready;
	return OBS_BGREMOVAL_ORT_SESSION_SUCCESS;
}
} // namespace

void logWindowsMlSessionOutcome(const windows_ml::SessionDiagnostics &diagnostics, std::string_view model)
{
	const auto *attempt = diagnostics.provider_attempt ? &*diagnostics.provider_attempt : nullptr;
	const auto *device = attempt && attempt->selected_device ? &*attempt->selected_device : nullptr;
	const bool activeGpu = diagnostics.outcome == windows_ml::SessionOutcome::Ready && device &&
			       diagnostics.effective_provider == device->ep_name;
	char hresult[16] = "none";
	if (attempt && attempt->error_hresult) {
		snprintf(hresult, sizeof(hresult), "0x%08x", static_cast<unsigned int>(*attempt->error_hresult));
	}
	obs_log(diagnostics.outcome == windows_ml::SessionOutcome::Ready ? LOG_INFO : LOG_ERROR,
		"Windows ML session: outcome=%s requested=%s runtime=%s effective=%s model=%.*s fallback=%s "
		"catalog_provider=%s readiness_before=%s readiness_after=%s activation=%s registration=%s "
		"active_ep=%s vendor=0x%08x device=0x%08x attempted_ep=%s attempted_vendor=0x%08x "
		"attempted_device=0x%08x stage=%s hresult=%s provider_error=%s error=%s cpu_error=%s",
		outcomeName(diagnostics.outcome), present(diagnostics.requested_provider),
		present(diagnostics.requested_runtime_provider), present(diagnostics.effective_provider),
		static_cast<int>(std::min(model.size(), static_cast<size_t>(std::numeric_limits<int>::max()))),
		model.empty() ? "" : model.data(), present(diagnostics.fallback_reason),
		attempt ? present(attempt->discovered_provider_name) : "none",
		attempt ? present(attempt->ready_state_before) : "none",
		attempt ? present(attempt->ready_state_after) : "none",
		attempt && attempt->process_activation_attempted ? "true" : "false",
		attempt && attempt->provider_registration_succeeded ? "true" : "false",
		activeGpu ? present(device->ep_name) : "none",
		activeGpu ? static_cast<unsigned int>(device->vendor_id) : 0u,
		activeGpu ? static_cast<unsigned int>(device->device_id) : 0u,
		device ? present(device->ep_name) : "none", device ? static_cast<unsigned int>(device->vendor_id) : 0u,
		device ? static_cast<unsigned int>(device->device_id) : 0u,
		attempt ? stageName(attempt->failure_stage) : "None", hresult,
		attempt ? present(attempt->error) : "none", present(diagnostics.error), present(diagnostics.cpu_error));
}

int createWindowsMlOrtSession(filter_data *tf, const std::filesystem::path &modelPath)
{
	if (!tf) {
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_MODEL;
	}
	int code = OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP;
	try {
		beginWindowsMlInitialization(*tf);
		code = initializeWindowsMlSession(*tf, modelPath);
	} catch (const std::exception &error) {
		code = failWindowsMlInitialization(*tf, OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP, error.what());
	} catch (...) {
		code = failWindowsMlInitialization(*tf, OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP,
						   "unknown model initialization failure");
	}
	logWindowsMlSessionOutcome(tf->sessionDiagnostics, tf->modelSelection);
	return code;
}
#endif

int createOrtSession(filter_data *tf)
{
#ifdef _WIN32
	if (!tf) {
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_MODEL;
	}
	beginWindowsMlInitialization(*tf);
	try {
		// OBS returns UTF-8. Own its allocation even if path conversion throws.
		const auto freePath = [](char *path) {
			bfree(path);
		};
		std::unique_ptr<char, decltype(freePath)> path(obs_module_file(tf->modelSelection.c_str()), freePath);
		const std::string_view utf8Path = path ? path.get() : "";
		return createWindowsMlOrtSession(
			tf, path ? std::filesystem::path(std::u8string(utf8Path.begin(), utf8Path.end()))
				 : std::filesystem::path{});
	} catch (const std::exception &error) {
		beginWindowsMlInitialization(*tf);
		const int code =
			failWindowsMlInitialization(*tf, OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP, error.what());
		logWindowsMlSessionOutcome(tf->sessionDiagnostics, tf->modelSelection);
		return code;
	} catch (...) {
		const int code = failWindowsMlInitialization(*tf, OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP,
							     "unknown model path resolution failure");
		logWindowsMlSessionOutcome(tf->sessionDiagnostics, tf->modelSelection);
		return code;
	}
#else
	resetOrtSessionData(*tf);
	if (tf->model.get() == nullptr) {
		obs_log(LOG_ERROR, "Model object is not initialized");
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_MODEL;
	}

	Ort::SessionOptions sessionOptions;

	sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	if (tf->useGPU != USEGPU_CPU) {
		sessionOptions.DisableMemPattern();
		sessionOptions.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	} else {
		sessionOptions.SetInterOpNumThreads(tf->numThreads);
		sessionOptions.SetIntraOpNumThreads(tf->numThreads);
	}

	char *modelFilepath_rawPtr = obs_module_file(tf->modelSelection.c_str());

	if (modelFilepath_rawPtr == nullptr) {
		obs_log(LOG_ERROR, "Unable to get model filename %s from plugin.", tf->modelSelection.c_str());
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_FILE_NOT_FOUND;
	}

	std::string modelFilepath_s(modelFilepath_rawPtr);

	tf->modelFilepath = std::string(modelFilepath_rawPtr);

	bfree(modelFilepath_rawPtr);

	try {
#ifdef HAVE_ONNXRUNTIME_CUDA_EP
		if (tf->useGPU == USEGPU_CUDA) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_CUDA(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_ROCM_EP
		if (tf->useGPU == USEGPU_ROCM) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_ROCM(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_MIGRAPHX_EP
		if (tf->useGPU == USEGPU_MIGRAPHX) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_MIGraphX(sessionOptions, 0));
		}
#endif
#ifdef HAVE_ONNXRUNTIME_TENSORRT_EP
		if (tf->useGPU == USEGPU_TENSORRT) {
			Ort::ThrowOnError(OrtSessionOptionsAppendExecutionProvider_Tensorrt(sessionOptions, 0));
		}
#endif
#if defined(__APPLE__)
		if (tf->useGPU == USEGPU_COREML) {
			uint32_t coreml_flags = 0;
			coreml_flags |= COREML_FLAG_ENABLE_ON_SUBGRAPH;
			Ort::ThrowOnError(
				OrtSessionOptionsAppendExecutionProvider_CoreML(sessionOptions, coreml_flags));
		}
#endif
		tf->session.reset(new Ort::Session(*tf->env, tf->modelFilepath.c_str(), sessionOptions));
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "%s", e.what());
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_STARTUP;
	}

	Ort::AllocatorWithDefaultOptions allocator;

	tf->model->populateInputOutputNames(tf->session, tf->inputNames, tf->outputNames);

	if (!tf->model->populateInputOutputShapes(tf->session, tf->inputDims, tf->outputDims)) {
		obs_log(LOG_ERROR, "Unable to get model input and output shapes");
		return OBS_BGREMOVAL_ORT_SESSION_ERROR_INVALID_INPUT_OUTPUT;
	}

	for (size_t i = 0; i < tf->inputNames.size(); i++) {
		obs_log(LOG_INFO, "Model %s input %d: name %s shape (%d dim) %d x %d x %d x %d",
			tf->modelSelection.c_str(), (int)i, tf->inputNames[i].get(), (int)tf->inputDims[i].size(),
			(int)tf->inputDims[i][0], ((int)tf->inputDims[i].size() > 1) ? (int)tf->inputDims[i][1] : 0,
			((int)tf->inputDims[i].size() > 2) ? (int)tf->inputDims[i][2] : 0,
			((int)tf->inputDims[i].size() > 3) ? (int)tf->inputDims[i][3] : 0);
	}
	for (size_t i = 0; i < tf->outputNames.size(); i++) {
		obs_log(LOG_INFO, "Model %s output %d: name %s shape (%d dim) %d x %d x %d x %d",
			tf->modelSelection.c_str(), (int)i, tf->outputNames[i].get(), (int)tf->outputDims[i].size(),
			(int)tf->outputDims[i][0], ((int)tf->outputDims[i].size() > 1) ? (int)tf->outputDims[i][1] : 0,
			((int)tf->outputDims[i].size() > 2) ? (int)tf->outputDims[i][2] : 0,
			((int)tf->outputDims[i].size() > 3) ? (int)tf->outputDims[i][3] : 0);
	}

	// Allocate buffers
	tf->model->allocateTensorBuffers(tf->inputDims, tf->outputDims, tf->outputTensorValues, tf->inputTensorValues,
					 tf->inputTensor, tf->outputTensor);

	return OBS_BGREMOVAL_ORT_SESSION_SUCCESS;
#endif
}

bool runFilterModelInference(filter_data *tf, const cv::Mat &imageBGRA, cv::Mat &output)
{
	if (!tf || tf->isDisabled || tf->session.get() == nullptr) {
		// Onnx runtime session is not initialized. Problem in initialization
		return false;
	}
	if (tf->model.get() == nullptr) {
		// Model object is not initialized
		return false;
	}

	// To RGB
	cv::Mat imageRGB;
	cv::cvtColor(imageBGRA, imageRGB, cv::COLOR_BGRA2RGB);

	// Resize to network input size
	uint32_t inputWidth, inputHeight;
	tf->model->getNetworkInputSize(tf->inputDims, inputWidth, inputHeight);

	cv::Mat resizedImageRGB;
	cv::resize(imageRGB, resizedImageRGB, cv::Size(inputWidth, inputHeight));

	// Prepare input to nework
	cv::Mat resizedImage, preprocessedImage;
	resizedImageRGB.convertTo(resizedImage, CV_32F);

	tf->model->prepareInputToNetwork(resizedImage, preprocessedImage);

	tf->model->loadInputToTensor(preprocessedImage, inputWidth, inputHeight, tf->inputTensorValues);

	// Run network inference
	tf->model->runNetworkInference(tf->session, tf->inputNames, tf->outputNames, tf->inputTensor, tf->outputTensor);

	// Get output
	// Map network output to cv::Mat
	cv::Mat outputImage = tf->model->getNetworkOutput(tf->outputDims, tf->outputTensorValues);

	// Assign output to input in some models that have temporal information
	tf->model->assignOutputToInput(tf->outputTensorValues, tf->inputTensorValues);

	// Post-process output. The image will now be in [0,1] float, BHWC format
	tf->model->postprocessOutput(outputImage);

	// Convert [0,1] float to CV_8U [0,255]
	outputImage.convertTo(output, CV_8U, 255.0);

	return true;
}

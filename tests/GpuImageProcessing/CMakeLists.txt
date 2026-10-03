# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(GpuImageProcessingTests LANGUAGES CXX)
  enable_testing()
  option(GPU_IMAGE_PROCESSING_POLICY_ONLY "Build only the OBS/OpenCV-free policy tests" ON)
endif()

set(GPU_IMAGE_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../src/obs-utils")
add_executable(gpu-image-policy policy-test.cpp "${GPU_IMAGE_SOURCE_DIR}/gpu-image-policy.cpp")
target_include_directories(gpu-image-policy PRIVATE "${GPU_IMAGE_SOURCE_DIR}")
target_compile_features(gpu-image-policy PRIVATE cxx_std_20)
if(TARGET BuildOptions)
  target_link_libraries(gpu-image-policy PRIVATE BuildOptions)
endif()
add_test(NAME gpu-image-policy COMMAND gpu-image-policy)

if(NOT GPU_IMAGE_PROCESSING_POLICY_ONLY)
  if(NOT WIN32)
    message(FATAL_ERROR "GPU image pipeline tests require native Windows")
  endif()
  add_executable(
    gpu-image-pipeline
    pipeline-test.cpp
    "${GPU_IMAGE_SOURCE_DIR}/gpu-image-policy.cpp"
    "${GPU_IMAGE_SOURCE_DIR}/gpu-image-pipeline.cpp"
  )
  target_include_directories(gpu-image-pipeline PRIVATE "${GPU_IMAGE_SOURCE_DIR}")
  target_compile_features(gpu-image-pipeline PRIVATE cxx_std_20)
  target_link_libraries(gpu-image-pipeline PRIVATE BuildOptions OpenCV::opencv_core)
  add_test(NAME gpu-image-pipeline COMMAND gpu-image-pipeline)
  set_tests_properties(gpu-image-pipeline PROPERTIES TIMEOUT 60)

  add_executable(
    gpu-image-processing-native
    native-main.cpp
    input-test.cpp
    graphics-faults.cpp
    "${GPU_IMAGE_SOURCE_DIR}/gpu-image-policy.cpp"
    "${GPU_IMAGE_SOURCE_DIR}/gpu-input-preprocessor.cpp"
  )
  # Source properties are scoped to this tests directory: the plugin's compilation is untouched.
  set_source_files_properties(
    "${GPU_IMAGE_SOURCE_DIR}/gpu-input-preprocessor.cpp"
    PROPERTIES COMPILE_OPTIONS "/FI${CMAKE_CURRENT_SOURCE_DIR}/graphics-faults.hpp"
  )
  target_include_directories(gpu-image-processing-native PRIVATE "${GPU_IMAGE_SOURCE_DIR}")
  target_compile_features(gpu-image-processing-native PRIVATE cxx_std_20)
  target_link_libraries(
    gpu-image-processing-native
    PRIVATE BuildOptions OBS::libobs OpenCV::opencv_core OpenCV::opencv_imgproc
  )
  add_custom_command(
    TARGET gpu-image-processing-native
    POST_BUILD
    COMMAND
      "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:gpu-image-processing-native>
      $<TARGET_FILE_DIR:gpu-image-processing-native>
    COMMAND_EXPAND_LISTS
  )
  add_test(
    NAME gpu-image-processing-native
    COMMAND
      gpu-image-processing-native --effect-root "${PROJECT_SOURCE_DIR}/data/effects" --model
      "${PROJECT_SOURCE_DIR}/data/models/mediapipe.onnx"
  )
  set_tests_properties(gpu-image-processing-native PROPERTIES TIMEOUT 300)
endif()

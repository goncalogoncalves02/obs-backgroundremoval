# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.21)

project(windows-ml-smoke LANGUAGES CXX)

option(WINDOWS_ML_SMOKE_CLI_TESTS_ONLY "Build only the portable command-line parser tests" OFF)

add_library(windows-ml-smoke-cli STATIC cli.cpp)
target_compile_features(windows-ml-smoke-cli PUBLIC cxx_std_20)
target_include_directories(windows-ml-smoke-cli PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(windows-ml-smoke-benchmark STATIC benchmark.cpp)
target_compile_features(windows-ml-smoke-benchmark PUBLIC cxx_std_20)
target_include_directories(windows-ml-smoke-benchmark PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(windows-ml-smoke-benchmark-report STATIC benchmark-report.cpp)
target_compile_features(windows-ml-smoke-benchmark-report PUBLIC cxx_std_20)
target_include_directories(windows-ml-smoke-benchmark-report PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/windows_ml_sessions.cmake")
add_windows_ml_policy_targets()

add_library(windows-ml-smoke-provider-report STATIC provider-report.cpp)
target_compile_features(windows-ml-smoke-provider-report PUBLIC cxx_std_20)
target_include_directories(
  windows-ml-smoke-provider-report
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/../../src/ort-utils"
)

if(WINDOWS_ML_SMOKE_CLI_TESTS_ONLY)
  enable_testing()

  add_executable(windows-ml-smoke-cli-test tests/cli-test.cpp)
  target_link_libraries(windows-ml-smoke-cli-test PRIVATE windows-ml-smoke-cli)

  add_executable(windows-ml-smoke-benchmark-test tests/benchmark-test.cpp)
  target_link_libraries(windows-ml-smoke-benchmark-test PRIVATE windows-ml-smoke-benchmark)

  add_executable(windows-ml-smoke-benchmark-report-test tests/benchmark-report-test.cpp)
  target_link_libraries(windows-ml-smoke-benchmark-report-test PRIVATE windows-ml-smoke-benchmark-report)

  add_executable(windows-ml-smoke-provider-report-test tests/provider-report-test.cpp)
  target_link_libraries(windows-ml-smoke-provider-report-test PRIVATE windows-ml-smoke-provider-report)

  add_executable(windows-ml-smoke-provider-policy-test tests/provider-policy-test.cpp)
  target_link_libraries(windows-ml-smoke-provider-policy-test PRIVATE windows-ml-smoke-provider-policy)

  add_executable(windows-ml-session-policy-test tests/session-policy-test.cpp)
  target_link_libraries(windows-ml-session-policy-test PRIVATE windows-ml-session-policy)

  add_test(NAME windows-ml-smoke-cli COMMAND windows-ml-smoke-cli-test)
  add_test(NAME windows-ml-smoke-benchmark COMMAND windows-ml-smoke-benchmark-test)
  add_test(NAME windows-ml-smoke-benchmark-report COMMAND windows-ml-smoke-benchmark-report-test)
  add_test(NAME windows-ml-smoke-provider-report COMMAND windows-ml-smoke-provider-report-test)
  add_test(NAME windows-ml-smoke-provider-policy COMMAND windows-ml-smoke-provider-policy-test)
  add_test(NAME windows-ml-session-policy COMMAND windows-ml-session-policy-test)
  return()
endif()

if(NOT WIN32)
  message(FATAL_ERROR "windows-ml-smoke is supported only on Windows")
endif()

find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED)

include("${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/onnxruntime_backend.cmake")
file(REAL_PATH "${microsoft.windows.ai.machinelearning_DIR}/../.." windows_ml_package_root)
resolve_windows_ml_directml_dll("${windows_ml_package_root}" WINDOWS_ML_DIRECTML_DLL)

enable_testing()

add_windows_ml_session_core()
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/../../data/models/mediapipe.onnx" session_check_model_sha256)
file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/../../data/models/mediapipe.onnx" session_check_model_size)
add_executable(windows-ml-session-check production-session-check.cpp)
target_compile_features(windows-ml-session-check PRIVATE cxx_std_20)
target_compile_definitions(
  windows-ml-session-check
  PRIVATE
    WINDOWS_ML_PACKAGE_VERSION="2.2.12"
    WINDOWS_ML_MEDIAPIPE_SHA256="${session_check_model_sha256}"
    WINDOWS_ML_MEDIAPIPE_SIZE=${session_check_model_size}
)
target_link_libraries(windows-ml-session-check PRIVATE windows-ml-session-core windows-ml-smoke-benchmark Crypt32)
stage_windows_ml_runtime(windows-ml-session-check)
add_test(
  NAME windows-ml-session-check-cpu
  COMMAND
    windows-ml-session-check --provider cpu --model "${CMAKE_CURRENT_SOURCE_DIR}/../../data/models/mediapipe.onnx"
    --iterations 100 --cycles 3 --require-effective
)

add_executable(windows-ml-session-core-test tests/session-core-test.cpp)
target_link_libraries(windows-ml-session-core-test PRIVATE windows-ml-session-core)
stage_windows_ml_runtime(windows-ml-session-core-test)
add_test(
  NAME windows-ml-session-core
  COMMAND windows-ml-session-core-test "${CMAKE_CURRENT_SOURCE_DIR}/../../data/models/mediapipe.onnx"
)

add_executable(windows-ml-smoke main.cpp inference-runner.cpp)
target_compile_features(windows-ml-smoke PRIVATE cxx_std_20)
target_compile_definitions(windows-ml-smoke PRIVATE WINDOWS_ML_PACKAGE_VERSION="2.2.12")
target_include_directories(windows-ml-smoke PRIVATE ../../src/ort-utils)
target_link_libraries(
  windows-ml-smoke
  PRIVATE
    windows-ml-smoke-cli
    windows-ml-smoke-benchmark
    windows-ml-smoke-benchmark-report
    windows-ml-smoke-provider-policy
    windows-ml-smoke-provider-report
    windows-ml-session-core
)

stage_windows_ml_runtime(windows-ml-smoke)

add_executable(windows-ml-smoke-inference-metadata-test tests/inference-metadata-test.cpp inference-runner.cpp)
target_compile_features(windows-ml-smoke-inference-metadata-test PRIVATE cxx_std_20)
target_include_directories(windows-ml-smoke-inference-metadata-test PRIVATE ../../src/ort-utils)
target_link_libraries(
  windows-ml-smoke-inference-metadata-test
  PRIVATE windows-ml-smoke-benchmark windows-ml-session-core
)

stage_windows_ml_runtime(windows-ml-smoke-inference-metadata-test)

add_test(
  NAME windows-ml-smoke-inference-metadata
  COMMAND windows-ml-smoke-inference-metadata-test "${CMAKE_CURRENT_SOURCE_DIR}/../../data/models/mediapipe.onnx"
)

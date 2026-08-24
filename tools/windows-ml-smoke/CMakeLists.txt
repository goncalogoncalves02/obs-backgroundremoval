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

add_library(windows-ml-smoke-provider-policy STATIC ../../src/ort-utils/windows-ml-provider-policy.cpp)
target_compile_features(windows-ml-smoke-provider-policy PUBLIC cxx_std_20)
target_include_directories(
  windows-ml-smoke-provider-policy PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/../../src/ort-utils"
)

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

  add_executable(windows-ml-smoke-provider-report-test tests/provider-report-test.cpp)
  target_link_libraries(windows-ml-smoke-provider-report-test PRIVATE windows-ml-smoke-provider-report)

  add_executable(windows-ml-smoke-provider-policy-test tests/provider-policy-test.cpp)
  target_link_libraries(windows-ml-smoke-provider-policy-test PRIVATE windows-ml-smoke-provider-policy)

  add_test(NAME windows-ml-smoke-cli COMMAND windows-ml-smoke-cli-test)
  add_test(NAME windows-ml-smoke-benchmark COMMAND windows-ml-smoke-benchmark-test)
  add_test(NAME windows-ml-smoke-provider-report COMMAND windows-ml-smoke-provider-report-test)
  add_test(NAME windows-ml-smoke-provider-policy COMMAND windows-ml-smoke-provider-policy-test)
  return()
endif()

if(NOT WIN32)
  message(FATAL_ERROR "windows-ml-smoke is supported only on Windows")
endif()

find_package(microsoft.windows.ai.machinelearning CONFIG REQUIRED)

add_executable(windows-ml-smoke main.cpp ../../src/ort-utils/windows-ml-provider.cpp)
target_compile_features(windows-ml-smoke PRIVATE cxx_std_20)
target_compile_definitions(windows-ml-smoke PRIVATE WINDOWS_ML_PACKAGE_VERSION="2.2.12")
target_include_directories(windows-ml-smoke PRIVATE ../../src/ort-utils)
target_link_libraries(
  windows-ml-smoke PRIVATE windows-ml-smoke-cli windows-ml-smoke-provider-policy windows-ml-smoke-provider-report
                           WindowsML::Api WindowsML::OnnxRuntime
)

add_custom_command(
  TARGET windows-ml-smoke
  POST_BUILD
  COMMAND
    "${CMAKE_COMMAND}" -E copy_if_different $<TARGET_RUNTIME_DLLS:windows-ml-smoke> $<TARGET_FILE_DIR:windows-ml-smoke>
  COMMAND_EXPAND_LISTS
)

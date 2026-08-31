# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

function(add_onnxruntime_backend TARGET_NAME EXPECTED_WINDOWS_ML_VERSION)
  if(TARGET "${TARGET_NAME}")
    message(FATAL_ERROR "ONNX Runtime backend target '${TARGET_NAME}' already exists.")
  endif()

  add_library("${TARGET_NAME}" INTERFACE)

  if(WIN32)
    if(NOT DEFINED microsoft.windows.ai.machinelearning_DIR
       OR "${microsoft.windows.ai.machinelearning_DIR}" STREQUAL "")
      message(FATAL_ERROR "microsoft.windows.ai.machinelearning_DIR must be set explicitly.")
    endif()

    set(requested_windows_ml_directory "${microsoft.windows.ai.machinelearning_DIR}")
    cmake_path(
      ABSOLUTE_PATH
      requested_windows_ml_directory
      BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
      NORMALIZE
    )
    set(
      microsoft.windows.ai.machinelearning_DIR
      "${requested_windows_ml_directory}"
      CACHE PATH
      "Directory containing the Microsoft.Windows.AI.MachineLearning package configuration"
      FORCE
    )

    find_package(
      microsoft.windows.ai.machinelearning
      CONFIG
      REQUIRED
      PATHS "${requested_windows_ml_directory}"
      NO_DEFAULT_PATH
    )

    set(resolved_windows_ml_directory "${microsoft.windows.ai.machinelearning_DIR}")
    cmake_path(
      ABSOLUTE_PATH
      resolved_windows_ml_directory
      BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
      NORMALIZE
    )
    set(requested_windows_ml_directory_for_comparison "${requested_windows_ml_directory}")
    set(resolved_windows_ml_directory_for_comparison "${resolved_windows_ml_directory}")
    if(CMAKE_HOST_WIN32)
      string(TOLOWER "${requested_windows_ml_directory_for_comparison}" requested_windows_ml_directory_for_comparison)
      string(TOLOWER "${resolved_windows_ml_directory_for_comparison}" resolved_windows_ml_directory_for_comparison)
    endif()
    if(NOT resolved_windows_ml_directory_for_comparison STREQUAL requested_windows_ml_directory_for_comparison)
      message(
        FATAL_ERROR
        "Resolved Microsoft.Windows.AI.MachineLearning package directory ${resolved_windows_ml_directory} "
        "does not match requested directory ${requested_windows_ml_directory}."
      )
    endif()

    set(found_windows_ml_version undefined)
    if(DEFINED WINML_VERSION)
      set(found_windows_ml_version "${WINML_VERSION}")
    endif()
    if(NOT found_windows_ml_version VERSION_EQUAL EXPECTED_WINDOWS_ML_VERSION)
      message(FATAL_ERROR "Expected Microsoft.Windows.AI.MachineLearning ${EXPECTED_WINDOWS_ML_VERSION}, found ${found_windows_ml_version}.")
    endif()

    foreach(required_target IN ITEMS WindowsML::Api WindowsML::OnnxRuntime)
      if(NOT TARGET "${required_target}")
        message(FATAL_ERROR "Microsoft.Windows.AI.MachineLearning is missing required target ${required_target}.")
      endif()
    endforeach()

    target_link_libraries("${TARGET_NAME}" INTERFACE WindowsML::Api WindowsML::OnnxRuntime)

    cmake_path(GET microsoft.windows.ai.machinelearning_DIR PARENT_PATH windows_ml_build_dir)
    cmake_path(GET windows_ml_build_dir PARENT_PATH windows_ml_package_root)
    cmake_path(NORMAL_PATH windows_ml_package_root)
    set(windows_ml_license_file "${windows_ml_package_root}/license.txt")
    set(windows_ml_third_party_notices_file "${windows_ml_package_root}/ThirdPartyNotices.txt")

    foreach(required_file IN ITEMS "${windows_ml_license_file}" "${windows_ml_third_party_notices_file}")
      if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Microsoft.Windows.AI.MachineLearning is missing required legal file ${required_file}.")
      endif()
    endforeach()

    set(WINDOWS_ML_PACKAGE_ROOT "${windows_ml_package_root}" PARENT_SCOPE)
    set(WINDOWS_ML_LICENSE_FILE "${windows_ml_license_file}" PARENT_SCOPE)
    set(WINDOWS_ML_THIRD_PARTY_NOTICES_FILE "${windows_ml_third_party_notices_file}" PARENT_SCOPE)
    return()
  endif()

  find_package(onnxruntime CONFIG)
  if(NOT onnxruntime_FOUND AND PkgConfig_FOUND)
    pkg_check_modules(PC_onnxruntime onnxruntime IMPORTED_TARGET)
    if(PC_onnxruntime_FOUND)
      add_library(onnxruntime::onnxruntime ALIAS PkgConfig::PC_onnxruntime)
      set(onnxruntime_FOUND TRUE)
    endif()
  endif()
  if(NOT onnxruntime_FOUND)
    message(FATAL_ERROR "ONNX Runtime not found via CMake or pkg-config.")
  endif()
  if(NOT TARGET onnxruntime::onnxruntime)
    add_library(onnxruntime::onnxruntime ALIAS onnxruntime)
  endif()
  target_link_libraries("${TARGET_NAME}" INTERFACE onnxruntime::onnxruntime)
endfunction()

# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

function(add_onnxruntime_backend TARGET_NAME EXPECTED_WINDOWS_ML_VERSION REQUESTED_WINDOWS_ML_DIRECTORY)
  if(TARGET "${TARGET_NAME}")
    message(FATAL_ERROR "ONNX Runtime backend target '${TARGET_NAME}' already exists.")
  endif()

  add_library("${TARGET_NAME}" INTERFACE)

  if(WIN32)
    set(windows_ml_package_state_schema_version 1)
    set(has_one_shot_windows_ml_directory FALSE)
    set(has_package_windows_ml_directory FALSE)
    if(DEFINED CACHE{WINDOWS_ML_PACKAGE_CONFIG_DIR})
      set(has_one_shot_windows_ml_directory TRUE)
      set(one_shot_windows_ml_directory "$CACHE{WINDOWS_ML_PACKAGE_CONFIG_DIR}")
    endif()
    if(DEFINED CACHE{microsoft.windows.ai.machinelearning_DIR})
      set(has_package_windows_ml_directory TRUE)
      set(package_windows_ml_directory "$CACHE{microsoft.windows.ai.machinelearning_DIR}")
    endif()
    unset(WINDOWS_ML_PACKAGE_CONFIG_DIR CACHE)
    unset(microsoft.windows.ai.machinelearning_DIR CACHE)

    # Capture only the internal state, then invalidate it until validation succeeds.
    get_property(stored_windows_ml_schema CACHE OBS_WINDOWS_ML_PACKAGE_STATE_SCHEMA PROPERTY VALUE)
    get_property(stored_windows_ml_schema_type CACHE OBS_WINDOWS_ML_PACKAGE_STATE_SCHEMA PROPERTY TYPE)
    get_property(stored_windows_ml_directory CACHE OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY PROPERTY VALUE)
    get_property(stored_windows_ml_directory_type CACHE OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY PROPERTY TYPE)
    unset(OBS_WINDOWS_ML_PACKAGE_STATE_SCHEMA CACHE)
    unset(OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY CACHE)

    if(has_one_shot_windows_ml_directory OR has_package_windows_ml_directory)
      if(NOT has_one_shot_windows_ml_directory OR "${one_shot_windows_ml_directory}" STREQUAL "")
        message(FATAL_ERROR "WINDOWS_ML_PACKAGE_CONFIG_DIR must be supplied for every fresh package selection.")
      endif()
      if(NOT has_package_windows_ml_directory OR "${package_windows_ml_directory}" STREQUAL "")
        message(
          FATAL_ERROR
          "microsoft.windows.ai.machinelearning_DIR must be set explicitly for every fresh package selection."
        )
      endif()

      set(requested_windows_ml_directory "${REQUESTED_WINDOWS_ML_DIRECTORY}")
      cmake_path(ABSOLUTE_PATH requested_windows_ml_directory BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE)
      cmake_path(ABSOLUTE_PATH one_shot_windows_ml_directory BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE)
      cmake_path(ABSOLUTE_PATH package_windows_ml_directory BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE)
      file(REAL_PATH "${requested_windows_ml_directory}" requested_windows_ml_directory)
      file(REAL_PATH "${one_shot_windows_ml_directory}" one_shot_windows_ml_directory)
      file(REAL_PATH "${package_windows_ml_directory}" package_windows_ml_directory)

      set(requested_windows_ml_directory_for_comparison "${requested_windows_ml_directory}")
      set(one_shot_windows_ml_directory_for_comparison "${one_shot_windows_ml_directory}")
      set(package_windows_ml_directory_for_comparison "${package_windows_ml_directory}")
      if(CMAKE_HOST_WIN32)
        string(TOLOWER "${requested_windows_ml_directory_for_comparison}" requested_windows_ml_directory_for_comparison)
        string(TOLOWER "${one_shot_windows_ml_directory_for_comparison}" one_shot_windows_ml_directory_for_comparison)
        string(TOLOWER "${package_windows_ml_directory_for_comparison}" package_windows_ml_directory_for_comparison)
      endif()
      if(NOT requested_windows_ml_directory_for_comparison STREQUAL one_shot_windows_ml_directory_for_comparison)
        message(FATAL_ERROR "WINDOWS_ML_PACKAGE_CONFIG_DIR was not forwarded unchanged to the ONNX Runtime backend.")
      endif()
      if(NOT requested_windows_ml_directory_for_comparison STREQUAL package_windows_ml_directory_for_comparison)
        message(
          FATAL_ERROR
          "WINDOWS_ML_PACKAGE_CONFIG_DIR and microsoft.windows.ai.machinelearning_DIR "
          "must name the same directory."
        )
      endif()
    else()
      if("${stored_windows_ml_schema_type}" STREQUAL "")
        message(FATAL_ERROR "No validated Windows ML package state is available; supply both package directory inputs.")
      endif()
      if(NOT "${stored_windows_ml_schema}" STREQUAL "${windows_ml_package_state_schema_version}")
        message(FATAL_ERROR "Found unsupported Windows ML package state schema '${stored_windows_ml_schema}'.")
      endif()
      if(
        NOT stored_windows_ml_schema_type STREQUAL "INTERNAL"
        OR NOT stored_windows_ml_directory_type STREQUAL "INTERNAL"
        OR "${stored_windows_ml_directory}" STREQUAL ""
      )
        message(FATAL_ERROR "The validated Windows ML package state marker is corrupt.")
      endif()
      cmake_path(IS_ABSOLUTE stored_windows_ml_directory stored_windows_ml_directory_is_absolute)
      file(
        REAL_PATH "${stored_windows_ml_directory}"
        requested_windows_ml_directory
        BASE_DIRECTORY "${CMAKE_BINARY_DIR}"
      )
      if(
        NOT stored_windows_ml_directory_is_absolute
        OR NOT "${stored_windows_ml_directory}" STREQUAL "${requested_windows_ml_directory}"
      )
        message(FATAL_ERROR "The validated Windows ML package state directory is not canonical.")
      endif()
    endif()

    foreach(
      redirect_config
      IN
      ITEMS
        "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/microsoft.windows.ai.machinelearning-config.cmake"
        "${CMAKE_FIND_PACKAGE_REDIRECTS_DIR}/microsoft.windows.ai.machinelearningConfig.cmake"
    )
      if(EXISTS "${redirect_config}")
        message(
          FATAL_ERROR
          "A CMake package redirect for Microsoft.Windows.AI.MachineLearning is not permitted: ${redirect_config}"
        )
      endif()
    endforeach()

    # A normal variable can shadow the cleared cache; pin it to the selected path too.
    set(microsoft.windows.ai.machinelearning_DIR "${requested_windows_ml_directory}")
    find_package(
      microsoft.windows.ai.machinelearning
      CONFIG
      QUIET
      PATHS "${requested_windows_ml_directory}"
      NO_DEFAULT_PATH
    )

    set(resolved_windows_ml_directory "${microsoft.windows.ai.machinelearning_DIR}")
    unset(microsoft.windows.ai.machinelearning_DIR CACHE)
    unset(microsoft.windows.ai.machinelearning_DIR)
    if(NOT microsoft.windows.ai.machinelearning_FOUND)
      message(
        FATAL_ERROR
        "Could not find a package configuration file provided by \"microsoft.windows.ai.machinelearning\" "
        "in requested directory ${requested_windows_ml_directory}."
      )
    endif()
    if("${resolved_windows_ml_directory}" STREQUAL "")
      message(FATAL_ERROR "Microsoft.Windows.AI.MachineLearning did not report its resolved package directory.")
    endif()
    cmake_path(ABSOLUTE_PATH resolved_windows_ml_directory BASE_DIRECTORY "${CMAKE_BINARY_DIR}" NORMALIZE)
    file(REAL_PATH "${resolved_windows_ml_directory}" resolved_windows_ml_directory)
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
      message(
        FATAL_ERROR
        "Expected Microsoft.Windows.AI.MachineLearning ${EXPECTED_WINDOWS_ML_VERSION}, found ${found_windows_ml_version}."
      )
    endif()

    foreach(required_target IN ITEMS WindowsML::Api WindowsML::OnnxRuntime)
      if(NOT TARGET "${required_target}")
        message(FATAL_ERROR "Microsoft.Windows.AI.MachineLearning is missing required target ${required_target}.")
      endif()
    endforeach()

    target_link_libraries("${TARGET_NAME}" INTERFACE WindowsML::Api WindowsML::OnnxRuntime)

    cmake_path(GET resolved_windows_ml_directory PARENT_PATH windows_ml_build_dir)
    cmake_path(GET windows_ml_build_dir PARENT_PATH windows_ml_package_root)
    cmake_path(NORMAL_PATH windows_ml_package_root)
    set(windows_ml_license_file "${windows_ml_package_root}/license.txt")
    set(windows_ml_third_party_notices_file "${windows_ml_package_root}/ThirdPartyNotices.txt")

    foreach(required_file IN ITEMS "${windows_ml_license_file}" "${windows_ml_third_party_notices_file}")
      if(NOT EXISTS "${required_file}")
        message(FATAL_ERROR "Microsoft.Windows.AI.MachineLearning is missing required legal file ${required_file}.")
      endif()
    endforeach()

    # The pinned package has configuration-less DLL and import-library locations.
    # Override only these targets' inherited Release mapping for RelWithDebInfo.
    set_target_properties(WindowsML::Api WindowsML::OnnxRuntime PROPERTIES MAP_IMPORTED_CONFIG_RELWITHDEBINFO "")

    set(
      OBS_WINDOWS_ML_PACKAGE_STATE_SCHEMA
      "${windows_ml_package_state_schema_version}"
      CACHE INTERNAL
      "Validated Windows ML package state schema"
    )
    set(
      OBS_WINDOWS_ML_PACKAGE_STATE_DIRECTORY
      "${resolved_windows_ml_directory}"
      CACHE INTERNAL
      "Validated Windows ML package configuration directory"
    )

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

# SPDX-FileCopyrightText: 2026 Kaito Udagawa <umireon@kaito.tokyo>
#
# SPDX-License-Identifier: Apache-2.0

function(collect_licenses output_header)
  cmake_parse_arguments(PARSE_ARGV 1 COLLECT "SKIP_VENDORED_ONNXRUNTIME" "" "EXTRA_LICENSE_FILES")

  set(license_files "${CMAKE_SOURCE_DIR}/LICENSE" "${CMAKE_SOURCE_DIR}/NOTICE")

  if(NOT COLLECT_SKIP_VENDORED_ONNXRUNTIME)
    if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/onnxruntime/LICENSE")
      list(APPEND license_files "${CMAKE_SOURCE_DIR}/vendor/onnxruntime/LICENSE")
    endif()

    if(EXISTS "${CMAKE_SOURCE_DIR}/vendor/onnxruntime/ThirdPartyNotices.txt")
      list(APPEND license_files "${CMAKE_SOURCE_DIR}/vendor/onnxruntime/ThirdPartyNotices.txt")
    endif()
  endif()

  foreach(extra_license_file IN LISTS COLLECT_EXTRA_LICENSE_FILES)
    if(NOT EXISTS "${extra_license_file}")
      message(FATAL_ERROR "Required extra license file does not exist: ${extra_license_file}")
    endif()
    get_filename_component(extra_license_filename "${extra_license_file}" NAME)
    if(
      NOT extra_license_filename STREQUAL "license.txt"
      AND NOT extra_license_filename STREQUAL "ThirdPartyNotices.txt"
    )
      message(FATAL_ERROR "Unsupported extra license filename: ${extra_license_filename}")
    endif()
    list(APPEND license_files "${extra_license_file}")
  endforeach()

  foreach(prefix IN LISTS CMAKE_PREFIX_PATH)
    file(GLOB vcpkg_license_files "${prefix}/share/*/copyright")
    list(APPEND license_files ${vcpkg_license_files})
  endforeach()

  list(REMOVE_DUPLICATES license_files)
  list(SORT license_files)

  get_filename_component(output_directory "${output_header}" DIRECTORY)
  set(output_text "${output_directory}/legal.txt")
  file(WRITE "${output_text}" "")
  foreach(license_file IN LISTS license_files)
    get_filename_component(license_directory "${license_file}" DIRECTORY)
    get_filename_component(license_name "${license_directory}" NAME)
    if(license_file STREQUAL "${CMAKE_SOURCE_DIR}/LICENSE")
      set(license_name "obs-backgroundremoval")
    elseif(license_file STREQUAL "${CMAKE_SOURCE_DIR}/NOTICE")
      set(license_name "obs-backgroundremoval notices")
    elseif(license_file STREQUAL "${CMAKE_SOURCE_DIR}/vendor/onnxruntime/LICENSE")
      set(license_name "onnxruntime")
    elseif(license_file STREQUAL "${CMAKE_SOURCE_DIR}/vendor/onnxruntime/ThirdPartyNotices.txt")
      set(license_name "onnxruntime third-party notices")
    elseif(license_file IN_LIST COLLECT_EXTRA_LICENSE_FILES)
      get_filename_component(extra_license_filename "${license_file}" NAME)
      if(extra_license_filename STREQUAL "license.txt")
        set(license_name "Microsoft.Windows.AI.MachineLearning")
      elseif(extra_license_filename STREQUAL "ThirdPartyNotices.txt")
        set(license_name "Microsoft.Windows.AI.MachineLearning third-party notices")
      endif()
    endif()

    file(READ "${license_file}" license_text)
    file(APPEND "${output_text}" "===== ${license_name} =====\n\n${license_text}\n\n")
  endforeach()

  file(READ "${output_text}" legal_hex HEX)
  string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," legal_bytes "${legal_hex}")
  file(
    WRITE "${output_header}"
    "#pragma once\n\n"
    "#include <cstddef>\n\n"
    "namespace obs_backgroundremoval {\n"
    "inline constexpr unsigned char legal_text[] = {${legal_bytes}0x00};\n"
    "inline constexpr std::size_t legal_text_size = sizeof(legal_text) - 1;\n"
    "}\n"
  )
endfunction()

# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

function(add_windows_ml_policy_targets)
  set(_windows_ml_policy_dir "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/ort-utils")

  if(NOT TARGET windows-ml-provider-policy)
    add_library(windows-ml-provider-policy STATIC "${_windows_ml_policy_dir}/windows-ml-provider-policy.cpp")
    target_compile_features(windows-ml-provider-policy PUBLIC cxx_std_20)
    target_include_directories(windows-ml-provider-policy PUBLIC "${_windows_ml_policy_dir}")
  endif()

  if(NOT TARGET windows-ml-session-policy)
    add_library(windows-ml-session-policy STATIC "${_windows_ml_policy_dir}/windows-ml-session-policy.cpp")
    target_compile_features(windows-ml-session-policy PUBLIC cxx_std_20)
    target_include_directories(windows-ml-session-policy PUBLIC "${_windows_ml_policy_dir}")
  endif()

  if(NOT TARGET windows-ml-smoke-provider-policy)
    add_library(windows-ml-smoke-provider-policy ALIAS windows-ml-provider-policy)
  endif()
endfunction()

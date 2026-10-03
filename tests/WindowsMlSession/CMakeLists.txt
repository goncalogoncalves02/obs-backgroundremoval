# SPDX-FileCopyrightText: 2026 Gonçalo Filipe Brigues Gonçalves <goncalogoncalves.02@gmail.com>
# SPDX-License-Identifier: Apache-2.0

add_executable(
  windows-ml-plugin-session
  plugin-session-test.cpp
  obs-boundary-stubs.cpp
  "${PLUGIN_SOURCE_DIR}/ort-utils/ort-session-utils.cpp"
)
target_include_directories(windows-ml-plugin-session PRIVATE "${PLUGIN_SOURCE_DIR}")
target_link_libraries(
  windows-ml-plugin-session
  PRIVATE BuildOptions OBS::libobs OnnxRuntimeBackend windows-ml-session-core OpenCV::opencv_core OpenCV::opencv_imgproc
)
stage_windows_ml_runtime(windows-ml-plugin-session)
add_test(
  NAME windows-ml-plugin-session
  COMMAND windows-ml-plugin-session "${PROJECT_SOURCE_DIR}/data/models/mediapipe.onnx"
)
set_tests_properties(windows-ml-plugin-session PROPERTIES TIMEOUT 300)

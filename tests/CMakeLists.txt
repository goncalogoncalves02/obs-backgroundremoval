# SPDX-FileCopyrightText: 2026 Kaito Udagawa <umireon@kaito.tokyo>
#
# SPDX-License-Identifier: Apache-2.0

add_subdirectory(UI)

if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_subdirectory(UpdateConfig)
endif()

if(WIN32 AND BUILD_TESTING)
  add_subdirectory(WindowsMlSession)
  add_subdirectory(GpuImageProcessing)
endif()

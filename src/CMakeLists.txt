# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 TextAnalyst-Qt contributors
add_subdirectory(core)
add_subdirectory(io)
add_subdirectory(controller)
add_subdirectory(ui)

add_executable(textanalyst-qt main.cpp)
target_link_libraries(textanalyst-qt PRIVATE core io controller ui)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(textanalyst-qt PRIVATE -fsanitize=address,undefined)
endif()
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 TextAnalyst-Qt contributors
# controller: 控制层（中介者），依赖 core（BLL/DAL）与 io（持久化）
add_library(controller STATIC
  MainController.cpp
  CommandLineParser.cpp
)
target_include_directories(controller PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(controller PUBLIC core io Qt6::Core Qt6::Concurrent)
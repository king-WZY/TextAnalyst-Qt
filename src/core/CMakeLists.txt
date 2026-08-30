# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 TextAnalyst-Qt contributors
# core: BLL + DAL，Qt-independent（硬约束 §0.3-1/2）
add_library(core STATIC
  buffer/MemoryMappedFile.cpp
  buffer/EncodingDetector.cpp
  buffer/LineIndexer.cpp
  buffer/TextBuffer.cpp
  engine/ThreadPool.cpp
  engine/FilterEngine.cpp
  engine/MarkerManager.cpp
  engine/Searcher.cpp
)
target_include_directories(core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(core PUBLIC cxx_std_17)
target_link_libraries(core PUBLIC Threads::Threads)

# 头文件依赖方向：models/ ← buffer/ ← engine/（禁止反向 include，评审时检查）
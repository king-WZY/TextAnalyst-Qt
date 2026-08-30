# io: 持久化/系统集成（Qt 依赖：Xml/Concurrent），依赖 core（规则数据结构）
add_library(io STATIC
  TatSerializer.cpp
  SettingsManager.cpp
  FileWatcher.cpp
  LogRotator.cpp
)
target_include_directories(io PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(io PUBLIC core Qt6::Core Qt6::Gui Qt6::Xml)
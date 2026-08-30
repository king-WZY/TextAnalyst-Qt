# ui: 表现层（Qt Widgets），依赖 core/io/controller（§0.3 依赖方向）
add_library(ui STATIC
  widgets/LogViewModel.cpp
  widgets/LogViewDelegate.cpp
  widgets/LogListView.cpp
  widgets/FilterListModel.cpp
  widgets/FilterDockWidget.cpp
  mainwindow/MainWindow.cpp
  dialogs/FindDialog.cpp
  dialogs/FilterRuleDialog.cpp
)
target_include_directories(ui PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(ui PUBLIC core io controller
  Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent)
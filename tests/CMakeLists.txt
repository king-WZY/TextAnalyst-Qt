enable_testing()

# core 层单测：不依赖 Qt（任何有 g++ 的环境可跑）
set(CORE_TESTS
  unit/tst_buffer.cpp
  unit/tst_encoding.cpp
  unit/tst_marker.cpp
  unit/tst_filter.cpp
  unit/tst_search.cpp
)
add_executable(bench_1gb performance/bench_1gb.cpp)
target_link_libraries(bench_1gb PRIVATE core)
add_test(NAME bench_1gb COMMAND bench_1gb --size 64)

foreach(t ${CORE_TESTS})
  get_filename_component(name ${t} NAME_WE)
  add_executable(${name} ${t})
  target_link_libraries(${name} PRIVATE core)
  add_test(NAME ${name} COMMAND ${name})
endforeach()

# Qt 集成测试（QTest：.tat 序列化 / 设置 / inotify / 打开文件 / Controller 异步流）
if (Qt6_FOUND)
  find_package(Qt6 REQUIRED COMPONENTS Test)
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/tst_open_file.cpp")
    add_executable(tst_open_file integration/tst_open_file.cpp)
    target_link_libraries(tst_open_file PRIVATE core io controller Qt6::Test)
    add_test(NAME tst_open_file COMMAND tst_open_file)
    # 无显示环境（CI runner）下 Qt Widgets 需要 offscreen 平台插件
    set_tests_properties(tst_open_file PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/tst_io.cpp")
    add_executable(tst_io integration/tst_io.cpp)
    target_link_libraries(tst_io PRIVATE core io Qt6::Test)
    add_test(NAME tst_io COMMAND tst_io)
  endif()
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/integration/tst_ui.cpp")
    add_executable(tst_ui integration/tst_ui.cpp)
    target_link_libraries(tst_ui PRIVATE core io controller ui Qt6::Test)
    add_test(NAME tst_ui COMMAND tst_ui)
    set_tests_properties(tst_ui PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
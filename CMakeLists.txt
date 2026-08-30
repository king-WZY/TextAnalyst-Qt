cmake_minimum_required(VERSION 3.22)
project(textanalyst-qt VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ubuntu 22.04+ 默认 Qt 6（ARCHITECTURE.md §1.2 MUST）
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Concurrent Xml Network)

# core 层（BLL+DAL）严格零 Qt 依赖，独立静态库，可脱离 Qt 构建
enable_testing()
add_subdirectory(src)
add_subdirectory(tests)

# ===== 安装与 .deb 打包（CPack DEB 生成器，dpkg-deb 底包，无需 root）=====
if (TARGET textanalyst-qt)
  install(TARGETS textanalyst-qt DESTINATION bin)
  install(FILES packaging/deb/usr/share/applications/textanalyst-qt.desktop
          DESTINATION share/applications)
  install(FILES packaging/deb/usr/share/mime/packages/textanalyst-qt.xml
          DESTINATION share/mime/packages)
  install(FILES packaging/deb/usr/share/icons/hicolor/scalable/apps/textanalyst-qt.svg
          DESTINATION share/icons/hicolor/scalable/apps)
  install(FILES packaging/deb/usr/share/man/man1/textanalyst-qt.1
          DESTINATION share/man/man1)
  install(FILES packaging/deb/copyright
          DESTINATION share/doc/textanalyst-qt)
endif()

set(CPACK_PACKAGE_NAME "textanalyst-qt")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "textanalyst-qt project")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "High-performance text log filtering and marking tool")
set(CPACK_PACKAGE_DESCRIPTION "TextAnalyst-Qt opens multi-GiB log files via mmap with a lazy line index, applies include/exclude filter rules (substring or regex), supports 8 line markers with circular jump, and stays compatible with the original TextAnalyst (.tat) filter format.")
set(CPACK_PACKAGE_CONTACT "TextAnalyst Dev <dev@textanalyst.local>")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/king-WZY/TextAnalyst-Qt")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)   # textanalyst-qt_1.1.0_amd64.deb
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "TextAnalyst Dev <dev@textanalyst.local>")
# 22.04/24.04 双发行版兼容：t64 与非 t64 包名二选一（ARCH-UB §11.1）
set(CPACK_DEBIAN_PACKAGE_DEPENDS
  "libc6 (>= 2.35), libstdc++6 (>= 12), libgcc-s1, libqt6core6 (>= 6.4.2) | libqt6core6t64 (>= 6.4.2), libqt6gui6 (>= 6.4.2) | libqt6gui6t64 (>= 6.4.2), libqt6widgets6 (>= 6.4.2) | libqt6widgets6t64 (>= 6.4.2), libqt6concurrent6 (>= 6.4.2) | libqt6concurrent6t64 (>= 6.4.2), libqt6xml6 (>= 6.4.2) | libqt6xml6t64 (>= 6.4.2)")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "fonts-noto-cjk")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)   # 显式 Depends（见上），避免仅绑定构建机库名

include(CPack)
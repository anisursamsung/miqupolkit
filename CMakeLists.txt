cmake_minimum_required(VERSION 3.20)
project(miqupolkit LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;$ORIGIN/../lib")
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

add_definitions(-DPOLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE)

find_package(PkgConfig REQUIRED)
pkg_check_modules(MIQUTOOLKIT REQUIRED IMPORTED_TARGET miqutoolkit)
pkg_check_modules(POLKIT_AGENT REQUIRED IMPORTED_TARGET polkit-agent-1)
pkg_check_modules(POLKIT_GOBJECT REQUIRED IMPORTED_TARGET polkit-gobject-1)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-2.0)
pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0 gobject-2.0)
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(PANGO REQUIRED IMPORTED_TARGET pangocairo)

add_executable(miqupolkit
    src/main.cpp
    src/agent/polkit_listener.cpp
    src/ui/auth_dialog.cpp
)

target_include_directories(miqupolkit PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(miqupolkit PRIVATE
    PkgConfig::MIQUTOOLKIT
    PkgConfig::POLKIT_AGENT
    PkgConfig::POLKIT_GOBJECT
    PkgConfig::GIO
    PkgConfig::GLIB
    PkgConfig::CAIRO
    PkgConfig::PANGO
    stdc++fs
    pthread
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/build/libmiqutoolkit.so")
    target_link_libraries(miqupolkit PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/build/libmiqutoolkit.so")
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/include")
    target_include_directories(miqupolkit BEFORE PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../miqutoolkit/include")
endif()

target_compile_options(miqupolkit PRIVATE
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

include(GNUInstallDirs)
install(TARGETS miqupolkit RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install systemd user service
install(FILES assets/miqupolkit.service DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/user OPTIONAL)

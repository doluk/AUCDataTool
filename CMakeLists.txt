# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.21)

# vcpkg: with VCPKG_ROOT set (or the toolchain passed explicitly), configuring installs the
# dependencies listed in vcpkg.json into the build tree (manifest mode). Both must be set
# before project(). Pass -DVCPKG_MANIFEST_MODE=OFF to use classic-mode packages instead.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VCPKG_ROOT}
   AND EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "vcpkg toolchain")
endif()
if(CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake$")
    set(VCPKG_MANIFEST_MODE ON CACHE BOOL "Install dependencies from vcpkg.json")
    set(VCPKG_MANIFEST_INSTALL ON CACHE BOOL "Run vcpkg install when configuring")
endif()

project(AUCDataTool
    VERSION 0.1.0
    DESCRIPTION "Viewer for analytical ultracentrifugation raw data"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUC_BUILD_APP   "Build the Qt Quick application" ON)
if(ANDROID)
    set(_auc_host_default OFF)  # tests and tools cannot run on the build host
else()
    set(_auc_host_default ON)
endif()
option(AUC_BUILD_TESTS "Build unit tests"               ${_auc_host_default})
option(AUC_BUILD_TOOLS "Build command-line tools"       ${_auc_host_default})

find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
qt_standard_project_setup(REQUIRES 6.8)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)

if(AUC_BUILD_APP)
    add_subdirectory(app)
    if(NOT ANDROID)
        include(cmake/Packaging.cmake)
    endif()
endif()

if(AUC_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(AUC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

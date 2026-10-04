# SPDX-FileCopyrightText: 2026 Lukas Dobler
# SPDX-License-Identifier: LGPL-3.0-or-later

cmake_minimum_required(VERSION 3.21)

project(AUCDataTool
    VERSION 0.1.0
    DESCRIPTION "Viewer for analytical ultracentrifugation raw data"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUC_BUILD_APP   "Build the Qt Quick application" ON)
option(AUC_BUILD_TESTS "Build unit tests"               ON)
option(AUC_BUILD_TOOLS "Build command-line tools"       ON)

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
    include(cmake/Packaging.cmake)
endif()

if(AUC_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(AUC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

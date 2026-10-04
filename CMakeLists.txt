cmake_minimum_required(VERSION 3.21)

project(AucViewer
    VERSION 0.1.0
    DESCRIPTION "Viewer for analytical ultracentrifugation raw data"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AUC_BUILD_APP   "Build the Qt Quick application" ON)
option(AUC_BUILD_TESTS "Build unit tests"               ON)
option(AUC_BUILD_TOOLS "Build command-line tools"       ON)

# Qt 6.4 is the minimum that builds; Qt 6.8 LTS is the recommended target.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)

if(AUC_BUILD_APP)
    add_subdirectory(app)
endif()

if(AUC_BUILD_TOOLS)
    add_subdirectory(tools)
endif()

if(AUC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

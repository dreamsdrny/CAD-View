cmake_minimum_required(VERSION 3.21)
project(CADWorkbench VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Concurrent PrintSupport Test)
enable_testing()

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/io)
add_subdirectory(src/engineering)
add_subdirectory(src/ui)
add_subdirectory(tests/native)

install(TARGETS CADWorkbench RUNTIME DESTINATION bin)

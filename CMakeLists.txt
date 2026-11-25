cmake_minimum_required(VERSION 3.20)
project(rocks_maj_farmer LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Screen capture requires a Windows build target.")
endif()

add_library(screen_capture STATIC
    src/capture/screen_capture.cpp
    src/capture/screen_capture.hpp
)
target_compile_features(screen_capture PUBLIC cxx_std_17)
target_include_directories(screen_capture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(screen_capture PRIVATE d3d11 dxgi user32)

if(MSVC)
    target_compile_options(screen_capture PRIVATE /W4 /permissive-)
endif()

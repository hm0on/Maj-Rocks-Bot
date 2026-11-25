cmake_minimum_required(VERSION 3.20)
project(rocks_maj_farmer LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "Screen capture requires a Windows build target.")
endif()

# ONNX Runtime: распакованный релиз onnxruntime-win-x64-*.zip с GitHub (папки include и lib).
set(ONNXRUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime"
    CACHE PATH "Folder with the unpacked ONNX Runtime release")
if(NOT EXISTS "${ONNXRUNTIME_DIR}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "ONNX Runtime not found in ${ONNXRUNTIME_DIR}. "
        "Unpack onnxruntime-win-x64 there or pass -DONNXRUNTIME_DIR=<path>.")
endif()
add_library(onnxruntime SHARED IMPORTED)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
    IMPORTED_IMPLIB "${ONNXRUNTIME_DIR}/lib/onnxruntime.lib"
    INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_DIR}/include"
)

add_library(screen_capture STATIC
    src/capture/screen_capture.cpp
    src/capture/screen_capture.hpp
)
target_compile_features(screen_capture PUBLIC cxx_std_17)
target_include_directories(screen_capture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(screen_capture PRIVATE user32 gdi32)

add_library(detector STATIC
    src/detection/detector.cpp
    src/detection/detector.hpp
)
target_link_libraries(detector PUBLIC screen_capture onnxruntime)

add_library(mouse_controller STATIC
    src/input/mouse_controller.cpp
    src/input/mouse_controller.hpp
)
target_compile_features(mouse_controller PUBLIC cxx_std_17)
target_include_directories(mouse_controller PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(mouse_controller PRIVATE user32)

add_executable(rocks_maj_farmer src/main.cpp)
target_link_libraries(rocks_maj_farmer PRIVATE screen_capture detector mouse_controller user32)

# Кладём onnxruntime.dll рядом с exe. Иначе Windows может подхватить
# старую системную onnxruntime.dll из System32, и модель не загрузится.
add_custom_command(TARGET rocks_maj_farmer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:onnxruntime>" "$<TARGET_FILE_DIR:rocks_maj_farmer>"
)

if(MSVC)
    # /utf-8: исходники в UTF-8 (русские комментарии), без него MSVC читает их в кодировке ANSI.
    foreach(target screen_capture detector mouse_controller rocks_maj_farmer)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    endforeach()
endif()

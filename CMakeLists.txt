cmake_minimum_required(VERSION 3.10)
project(libnativedraw)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIB_HEADERS src/nativedraw.h
                src/nativedraw_private.h)
set(LIB_SOURCES src/nativedraw.cpp
                src/nativedraw_direct2d.cpp)
if (APPLE)
    list(APPEND LIB_SOURCES src/nativedraw_macos.mm)
endif()
add_library(libnativedraw ${LIB_HEADERS} ${LIB_SOURCES})

# Platform-specific
if (APPLE)
    find_library(CORE_GRAPHICS CoreGraphics)
    find_library(APPKIT AppKit)
    target_link_libraries(libnativedraw ${APPKIT} ${CORE_GRAPHICS})
elseif (WIN32)
    target_link_libraries(libnativedraw d3d11 d2d1 dwrite)
endif()

# Tests
set(TEST_SOURCES tests/main.cpp)
add_executable(test ${TEST_SOURCES})
target_link_libraries(test libnativedraw)

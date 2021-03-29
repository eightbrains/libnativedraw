cmake_minimum_required(VERSION 3.10)
project(libnativedraw)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LIB_HEADERS src/nativedraw.h
                src/nativedraw_private.h)
set(LIB_SOURCES src/nativedraw.cpp
                src/nativedraw_direct2d.cpp
                src/nativedraw_cairo.cpp)
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
elseif (UNIX)  # note that APPLE is UNIX, so need to check that first
    find_package(X11)
    find_path(CAIRO_INCLUDE_DIRS
              NAMES cairo.h
              HINTS /usr/include/cairo
                    /usr/local/include/cairo
                    /opt/include/cairo
                    /opt/local/include/cairo)
    if ("${CAIRO_INCLUDE_DIRS}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find cairo.h")
    endif()
    find_library(CAIRO_LIBRARIES NAMES cairo)
    if ("${CAIRO_LIBRARIES}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find Cairo library")
    endif()

    include_directories(${X11_INCLUDE_DIRS} ${CAIRO_INCLUDE_DIRS})
    target_link_libraries(libnativedraw ${CAIRO_LIBRARIES}
                                        ${X11_Xrender_LIB}
                                        ${X11_LIBRARIES})
endif()

add_subdirectory(tests)
add_subdirectory(timings)

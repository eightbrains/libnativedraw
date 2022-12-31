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
elseif (UNIX)
    list(APPEND LIB_SOURCES src/jpeg.cpp)
    list(APPEND LIB_SOURCES src/png.cpp)
    list(APPEND LIB_SOURCES src/gif.cpp)
endif()
add_library(nativedraw ${LIB_HEADERS} ${LIB_SOURCES})
set_target_properties(nativedraw PROPERTIES PUBLIC_HEADER src/nativedraw.h)

# Platform-specific
if (APPLE)
    target_compile_options(nativedraw PRIVATE "-Wmissing-noreturn")

    # We have to enable ARC automatically. All ObjC objects are internal, so
    # callers do not need to have ARC enabled, hence PRIVATE is sufficient.
    target_compile_options(nativedraw PRIVATE "-fobjc-arc")

    find_library(CORE_GRAPHICS CoreGraphics)
    find_library(APPKIT AppKit)
    target_link_libraries(nativedraw ${APPKIT} ${CORE_GRAPHICS})
elseif (WIN32)
    target_link_libraries(nativedraw d3d11 d2d1 dwrite)
elseif (UNIX)  # note that APPLE is UNIX, so need to check that first
    target_compile_options(nativedraw PRIVATE "-Wmissing-noreturn")
    find_package(X11)
    # Find Cairo
    find_path(CAIRO_INCLUDE_DIRS
              NAMES cairo.h
              PATH_SUFFIXES cairo)
    if ("${CAIRO_INCLUDE_DIRS}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find cairo.h")
    endif()
    find_library(CAIRO_LIBRARIES NAMES cairo)
    if ("${CAIRO_LIBRARIES}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find Cairo library")
    endif()

    # Find Pango (and GLib which it requires)
    find_path(PANGO_INCLUDE_DIRS
              NAME pango/pango.h
              PATH_SUFFIXES pango
                            pango-1.0 # Ubuntu 18.04
             )
    if ("${PANGO_INCLUDE_DIRS}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find pango.h")
    endif()
    find_path(GLIB_INCLUDE_DIR
              NAME glib.h
              PATH_SUFFIXES glib
                            glib-2.0 # Ubuntu 18.04
             )
    if ("${GLIB_INCLUDE_DIR}" MATCHES "NOTFOUND")
         message(FATAL_ERROR "Cannot find glib.h")
    endif()
    find_path(GLIBCONFIG_INCLUDE_DIR
              NAME glibconfig.h
              PATHS /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/glib/include
                    /usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}/glib-2.0/include)
    if ("${GLIBCONFIG_INCLUDE_DIR}" MATCHES "NOTFOUND")
         message(FATAL_ERROR "Cannot find glibconfig.h")
    endif()
    list(APPEND PANGO_INCLUDE_DIRS "${GLIB_INCLUDE_DIR}" "${GLIBCONFIG_INCLUDE_DIR}")
    find_library(PANGO_LIB NAMES pango pango-1.0)
    if ("${PANGO_LIB}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find Pango library")
    endif()
    find_library(PANGOCAIRO_LIB NAMES pangocairo pangocairo-1.0)
    if ("${PANGOCAIRO_LIB}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find Pango-cairo library")
    endif()
    find_library(GLIB_LIB NAMES glib glib-2.0)
    if ("${GLIB_LIB}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find GLib library")
    endif()
    find_library(GOBJECT_LIB NAMES gobject gobject-2.0)
    if ("${GOBJECT_LIB}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find GObject library")
    endif()
    set(PANGO_LIBRARIES ${PANGO_LIB} ${PANGOCAIRO_LIB} ${GLIB_LIB} ${GOBJECT_LIB})

    find_package(JPEG)
    if ("${JPEG_INCLUDE_DIRS}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find JPEG library. (We recommend libturbo-jpeg)")
    endif()
    find_package(PNG)
    if ("${PNG_INCLUDE_DIRS}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find PNG library.")
    endif()
    find_package(GIF)
    if ("${GIF_INCLUDE_DIRS}" MATCHES "NOTFOUND")
        message(FATAL_ERROR "Cannot find GIF library. (Install libgif7, libgif-dev on Ubuntu)")
    endif()

    include_directories(${X11_INCLUDE_DIRS}
                        ${PANGO_INCLUDE_DIRS}
                        ${CAIRO_INCLUDE_DIRS}
                        ${JPEG_INCLUDE_DIRS}
                        ${PNG_INCLUDE_DIRS}
                        ${GIF_INCLUDE_DIRS})
    target_link_libraries(nativedraw ${PANGO_LIBRARIES}
                                     ${CAIRO_LIBRARIES}
                                     ${X11_Xrender_LIB}
                                     ${X11_LIBRARIES}
                                     ${JPEG_LIBRARIES}
                                     ${PNG_LIBRARIES}
                                     ${GIF_LIBRARIES})
endif()

add_subdirectory(tests)
add_subdirectory(timings)

install(TARGETS nativedraw
        PUBLIC_HEADER)

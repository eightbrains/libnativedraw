set(TIMING_HEADERS timings.h)
set(TIMING_SOURCES timings.cpp)
if (APPLE)
    add_executable(timing ${TIMING_HEADERS} ${TIMING_SOURCES} macos.mm)
elseif (WIN32)
    add_executable(timing WIN32
                   ${TIMING_HEADERS} ${TIMING_SOURCES} direct2d.cpp)
elseif (EMSCRIPTEN)
    add_executable(timing ${TIMING_HEADERS} ${TIMING_SOURCES} wasm.cpp)
else()
    add_executable(timing ${TIMING_HEADERS} ${TIMING_SOURCES} cairo-x11.cpp)
endif()
target_link_libraries(timing nativedraw)

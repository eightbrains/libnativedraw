set(TEST_SOURCES main.cpp x11.cpp images.cpp)

if (EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".html")

    # Emscripten does not automatically add optimization
    if (CMAKE_MAKE_TYPE MATCHES "RELEASE")
       add_compile_options(-O3)
    endif ()
endif()

add_executable(test ${TEST_SOURCES})
target_link_libraries(test nativedraw)


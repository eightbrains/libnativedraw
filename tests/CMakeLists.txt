set(TEST_SOURCES main.cpp x11.cpp images.cpp)
add_executable(test ${TEST_SOURCES})
target_link_libraries(test nativedraw)


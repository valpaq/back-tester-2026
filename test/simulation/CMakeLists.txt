file(GLOB SIM_TEST_SRC CONFIGURE_DEPENDS *.cpp)

set(TARGET back-tester-sim-tests)
add_executable(${TARGET} ${SIM_TEST_SRC})

target_link_libraries(${TARGET} PRIVATE
    Catch2-static-lib
    Threads::Threads
    back-tester-order-book
)

catch_discover_tests(${TARGET})

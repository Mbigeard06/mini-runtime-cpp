include(GoogleTest)

add_executable(lambda_runtime_tests
    outcome_test.cpp
    invocation_test.cpp
    runtime_test.cpp
)

target_link_libraries(lambda_runtime_tests
    PRIVATE lambda_runtime
    PRIVATE nlohmann_json::nlohmann_json
    PRIVATE GTest::gtest_main
    PRIVATE GTest::gmock
)

gtest_discover_tests(lambda_runtime_tests)

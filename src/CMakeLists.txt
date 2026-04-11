add_library(lambda_runtime
    invocation.cpp
    http_client.cpp
    runtime.cpp
)

target_include_directories(lambda_runtime PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(lambda_runtime
    PUBLIC  CURL::libcurl
    PRIVATE nlohmann_json::nlohmann_json
)

target_compile_features(lambda_runtime PUBLIC cxx_std_17)

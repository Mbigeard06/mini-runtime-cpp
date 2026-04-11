add_executable(hello_world hello_world.cpp)

target_link_libraries(hello_world
    PRIVATE lambda_runtime
    PRIVATE nlohmann_json::nlohmann_json
)

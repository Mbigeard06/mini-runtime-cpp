add_executable(mock_server main.cpp mock_server.cpp)

target_include_directories(mock_server
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(mock_server
    PRIVATE nlohmann_json::nlohmann_json
)

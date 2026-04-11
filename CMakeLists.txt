cmake_minimum_required(VERSION 3.20)
project(mini_lambda_runtime VERSION 0.1.0 LANGUAGES CXX)

# ── Language standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # lets clangd resolve includes

# ── Compiler hardening ─────────────────────────────────────────────────────────
if(MSVC)
  add_compile_options(/W4 /WX /permissive-)
else()
  add_compile_options(
    -Wall -Wextra -Wpedantic -Werror
    -Wshadow -Wnon-virtual-dtor -Wold-style-cast
    -Wcast-align -Wunused -Woverloaded-virtual
    -Wconversion -Wsign-conversion
    -Wnull-dereference -Wdouble-promotion
    -Wformat=2
  )
endif()

# ── Build options ──────────────────────────────────────────────────────────────
option(BUILD_TESTS      "Build unit tests"              ON)
option(BUILD_EXAMPLES   "Build usage examples"          ON)
option(BUILD_MOCK       "Build local mock Lambda server" ON)

# ── Dependencies ───────────────────────────────────────────────────────────────
find_package(CURL REQUIRED)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

if(BUILD_TESTS)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE
  )
  # Prevent overriding the parent project's compiler/linker settings on Windows
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  # GoogleTest's own headers trigger -Wdouble-promotion. Suppress for
  # third-party targets only — our code still gets the full warning set.
  foreach(_t gtest gtest_main gmock gmock_main)
    target_compile_options(${_t} PRIVATE
      -Wno-double-promotion -Wno-conversion -Wno-sign-conversion
    )
  endforeach()
  enable_testing()
endif()

# ── Subdirectories ─────────────────────────────────────────────────────────────
add_subdirectory(src)

if(BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(BUILD_MOCK)
  add_subdirectory(mock)
endif()


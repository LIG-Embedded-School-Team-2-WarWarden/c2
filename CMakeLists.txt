cmake_minimum_required(VERSION 3.24)
project(c2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(C2_CORE_SOURCES
    C2.Core/src/attack_command_service.cpp
    C2.Core/src/connection_monitor.cpp
    C2.Core/src/dummy_assets.cpp
    C2.Core/src/effector_command_service.cpp
    C2.Core/src/observation_command_service.cpp
    C2.Core/src/pointing.cpp
    C2.Core/src/protobuf_codec.cpp
    C2.Core/src/protocol_validation.cpp
    C2.Core/src/server_runtime.cpp
    C2.Core/src/state_store.cpp
    C2.Core/src/telemetry_store.cpp
    C2.Core/src/udp_transport.cpp)

add_library(c2_core STATIC ${C2_CORE_SOURCES})
target_include_directories(c2_core PUBLIC C2.Core/include)
target_compile_definitions(c2_core PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(c2_core PUBLIC ws2_32)

add_executable(c2_server C2.Server/main.cpp)
target_link_libraries(c2_server PRIVATE c2_core)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()
file(GLOB C2_TEST_SOURCES CONFIGURE_DEPENDS C2.Tests/*_test.cpp C2.Tests/test.cpp)
add_executable(c2_tests ${C2_TEST_SOURCES})
target_include_directories(c2_tests PRIVATE C2.Tests)
target_link_libraries(c2_tests PRIVATE c2_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(c2_tests)

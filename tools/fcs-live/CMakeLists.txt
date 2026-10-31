cmake_minimum_required(VERSION 3.24)
project(fcs_live_gui LANGUAGES CXX)
add_executable(c2_fcs_gui main.cpp gui/ipc_protocol.cpp gui/ipc_server.cpp
    ../../C2.Core/src/fcs_client.cpp ../../C2.Core/src/fcs_protocol.cpp
    ../../C2.Core/src/udp_transport.cpp)
target_include_directories(c2_fcs_gui PRIVATE ../../C2.Core/include gui)
target_compile_features(c2_fcs_gui PRIVATE cxx_std_20)
target_compile_definitions(c2_fcs_gui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(c2_fcs_gui PRIVATE /utf-8)
target_link_libraries(c2_fcs_gui PRIVATE ws2_32)

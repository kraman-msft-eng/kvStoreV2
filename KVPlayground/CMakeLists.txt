cmake_minimum_required(VERSION 3.15)
project(KVPlayground VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Note: KVClient is added by root CMakeLists.txt, no need to add it again

# Find nlohmann_json
find_package(nlohmann_json CONFIG REQUIRED)

# Executable
add_executable(KVPlayground
    src/main.cpp
)

# Link against KVClient (which provides the gRPC implementation)
target_link_libraries(KVPlayground PRIVATE
    kvstore_grpc_client
    nlohmann_json::nlohmann_json
)

# Include directories
target_include_directories(KVPlayground PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../KVClient/include
)

# Compiler options for Linux
if(UNIX)
    target_compile_options(KVPlayground PRIVATE -Wall -Wextra -pthread)
    target_link_libraries(KVPlayground PRIVATE pthread)
endif()

set_target_properties(KVPlayground PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

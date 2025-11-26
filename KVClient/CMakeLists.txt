cmake_minimum_required(VERSION 3.15)
project(KVClientLibProject VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Threads REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

# Proto file
set(PROTO_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/protos/kvstore.proto
)

# Generate C++ sources from proto files
set(PROTO_SRC_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(MAKE_DIRECTORY ${PROTO_SRC_DIR})

set(PROTO_SRCS "${PROTO_SRC_DIR}/kvstore.pb.cc")
set(PROTO_HDRS "${PROTO_SRC_DIR}/kvstore.pb.h")
set(GRPC_SRCS "${PROTO_SRC_DIR}/kvstore.grpc.pb.cc")
set(GRPC_HDRS "${PROTO_SRC_DIR}/kvstore.grpc.pb.h")

add_custom_command(
    OUTPUT "${PROTO_SRCS}" "${PROTO_HDRS}" "${GRPC_SRCS}" "${GRPC_HDRS}"
    COMMAND protobuf::protoc
    ARGS --grpc_out="${PROTO_SRC_DIR}"
         --cpp_out="${PROTO_SRC_DIR}"
         -I "${CMAKE_CURRENT_SOURCE_DIR}/protos"
         --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
         "${CMAKE_CURRENT_SOURCE_DIR}/protos/kvstore.proto"
    DEPENDS "${PROTO_FILES}"
    COMMENT "Generating protobuf and gRPC C++ sources"
)

# KVClient library
add_library(kvstore_grpc_client STATIC
    src/KVStoreGrpcClient.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)

target_include_directories(kvstore_grpc_client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROTO_SRC_DIR}
)

target_link_libraries(kvstore_grpc_client PUBLIC
    gRPC::grpc++
    protobuf::libprotobuf
    Threads::Threads
)

# Compiler options for Linux
if(UNIX)
    target_compile_options(kvstore_grpc_client PRIVATE -Wall -Wextra -pthread)
endif()

# Install rules
install(TARGETS kvstore_grpc_client
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

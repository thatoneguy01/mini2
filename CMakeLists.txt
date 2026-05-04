cmake_minimum_required(VERSION 3.20)
project(mini2_basecamp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_definitions(-D_WIN32_WINNT=0x0A00)
endif()

find_package(Threads REQUIRED)
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
if(CMAKE_CROSSCOMPILING)
  find_program(_PROTOBUF_PROTOC protoc)
  find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
  set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
  set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()

get_filename_component(basecamp_proto "proto/basecamp.proto" ABSOLUTE)
get_filename_component(basecamp_proto_path "${basecamp_proto}" PATH)

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(basecamp_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/generated/basecamp.pb.cc")
set(basecamp_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/generated/basecamp.pb.h")
set(basecamp_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/generated/basecamp.grpc.pb.cc")
set(basecamp_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/generated/basecamp.grpc.pb.h")

add_custom_command(
  OUTPUT "${basecamp_proto_srcs}" "${basecamp_proto_hdrs}" "${basecamp_grpc_srcs}" "${basecamp_grpc_hdrs}"
  COMMAND ${_PROTOBUF_PROTOC}
  ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}/generated"
       --cpp_out "${CMAKE_CURRENT_BINARY_DIR}/generated"
       -I "${basecamp_proto_path}"
       --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
       "${basecamp_proto}"
  DEPENDS "${basecamp_proto}"
)

add_library(basecamp_proto
  ${basecamp_grpc_srcs}
  ${basecamp_grpc_hdrs}
  ${basecamp_proto_srcs}
  ${basecamp_proto_hdrs}
)

target_link_libraries(basecamp_proto
  PUBLIC
    gRPC::grpc++
    ${_PROTOBUF_LIBPROTOBUF}
)

target_include_directories(basecamp_proto
  PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)

add_library(basecamp_common
  cpp/common/Config.cpp
  cpp/common/CsvLibrary.cpp
  cpp/common/DobTask.cpp
  cpp/common/QueryEngine.cpp
)

target_link_libraries(basecamp_common
  PUBLIC
    basecamp_proto
)

target_include_directories(basecamp_common
  PUBLIC
    cpp/common
)

add_executable(basecamp_cpp_server
  cpp/server/node_server.cpp
)

target_link_libraries(basecamp_cpp_server
  PRIVATE
    basecamp_common
    basecamp_proto
    Threads::Threads
)

add_executable(basecamp_cpp_client
  cpp/client/basecamp_client.cpp
)

target_link_libraries(basecamp_cpp_client
  PRIVATE
    basecamp_common
    basecamp_proto
)

add_executable(query_client
  cpp/client/query_client.cpp
)

target_link_libraries(query_client
  PRIVATE
    basecamp_common
    basecamp_proto
)

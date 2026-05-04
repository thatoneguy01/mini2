# Notes:
# 1. remove old SDKs in  /Library/Developer/CommandLineTools/SDKs/
# 2. did you install gRPC? 
#
#

# create a gRPC client and server 
#
# Ref: https://github.com/grpc/grpc/blob/master/examples/cpp/helloworld/CMakeLists.txt
#

cmake_minimum_required(VERSION 4.0)
project(basic)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# enable for verbose make
#set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -v" )

if(MSVC)
  add_definitions(-D_WIN32_WINNT=0x600)
endif()

get_filename_component(basic_src_dir "src" ABSOLUTE)
message(STATUS "Source dir: ${basic_src_dir}")
#set (CMAKE_SOURCE_DIR  ${basic_src_dir}")

# Find required packages

find_package(Threads REQUIRED)

# -----------------------------------------------
# protobuf

find_package(Protobuf CONFIG REQUIRED)
option(protobuf_MODULE_COMPATIBLE TRUE)
message(STATUS "Using protobuf ${Protobuf_VERSION}")

set(_PROTOBUF_LIBPROTOBUF protobuf::libprotobuf)
set(_REFLECTION gRPC::grpc++_reflection)
if(CMAKE_CROSSCOMPILING)
    find_program(_PROTOBUF_PROTOC protoc)
else()
    set(_PROTOBUF_PROTOC $<TARGET_FILE:protobuf::protoc>)
endif()

include_directories(${Protobuf_INCLUDE_DIRS})


# -----------------------------------------------
# grpc

# your grpc installation may be in a directory that is not searched by cmake. 
# This is usually the case if you used homebrew (MacOS).

set(gRPC_DIR "/opt/homebrew/Cellar/grpc/1.76.0_1/lib/cmake/grpc")
#list(APPEND CMAKE_MODULE_PATH "/opt/homebrew/Cellar/grpc/1.76.0_1/lib/cmake")

find_package(gRPC CONFIG REQUIRED)

set(_GRPC_GRPCPP gRPC::grpc++)
if(CMAKE_CROSSCOMPILING)
    find_program(_GRPC_CPP_PLUGIN_EXECUTABLE grpc_cpp_plugin)
else()
    set(_GRPC_CPP_PLUGIN_EXECUTABLE $<TARGET_FILE:gRPC::grpc_cpp_plugin>)
endif()


# -----------------------------------------------

# note relative path doesn't work if build is called from another directory
get_filename_component(basic_proto "resources/basic.proto" ABSOLUTE)
get_filename_component(basic_proto_path "${basic_proto}" PATH)

# Generated sources
message(STATUS "generating ${CMAKE_CURRENT_BINARY_DIR}/generated/basic.pb.cc")
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(basic_proto_srcs "${CMAKE_CURRENT_BINARY_DIR}/generated/basic.pb.cc")
set(basic_proto_hdrs "${CMAKE_CURRENT_BINARY_DIR}/generated/basic.pb.h")
set(basic_grpc_srcs "${CMAKE_CURRENT_BINARY_DIR}/generated/basic.grpc.pb.cc")
set(basic_grpc_hdrs "${CMAKE_CURRENT_BINARY_DIR}/generated/basic.grpc.pb.h")
add_custom_command(
      OUTPUT "${basic_proto_srcs}" "${basic_proto_hdrs}" "${basic_grpc_srcs}" "${basic_grpc_hdrs}"
      COMMAND ${_PROTOBUF_PROTOC}
      ARGS --grpc_out "${CMAKE_CURRENT_BINARY_DIR}/generated"
        --cpp_out "${CMAKE_CURRENT_BINARY_DIR}/generated"
        -I "${basic_proto_path}"
        --plugin=protoc-gen-grpc="${_GRPC_CPP_PLUGIN_EXECUTABLE}"
        "${basic_proto}"
      DEPENDS "${basic_proto}")

# -----------------------------------------------
# Two possible ways to declare source/header files
# in a project:
#
# 1. Like here (below) a list. This works for 
#    small projects like this.
#
# 2. Larger projects - Create a CMakeLists.txt in 
#    each subdirectory (a lib is a subdirectory) or 
#    a hybrid where only in top subdirectory and 
#    have a list like below for the top libraries 
#    (a library contains multiple subdirectories).

# project sources
set(server_srcs "${basic_src_dir}/server/basicImpl.cc" 
                "${basic_src_dir}/server/server.cc")

set(server_hdrs "${basic_src_dir}/server/basicImpl.h")

set(client_srcs "${basic_src_dir}/client/client.cc")
set(client_hdrs "${basic_src_dir}/client/wrapper.h")

# search path for include file - project and generated (*.pb.h) files
include_directories("${CMAKE_CURRENT_BINARY_DIR}/generated"
                    "${basic_src_dir}")

# -----------------------------------------------

# basic_grpc_proto
add_library(basic_grpc_proto
  ${basic_grpc_srcs}
  ${basic_grpc_hdrs}
  ${basic_proto_srcs}
  ${basic_proto_hdrs})
target_link_libraries(basic_grpc_proto
  absl::check
  ${_REFLECTION}
  ${_GRPC_GRPCPP}
  ${_PROTOBUF_LIBPROTOBUF})


# server
add_executable(server
    "${server_hdrs}"
    "${server_srcs}")

target_link_libraries(server
    basic_grpc_proto
    absl::check
    absl::flags
    absl::flags_parse
    absl::log
    absl::log_initialize
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})

# client
add_executable(client
    "${client_hdrs}"
    "${client_srcs}")

target_link_libraries(client
    basic_grpc_proto
    absl::check
    absl::flags
    absl::flags_parse
    absl::log
    absl::log_initialize
    ${_REFLECTION}
    ${_GRPC_GRPCPP}
    ${_PROTOBUF_LIBPROTOBUF})


# there is no building of the python client, you can add one

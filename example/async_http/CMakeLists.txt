find_program(ASYNC_HTTP_PROTOC_EXECUTABLE protoc)
if(NOT ASYNC_HTTP_PROTOC_EXECUTABLE)
  message(FATAL_ERROR "protoc is required to build async HTTP examples")
endif()

set(ASYNC_HTTP_PROTO_FILE
    ${PROJECT_SOURCE_DIR}/proto/async_http_example.proto)
set(ASYNC_HTTP_PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ASYNC_HTTP_PROTO_SOURCE
    ${ASYNC_HTTP_PROTO_OUTPUT_DIR}/async_http_example.pb.cc)
set(ASYNC_HTTP_PROTO_HEADER
    ${ASYNC_HTTP_PROTO_OUTPUT_DIR}/async_http_example.pb.h)

add_custom_command(
  OUTPUT ${ASYNC_HTTP_PROTO_SOURCE} ${ASYNC_HTTP_PROTO_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ASYNC_HTTP_PROTO_OUTPUT_DIR}
  COMMAND ${ASYNC_HTTP_PROTOC_EXECUTABLE}
          --proto_path=${PROJECT_SOURCE_DIR}/proto
          --cpp_out=${ASYNC_HTTP_PROTO_OUTPUT_DIR}
          ${ASYNC_HTTP_PROTO_FILE}
  DEPENDS ${ASYNC_HTTP_PROTO_FILE}
  VERBATIM
)

add_library(async_http_example_proto STATIC
  ${ASYNC_HTTP_PROTO_SOURCE}
  ${ASYNC_HTTP_PROTO_HEADER}
)
target_include_directories(async_http_example_proto
  PUBLIC ${ASYNC_HTTP_PROTO_OUTPUT_DIR})
target_link_libraries(async_http_example_proto protobuf)

add_executable(async_http_backend backend.cc)
target_link_libraries(async_http_backend
  mprpc async_http_example_proto protobuf)

add_executable(async_http_server server.cc)
target_link_libraries(async_http_server
  mprpc async_http_example_proto protobuf)

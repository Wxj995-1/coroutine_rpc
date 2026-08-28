find_program(ASYNC_PROTOC_EXECUTABLE protoc)
if(NOT ASYNC_PROTOC_EXECUTABLE)
  message(FATAL_ERROR "protoc is required to build async channel examples")
endif()

set(ASYNC_PROTO_FILE ${PROJECT_SOURCE_DIR}/proto/async_example.proto)
set(ASYNC_PROTO_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ASYNC_PROTO_SOURCE ${ASYNC_PROTO_OUTPUT_DIR}/async_example.pb.cc)
set(ASYNC_PROTO_HEADER ${ASYNC_PROTO_OUTPUT_DIR}/async_example.pb.h)

add_custom_command(
  OUTPUT ${ASYNC_PROTO_SOURCE} ${ASYNC_PROTO_HEADER}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ASYNC_PROTO_OUTPUT_DIR}
  COMMAND ${ASYNC_PROTOC_EXECUTABLE}
          --proto_path=${PROJECT_SOURCE_DIR}/proto
          --cpp_out=${ASYNC_PROTO_OUTPUT_DIR}
          ${ASYNC_PROTO_FILE}
  DEPENDS ${ASYNC_PROTO_FILE}
  VERBATIM
)

add_library(async_example_proto STATIC
  ${ASYNC_PROTO_SOURCE}
  ${ASYNC_PROTO_HEADER}
)
target_include_directories(async_example_proto PUBLIC ${ASYNC_PROTO_OUTPUT_DIR})
target_link_libraries(async_example_proto protobuf)

add_executable(async_service_b service_b.cc)
target_link_libraries(async_service_b mprpc async_example_proto protobuf)

add_executable(async_service_a service_a.cc)
target_link_libraries(async_service_a mprpc async_example_proto protobuf)

add_executable(async_consumer consumer.cc)
target_link_libraries(async_consumer mprpc async_example_proto protobuf)

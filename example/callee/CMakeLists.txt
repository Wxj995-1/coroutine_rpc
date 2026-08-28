# set(SRC_LIST userservice.cc ../generated/user.pb.cc)
set(SRC_LIST friendservice.cc ../generated/friend.pb.cc)

add_executable(provider ${SRC_LIST})
target_include_directories(provider PRIVATE ${PROJECT_SOURCE_DIR}/example)
target_link_libraries(provider mprpc protobuf)

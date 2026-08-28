# set(SRC_LIST calluserservice.cc ../generated/user.pb.cc)
set(SRC_LIST callfriendservice.cc ../generated/friend.pb.cc)

add_executable(consumer ${SRC_LIST})
target_include_directories(consumer PRIVATE ${PROJECT_SOURCE_DIR}/example)
target_link_libraries(consumer mprpc protobuf)

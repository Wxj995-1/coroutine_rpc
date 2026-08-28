add_executable(rpc_codec_test rpc_codec_test.cc)
target_link_libraries(rpc_codec_test mprpc)

add_test(NAME rpc_codec_test COMMAND rpc_codec_test)

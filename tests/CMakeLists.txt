add_executable(rpc_codec_test rpc_codec_test.cc)
target_link_libraries(rpc_codec_test mprpc)

add_test(NAME rpc_codec_test COMMAND rpc_codec_test)

add_executable(process_signal_test process_signal_test.cc)
target_link_libraries(process_signal_test mprpc)

add_test(NAME process_signal_test COMMAND process_signal_test)

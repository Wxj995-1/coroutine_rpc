# 开启汇编语言支持，编译 coctx_swap.S
enable_language(ASM)

# 收集 src 下所有源文件（含子目录协程/网络/RPC模块）
file(GLOB_RECURSE SRC_LIST ${CMAKE_CURRENT_SOURCE_DIR}/*.cc ${CMAKE_CURRENT_SOURCE_DIR}/*.S)

add_library(mprpc ${SRC_LIST})
target_link_libraries(mprpc pthread zookeeper_mt protobuf dl)

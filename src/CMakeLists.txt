enable_language(ASM)

file(GLOB_RECURSE MPRPC_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cc"
  "${CMAKE_CURRENT_SOURCE_DIR}/*.S"
)

add_library(mprpc ${MPRPC_SOURCES})

target_compile_features(mprpc PUBLIC cxx_std_14)
target_compile_definitions(mprpc PRIVATE _GNU_SOURCE)
target_include_directories(mprpc PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mprpc pthread zookeeper_mt protobuf dl)

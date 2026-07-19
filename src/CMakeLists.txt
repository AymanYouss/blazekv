add_library(blazekv_core STATIC
    proto/resp.cpp
    store/object.cpp
    store/keyspace.cpp
    store/hyperloglog.cpp
    commands/registry.cpp
    commands/string_cmds.cpp
    commands/hash_cmds.cpp
    commands/list_cmds.cpp
    commands/set_cmds.cpp
    commands/zset_cmds.cpp
    commands/generic_cmds.cpp
    commands/server_cmds.cpp
    commands/vector_cmds.cpp
    vector/hnsw.cpp
    persist/aof.cpp
    persist/snapshot.cpp
    metrics/metrics.cpp
    net/socket.cpp
    net/reactor.cpp
    server/config.cpp
    server/shard.cpp
    server/server.cpp)

target_include_directories(blazekv_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated)

target_link_libraries(blazekv_core PUBLIC blazekv_flags Threads::Threads)

find_package(Threads REQUIRED)
target_link_libraries(blazekv_core PUBLIC Threads::Threads)

if(BLAZEKV_USE_MIMALLOC)
    target_link_libraries(blazekv_core PUBLIC mimalloc-static)
endif()

if(BLAZEKV_HAVE_URING)
    target_link_libraries(blazekv_core PUBLIC ${URING_LIBRARY})
    target_include_directories(blazekv_core PUBLIC ${URING_INCLUDE_DIR})
endif()

# ---------------------------------------------------------------------------
# Server executable
# ---------------------------------------------------------------------------
add_executable(blazekv-server server/main.cpp)
target_link_libraries(blazekv-server PRIVATE blazekv_core blazekv_flags)
set_target_properties(blazekv-server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

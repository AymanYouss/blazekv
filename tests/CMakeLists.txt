include(GoogleTest)

set(BLAZEKV_TESTS
    test_swiss_table
    test_skiplist
    test_arena
    test_resp
    test_hyperloglog
    test_hnsw
    test_keyspace
    test_integration)

foreach(t ${BLAZEKV_TESTS})
    add_executable(${t} ${t}.cpp)
    target_link_libraries(${t} PRIVATE blazekv_core blazekv_flags GTest::gtest_main)
    gtest_discover_tests(${t} DISCOVERY_TIMEOUT 30)
endforeach()

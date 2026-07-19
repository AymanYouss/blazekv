cmake_minimum_required(VERSION 3.20)

project(blazekv
    VERSION 0.4.0
    DESCRIPTION "A Redis-compatible, thread-per-core in-memory data store in C++20"
    LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(BLAZEKV_BUILD_TESTS      "Build unit tests"               ON)
option(BLAZEKV_BUILD_BENCH      "Build microbenchmarks"          OFF)
option(BLAZEKV_USE_MIMALLOC     "Link mimalloc as the allocator" ON)
option(BLAZEKV_USE_URING        "Use io_uring reactor on Linux"  ON)
option(BLAZEKV_ENABLE_ASAN      "Enable AddressSanitizer"        OFF)
option(BLAZEKV_NATIVE_ARCH      "Compile with -march=native"     ON)

# ---------------------------------------------------------------------------
# Global settings
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

include(CheckCXXSourceCompiles)
include(FetchContent)

# ---------------------------------------------------------------------------
# Warnings / flags
# ---------------------------------------------------------------------------
add_library(blazekv_flags INTERFACE)
target_compile_features(blazekv_flags INTERFACE cxx_std_20)
target_compile_options(blazekv_flags INTERFACE
    -Wall -Wextra -Wpedantic -Wshadow
    -Wno-unused-parameter -fno-omit-frame-pointer)

if(BLAZEKV_NATIVE_ARCH)
    check_cxx_source_compiles("int main(){return 0;}" HAVE_MARCH)
    include(CheckCXXCompilerFlag)
    check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
    if(HAS_MARCH_NATIVE)
        target_compile_options(blazekv_flags INTERFACE -march=native)
    endif()
endif()

if(BLAZEKV_ENABLE_ASAN)
    target_compile_options(blazekv_flags INTERFACE -fsanitize=address,undefined)
    target_link_options(blazekv_flags INTERFACE -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Platform feature detection
# ---------------------------------------------------------------------------
set(BLAZEKV_HAVE_URING 0)
set(BLAZEKV_HAVE_EPOLL 0)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    check_cxx_source_compiles(
        "#include <sys/epoll.h>\nint main(){return epoll_create1(0);}" HAVE_EPOLL)
    if(HAVE_EPOLL)
        set(BLAZEKV_HAVE_EPOLL 1)
    endif()
    if(BLAZEKV_USE_URING)
        find_path(URING_INCLUDE_DIR liburing.h)
        find_library(URING_LIBRARY uring)
        if(URING_INCLUDE_DIR AND URING_LIBRARY)
            set(BLAZEKV_HAVE_URING 1)
            message(STATUS "liburing found: ${URING_LIBRARY}")
        else()
            message(STATUS "liburing not found; io_uring reactor disabled")
        endif()
    endif()
endif()

# SIMD detection for the Swiss table probe (SSE2 on x86, NEON on arm64)
check_cxx_source_compiles(
    "#include <emmintrin.h>\nint main(){__m128i x=_mm_set1_epi8(0);(void)x;return 0;}"
    BLAZEKV_HAVE_SSE2)
check_cxx_source_compiles(
    "#include <arm_neon.h>\nint main(){uint8x16_t x=vdupq_n_u8(0);(void)x;return 0;}"
    BLAZEKV_HAVE_NEON)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_config.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/blazekv/build_config.hpp"
    @ONLY)

# ---------------------------------------------------------------------------
# Third-party dependencies
# ---------------------------------------------------------------------------
set(FETCHCONTENT_QUIET OFF)

if(BLAZEKV_USE_MIMALLOC)
    FetchContent_Declare(mimalloc
        GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
        GIT_TAG v2.1.7
        GIT_SHALLOW TRUE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(MI_BUILD_OBJECT OFF CACHE BOOL "" FORCE)
    set(MI_OVERRIDE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(mimalloc)
endif()

# ---------------------------------------------------------------------------
# Core library
# ---------------------------------------------------------------------------
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(BLAZEKV_BUILD_TESTS)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(BLAZEKV_BUILD_BENCH)
    FetchContent_Declare(benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.0
        GIT_SHALLOW TRUE)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(bench)
endif()

message(STATUS "BlazeKV ${PROJECT_VERSION} | uring=${BLAZEKV_HAVE_URING} "
        "epoll=${BLAZEKV_HAVE_EPOLL} sse2=${BLAZEKV_HAVE_SSE2} neon=${BLAZEKV_HAVE_NEON} "
        "mimalloc=${BLAZEKV_USE_MIMALLOC}")

#include <cstdio>
#include <cstdlib>
#include <string>

#include "blazekv/build_config.hpp"
#include "blazekv/config.hpp"
#include "blazekv/server.hpp"

using namespace blazekv;

namespace {
void print_banner(const Config& cfg, unsigned shards) {
    std::printf(
        "\n"
        "  ____  _                 _  ____   __\n"
        " | __ )| | __ _ _______  | |/ /\\ \\ / /\n"
        " |  _ \\| |/ _` |_  / _ \\ | ' /  \\ V / \n"
        " | |_) | | (_| |/ /  __/ | . \\   | |  \n"
        " |____/|_|\\__,_/___\\___| |_|\\_\\  |_|  \n"
        "\n"
        " BlazeKV %s  thread-per-core, shared-nothing\n"
        " port %u | shards %u | reactor %s | allocator %s\n\n",
        BLAZEKV_VERSION, cfg.port, shards,
        BLAZEKV_HAVE_URING ? "io_uring" : (BLAZEKV_HAVE_EPOLL ? "epoll" : "poll"),
        BLAZEKV_USE_MIMALLOC ? "mimalloc" : "libc");
}
}  // namespace

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--version" || a == "-v") {
            std::printf("BlazeKV %s\n", BLAZEKV_VERSION);
            return 0;
        }
        if (a == "--help" || a == "-h") {
            std::printf(
                "Usage: blazekv-server [config-file] [--port N] [--shards N] [--bind ADDR]\n"
                "                      [--appendonly yes|no] [--appendfsync always|everysec|no]\n"
                "                      [--dir PATH] [--maxmemory BYTES] [--reactor auto|uring|epoll|poll]\n"
                "                      [--metrics-port N]\n");
            return 0;
        }
    }

    Config cfg = Config::from_args(argc, argv);
    unsigned shards = cfg.shards ? cfg.shards : 0;

    Server server(cfg);
    std::string err;
    if (!server.start(err)) {
        std::fprintf(stderr, "blazekv: failed to start: %s\n", err.c_str());
        return 1;
    }
    print_banner(cfg, server.shard_count());
    (void)shards;
    std::printf(" ready to accept connections\n");
    std::fflush(stdout);

    server.run_until_signal();
    std::printf("\n shutting down gracefully\n");
    return 0;
}

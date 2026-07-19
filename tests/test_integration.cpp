#include <arpa/inet.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "blazekv/config.hpp"
#include "blazekv/server.hpp"

using namespace blazekv;

namespace {

// A minimal blocking RESP client for exercising the server over a real socket.
class Client {
   public:
    explicit Client(std::uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        EXPECT_EQ(::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)), 0);
        timeval tv{3, 0};
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }
    ~Client() {
        if (fd_ >= 0) ::close(fd_);
    }

    std::string cmd(const std::vector<std::string>& argv) {
        std::string out = "*" + std::to_string(argv.size()) + "\r\n";
        for (auto& a : argv) out += "$" + std::to_string(a.size()) + "\r\n" + a + "\r\n";
        ::write(fd_, out.data(), out.size());
        return read_reply();
    }

   private:
    char next_byte() {
        if (pos_ >= buf_.size()) {
            char tmp[4096];
            ssize_t n = ::read(fd_, tmp, sizeof(tmp));
            if (n <= 0) return '\0';
            buf_.assign(tmp, static_cast<std::size_t>(n));
            pos_ = 0;
        }
        return buf_[pos_++];
    }
    std::string line() {
        std::string s;
        char c;
        while ((c = next_byte()) != '\0') {
            if (c == '\r') {
                next_byte();
                break;
            }
            s += c;
        }
        return s;
    }
    std::string read_reply() {
        char t = next_byte();
        std::string head = line();
        switch (t) {
            case '+':
                return "+" + head;
            case '-':
                return "-" + head;
            case ':':
                return ":" + head;
            case '$': {
                long long n = std::stoll(head);
                if (n < 0) return "$-1";
                std::string s;
                for (long long i = 0; i < n; ++i) s += next_byte();
                next_byte();
                next_byte();  // CRLF
                return "$" + s;
            }
            case '*': {
                long long n = std::stoll(head);
                if (n < 0) return "*-1";
                std::string s = "*" + std::to_string(n);
                for (long long i = 0; i < n; ++i) s += "|" + read_reply();
                return s;
            }
        }
        return "?";
    }

    int fd_ = -1;
    std::string buf_;
    std::size_t pos_ = 0;
};

struct ServerFixture : ::testing::Test {
    std::unique_ptr<Server> server;
    std::uint16_t port = 0;

    void SetUp() override {
        Config cfg;
        port = static_cast<std::uint16_t>(20000 + (::getpid() % 20000));
        cfg.port = port;
        cfg.shards = 4;
        cfg.pin_threads = false;
        cfg.aof_enabled = false;
        cfg.snapshot_enabled = false;
        cfg.metrics_enabled = false;
        server = std::make_unique<Server>(cfg);
        std::string err;
        ASSERT_TRUE(server->start(err)) << err;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    void TearDown() override {
        if (server) server->stop();
    }
};

}  // namespace

TEST_F(ServerFixture, StringLifecycle) {
    Client c(port);
    EXPECT_EQ(c.cmd({"PING"}), "+PONG");
    EXPECT_EQ(c.cmd({"SET", "user:1", "alice"}), "+OK");
    EXPECT_EQ(c.cmd({"GET", "user:1"}), "$alice");
    EXPECT_EQ(c.cmd({"GET", "nope"}), "$-1");
    EXPECT_EQ(c.cmd({"INCR", "n"}), ":1");
    EXPECT_EQ(c.cmd({"INCRBY", "n", "10"}), ":11");
    EXPECT_EQ(c.cmd({"APPEND", "s", "ab"}), ":2");
    EXPECT_EQ(c.cmd({"STRLEN", "s"}), ":2");
}

TEST_F(ServerFixture, CrossShardMultiKey) {
    Client c(port);
    // These keys hash to different shards; MSET/MGET/DEL must fan out and reassemble.
    EXPECT_EQ(c.cmd({"MSET", "a", "1", "b", "2", "c", "3", "d", "4"}), "+OK");
    EXPECT_EQ(c.cmd({"MGET", "a", "b", "c", "d", "missing"}),
              "*5|$1|$2|$3|$4|$-1");
    EXPECT_EQ(c.cmd({"EXISTS", "a", "b", "zzz"}), ":2");
    EXPECT_EQ(c.cmd({"DEL", "a", "b", "c", "d"}), ":4");
}

TEST_F(ServerFixture, Collections) {
    Client c(port);
    EXPECT_EQ(c.cmd({"RPUSH", "l", "a", "b", "c"}), ":3");
    EXPECT_EQ(c.cmd({"LRANGE", "l", "0", "-1"}), "*3|$a|$b|$c");
    EXPECT_EQ(c.cmd({"HSET", "h", "f1", "v1", "f2", "v2"}), ":2");
    EXPECT_EQ(c.cmd({"HGET", "h", "f1"}), "$v1");
    EXPECT_EQ(c.cmd({"SADD", "st", "x", "y", "x"}), ":2");
    EXPECT_EQ(c.cmd({"SCARD", "st"}), ":2");
    EXPECT_EQ(c.cmd({"ZADD", "z", "1", "one", "2", "two"}), ":2");
    EXPECT_EQ(c.cmd({"ZSCORE", "z", "two"}), "$2");
    EXPECT_EQ(c.cmd({"ZRANK", "z", "two"}), ":1");
}

TEST_F(ServerFixture, ExpirationAndType) {
    Client c(port);
    c.cmd({"SET", "k", "v"});
    EXPECT_EQ(c.cmd({"EXPIRE", "k", "100"}), ":1");
    std::string ttl = c.cmd({"TTL", "k"});
    EXPECT_EQ(ttl[0], ':');
    EXPECT_EQ(c.cmd({"PERSIST", "k"}), ":1");
    EXPECT_EQ(c.cmd({"TTL", "k"}), ":-1");
    EXPECT_EQ(c.cmd({"TYPE", "k"}), "+string");
}

TEST_F(ServerFixture, HyperLogLogAndVector) {
    Client c(port);
    EXPECT_EQ(c.cmd({"PFADD", "hll", "a", "b", "c", "a"}), ":1");
    EXPECT_EQ(c.cmd({"PFCOUNT", "hll"}), ":3");
    EXPECT_EQ(c.cmd({"VADD", "vec", "d1", "1.0", "0.0", "0.0"}), ":1");
    EXPECT_EQ(c.cmd({"VADD", "vec", "d2", "0.0", "1.0", "0.0"}), ":1");
    EXPECT_EQ(c.cmd({"VCARD", "vec"}), ":2");
    std::string sim = c.cmd({"VSIM", "vec", "1", "0.9", "0.1", "0.0"});
    EXPECT_NE(sim.find("d1"), std::string::npos);
}

TEST_F(ServerFixture, WrongTypeAndArity) {
    Client c(port);
    c.cmd({"SET", "str", "v"});
    EXPECT_EQ(c.cmd({"LPUSH", "str", "x"}).substr(0, 10), "-WRONGTYPE");
    EXPECT_EQ(c.cmd({"GET"}).substr(0, 4), "-ERR");
    EXPECT_EQ(c.cmd({"NOSUCHCMD", "x"}).substr(0, 4), "-ERR");
}

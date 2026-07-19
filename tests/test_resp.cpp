#include "blazekv/resp.hpp"

#include <gtest/gtest.h>

#include <string>

using namespace blazekv;

TEST(Resp, ParseMultibulk) {
    RequestParser p;
    std::string in = "*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n";
    Command cmd;
    std::size_t consumed = 0;
    EXPECT_EQ(p.parse(in.data(), in.size(), cmd, consumed), ParseStatus::Ok);
    EXPECT_EQ(consumed, in.size());
    ASSERT_EQ(cmd.argv.size(), 3u);
    EXPECT_EQ(cmd.argv[0], "SET");
    EXPECT_EQ(cmd.argv[1], "foo");
    EXPECT_EQ(cmd.argv[2], "bar");
}

TEST(Resp, ParseInline) {
    RequestParser p;
    std::string in = "PING hello world\r\n";
    Command cmd;
    std::size_t consumed = 0;
    EXPECT_EQ(p.parse(in.data(), in.size(), cmd, consumed), ParseStatus::Ok);
    ASSERT_EQ(cmd.argv.size(), 3u);
    EXPECT_EQ(cmd.argv[0], "PING");
    EXPECT_EQ(cmd.argv[2], "world");
}

TEST(Resp, PartialBufferNeedsMore) {
    RequestParser p;
    std::string in = "*2\r\n$3\r\nGET\r\n$3\r\nfo";  // truncated
    Command cmd;
    std::size_t consumed = 0;
    EXPECT_EQ(p.parse(in.data(), in.size(), cmd, consumed), ParseStatus::NeedMore);
}

TEST(Resp, PipelinedCommands) {
    RequestParser p;
    std::string in = "*1\r\n$4\r\nPING\r\n*1\r\n$4\r\nPING\r\n";
    Command cmd;
    std::size_t consumed = 0;
    ASSERT_EQ(p.parse(in.data(), in.size(), cmd, consumed), ParseStatus::Ok);
    EXPECT_EQ(cmd.argv[0], "PING");
    std::size_t consumed2 = 0;
    ASSERT_EQ(p.parse(in.data() + consumed, in.size() - consumed, cmd, consumed2),
              ParseStatus::Ok);
    EXPECT_EQ(consumed + consumed2, in.size());
}

TEST(Resp, ReplyEncodings) {
    std::string buf;
    ReplyBuilder rb(buf);
    rb.simple_string("OK");
    rb.integer(42);
    rb.bulk("hi");
    rb.null_bulk();
    rb.array_header(2);
    EXPECT_EQ(buf, "+OK\r\n:42\r\n$2\r\nhi\r\n$-1\r\n*2\r\n");
}

TEST(Resp, EncodeMultibulkRoundTrip) {
    std::vector<std::string> argv = {"SET", "k", "v"};
    std::string encoded = encode_multibulk(argv);
    RequestParser p;
    Command cmd;
    std::size_t consumed = 0;
    ASSERT_EQ(p.parse(encoded.data(), encoded.size(), cmd, consumed), ParseStatus::Ok);
    ASSERT_EQ(cmd.argv.size(), 3u);
    EXPECT_EQ(cmd.argv[1], "k");
}

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "blazekv/common.hpp"

namespace blazekv {

// A single decoded client command: an array of bulk-string arguments.
// argv[0] is the (case-insensitive) command name.
struct Command {
    std::vector<std::string_view> argv;

    std::size_t size() const noexcept { return argv.size(); }
    std::string_view name() const noexcept { return argv.empty() ? std::string_view{} : argv[0]; }
    std::string_view operator[](std::size_t i) const noexcept { return argv[i]; }
};

// Result of pulling one command out of an input buffer.
enum class ParseStatus { Ok, NeedMore, ProtocolError };

// Incremental RESP request parser. Supports both the inline protocol (used by
// telnet-style clients) and the RESP array-of-bulk-strings form that every real
// client library speaks. Views point into the caller's buffer and stay valid
// until the buffer is consumed/compacted.
class RequestParser {
   public:
    // Attempts to decode one command from [data, data+len). On Ok, `consumed` is
    // set to the number of bytes eaten and `out.argv` points into `data`.
    ParseStatus parse(const char* data, std::size_t len, Command& out, std::size_t& consumed);

    std::string_view error() const noexcept { return error_; }

   private:
    ParseStatus parse_multibulk(const char* data, std::size_t len, Command& out,
                                std::size_t& consumed);
    ParseStatus parse_inline(const char* data, std::size_t len, Command& out,
                             std::size_t& consumed);
    std::string error_;
};

// RESP reply writer. Appends encoded replies onto a caller-owned output buffer so
// pipelined responses coalesce into a single writev/send.
class ReplyBuilder {
   public:
    explicit ReplyBuilder(std::string& buf) : buf_(buf) {}

    void simple_string(std::string_view s);       // +OK\r\n
    void error(std::string_view s);               // -ERR ...\r\n
    void integer(std::int64_t v);                 // :123\r\n
    void bulk(std::string_view s);                // $3\r\nfoo\r\n
    void null_bulk();                             // $-1\r\n
    void null_array();                            // *-1\r\n
    void array_header(std::int64_t n);            // *n\r\n
    void double_reply(double v);                  // bulk-encoded double
    void verbatim(std::string_view s);            // used for INFO-style payloads
    void raw(std::string_view s);                 // pre-encoded bytes

   private:
    std::string& buf_;
};

// Encode a full command as a RESP multibulk (used by AOF and replication).
std::string encode_multibulk(const std::vector<std::string_view>& argv);
std::string encode_multibulk(const std::vector<std::string>& argv);

}  // namespace blazekv

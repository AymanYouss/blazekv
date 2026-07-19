#include "blazekv/resp.hpp"

#include <charconv>
#include <cstring>

#include "blazekv/object.hpp"

namespace blazekv {
namespace {

// Finds the next CRLF starting at `pos`. Returns npos if incomplete.
std::size_t find_crlf(const char* data, std::size_t len, std::size_t pos) {
    for (; pos + 1 < len; ++pos) {
        if (data[pos] == '\r' && data[pos + 1] == '\n') return pos;
    }
    return std::string_view::npos;
}

bool to_ll(const char* p, std::size_t n, long long& out) {
    if (n == 0) return false;
    auto res = std::from_chars(p, p + n, out);
    return res.ec == std::errc{} && res.ptr == p + n;
}

void append_ll(std::string& buf, long long v) {
    char tmp[24];
    auto res = std::to_chars(tmp, tmp + sizeof(tmp), v);
    buf.append(tmp, static_cast<std::size_t>(res.ptr - tmp));
}

}  // namespace

ParseStatus RequestParser::parse(const char* data, std::size_t len, Command& out,
                                 std::size_t& consumed) {
    if (len == 0) return ParseStatus::NeedMore;
    if (data[0] == '*') return parse_multibulk(data, len, out, consumed);
    return parse_inline(data, len, out, consumed);
}

ParseStatus RequestParser::parse_multibulk(const char* data, std::size_t len, Command& out,
                                           std::size_t& consumed) {
    std::size_t pos = 0;
    std::size_t crlf = find_crlf(data, len, pos);
    if (crlf == std::string_view::npos) return ParseStatus::NeedMore;

    long long count = 0;
    if (!to_ll(data + 1, crlf - 1, count) || count < 0 || count > 1024 * 1024) {
        error_ = "invalid multibulk length";
        return ParseStatus::ProtocolError;
    }
    pos = crlf + 2;

    out.argv.clear();
    out.argv.reserve(static_cast<std::size_t>(count));
    for (long long i = 0; i < count; ++i) {
        if (pos >= len) return ParseStatus::NeedMore;
        if (data[pos] != '$') {
            error_ = "expected '$'";
            return ParseStatus::ProtocolError;
        }
        crlf = find_crlf(data, len, pos + 1);
        if (crlf == std::string_view::npos) return ParseStatus::NeedMore;
        long long blen = 0;
        if (!to_ll(data + pos + 1, crlf - pos - 1, blen) || blen < 0) {
            error_ = "invalid bulk length";
            return ParseStatus::ProtocolError;
        }
        const std::size_t start = crlf + 2;
        const std::size_t end = start + static_cast<std::size_t>(blen);
        if (end + 2 > len) return ParseStatus::NeedMore;
        out.argv.emplace_back(data + start, static_cast<std::size_t>(blen));
        pos = end + 2;  // skip trailing CRLF
    }
    consumed = pos;
    return ParseStatus::Ok;
}

ParseStatus RequestParser::parse_inline(const char* data, std::size_t len, Command& out,
                                        std::size_t& consumed) {
    std::size_t crlf = find_crlf(data, len, 0);
    std::size_t line_end;
    if (crlf == std::string_view::npos) {
        const void* nl = std::memchr(data, '\n', len);
        if (!nl) {
            if (len > 64 * 1024) {
                error_ = "inline request too long";
                return ParseStatus::ProtocolError;
            }
            return ParseStatus::NeedMore;
        }
        line_end = static_cast<std::size_t>(static_cast<const char*>(nl) - data);
        consumed = line_end + 1;
    } else {
        line_end = crlf;
        consumed = crlf + 2;
    }

    out.argv.clear();
    std::size_t i = 0;
    while (i < line_end) {
        while (i < line_end && (data[i] == ' ' || data[i] == '\t')) ++i;
        std::size_t start = i;
        while (i < line_end && data[i] != ' ' && data[i] != '\t') ++i;
        if (i > start) out.argv.emplace_back(data + start, i - start);
    }
    return ParseStatus::Ok;
}

// ---------------------------------------------------------------------------
// ReplyBuilder
// ---------------------------------------------------------------------------
void ReplyBuilder::simple_string(std::string_view s) {
    buf_.push_back('+');
    buf_.append(s);
    buf_.append("\r\n");
}

void ReplyBuilder::error(std::string_view s) {
    buf_.push_back('-');
    buf_.append(s);
    buf_.append("\r\n");
}

void ReplyBuilder::integer(std::int64_t v) {
    buf_.push_back(':');
    append_ll(buf_, v);
    buf_.append("\r\n");
}

void ReplyBuilder::bulk(std::string_view s) {
    buf_.push_back('$');
    append_ll(buf_, static_cast<long long>(s.size()));
    buf_.append("\r\n");
    buf_.append(s);
    buf_.append("\r\n");
}

void ReplyBuilder::null_bulk() { buf_.append("$-1\r\n"); }
void ReplyBuilder::null_array() { buf_.append("*-1\r\n"); }

void ReplyBuilder::array_header(std::int64_t n) {
    buf_.push_back('*');
    append_ll(buf_, n);
    buf_.append("\r\n");
}

void ReplyBuilder::double_reply(double v) { bulk(double_to_string(v)); }
void ReplyBuilder::verbatim(std::string_view s) { bulk(s); }
void ReplyBuilder::raw(std::string_view s) { buf_.append(s); }

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------
template <class Vec>
static std::string encode_impl(const Vec& argv) {
    std::string out;
    out.reserve(16 * argv.size() + 16);
    out.push_back('*');
    append_ll(out, static_cast<long long>(argv.size()));
    out.append("\r\n");
    for (const auto& a : argv) {
        out.push_back('$');
        append_ll(out, static_cast<long long>(a.size()));
        out.append("\r\n");
        out.append(a.data(), a.size());
        out.append("\r\n");
    }
    return out;
}

std::string encode_multibulk(const std::vector<std::string_view>& argv) {
    return encode_impl(argv);
}
std::string encode_multibulk(const std::vector<std::string>& argv) { return encode_impl(argv); }

}  // namespace blazekv

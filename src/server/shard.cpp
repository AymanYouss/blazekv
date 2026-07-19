#include "blazekv/shard.hpp"

#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>

#include "blazekv/command.hpp"
#include "blazekv/server.hpp"
#include "blazekv/socket.hpp"

namespace blazekv {
namespace {

constexpr std::size_t kReadChunk = 16 * 1024;
constexpr std::size_t kMaxInlineBuffer = 512u * 1024 * 1024;

void lowercase_into(std::string_view in, std::string& out) {
    out.assign(in);
    for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
}

bool arity_ok(const CommandSpec* spec, std::size_t argc) {
    if (spec->arity >= 0) return static_cast<int>(argc) == spec->arity;
    return static_cast<int>(argc) >= -spec->arity;
}

// Parses the integer out of a ":N\r\n" reply produced by a forwarded DEL/EXISTS.
long long parse_resp_integer(const std::string& reply) {
    if (reply.empty() || reply[0] != ':') return 0;
    long long v = 0;
    std::size_t i = 1;
    bool neg = false;
    if (i < reply.size() && reply[i] == '-') {
        neg = true;
        ++i;
    }
    for (; i < reply.size() && reply[i] >= '0' && reply[i] <= '9'; ++i)
        v = v * 10 + (reply[i] - '0');
    return neg ? -v : v;
}

}  // namespace

Shard::Shard(unsigned id, Server& server) : id_(id), server_(server) {}
Shard::~Shard() { join(); }

unsigned Shard::shard_for(std::string_view key) const { return server_.shard_for(key); }

Connection* Shard::find_conn(std::uint64_t id) {
    auto it = conns_.find(id);
    return it == conns_.end() ? nullptr : it->second.get();
}

bool Shard::init(int listen_fd, std::string& err) {
    listen_fd_ = listen_fd;
    reactor_ = make_reactor(server_.config().reactor);
    if (!load_persistence(err)) return false;
    if (server_.config().aof_enabled) {
        const std::string path =
            server_.config().dir + "/" + server_.config().aof_path + "." + std::to_string(id_);
        if (!aof_.open(path, server_.config().fsync, err)) return false;
    }
    metrics_.keys.store(db_.size(), std::memory_order_relaxed);
    return true;
}

bool Shard::load_persistence(std::string& err) {
    const auto& cfg = server_.config();
    if (cfg.snapshot_enabled) {
        const std::string snap =
            cfg.dir + "/" + cfg.snapshot_path + "." + std::to_string(id_);
        if (!snapshot::load(snap, db_, err)) return false;
    }
    // Replay this shard's AOF on top of the snapshot for full recovery.
    if (cfg.aof_enabled) {
        const std::string path = cfg.dir + "/" + cfg.aof_path + "." + std::to_string(id_);
        FILE* f = std::fopen(path.c_str(), "rb");
        if (f) {
            std::string data;
            char buf[65536];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
            std::fclose(f);
            RequestParser parser;
            std::size_t pos = 0;
            while (pos < data.size()) {
                Command cmd;
                std::size_t consumed = 0;
                ParseStatus st = parser.parse(data.data() + pos, data.size() - pos, cmd, consumed);
                if (st != ParseStatus::Ok) break;
                pos += consumed;
                if (!cmd.argv.empty()) {
                    std::string sink;
                    // Replay does not re-append to the AOF (aof_ not yet open).
                    exec_replay(cmd);
                    (void)sink;
                }
            }
        }
    }
    return true;
}

void Shard::start() {
    running_.store(true, std::memory_order_release);
    thread_ = std::thread([this] { run(); });
}

void Shard::stop() {
    running_.store(false, std::memory_order_release);
    waker_.notify();
}

void Shard::join() {
    if (thread_.joinable()) thread_.join();
}

void Shard::run() {
    if (server_.config().pin_threads) net::pin_to_core(id_);
    reactor_->add(listen_fd_, kRead, &listen_token_);
    reactor_->add(waker_.read_fd(), kRead, &wake_token_);
    last_cron_ms_ = now_ms();

    ReadyEvent events[512];
    while (running_.load(std::memory_order_acquire)) {
        int n = reactor_->wait(events, 512, 50);
        for (int i = 0; i < n; ++i) {
            auto* tok = static_cast<Token*>(events[i].token);
            switch (tok->kind) {
                case TokenKind::Listen:
                    on_listen_ready();
                    break;
                case TokenKind::Wake:
                    waker_.drain();
                    break;
                case TokenKind::Conn:
                    // A stale token from earlier in this batch may reference a
                    // connection already closed this iteration; skip it.
                    if (!tok->conn->dead) on_conn_ready(tok->conn, events[i].flags);
                    break;
            }
        }
        // Run any cross-shard completions posted to us by sibling shards.
        mailbox_.drain([](std::function<void()>&& fn) { fn(); });
        cron();
        // All tokens for this batch have been consumed; reclaim closed connections.
        pending_delete_.clear();
    }
    // Drain remaining work and flush persistence on shutdown.
    mailbox_.drain([](std::function<void()>&& fn) { fn(); });
    pending_delete_.clear();
    aof_.close();
}

void Shard::on_listen_ready() {
    while (true) {
        int fd = net::accept_conn(listen_fd_);
        if (fd < 0) break;  // EAGAIN: no more pending connections
        if (server_.config().tcp_nodelay) net::set_nodelay(fd, true);
        auto conn = std::make_unique<Connection>();
        conn->fd = fd;
        conn->id = next_conn_id_++;
        conn->token = Token{TokenKind::Conn, conn.get()};
        Connection* raw = conn.get();
        conns_.emplace(conn->id, std::move(conn));
        reactor_->add(fd, kRead, &raw->token);
        metrics_.connections_open.fetch_add(1, std::memory_order_relaxed);
        metrics_.connections_total.fetch_add(1, std::memory_order_relaxed);
    }
}

void Shard::on_conn_ready(Connection* c, std::uint32_t flags) {
    if (flags & kError) {
        close_conn(c);
        return;
    }
    if (flags & kRead) handle_readable(c);
    if (flags & kWrite) handle_writable(c);
    // Single close decision: only after all owed replies (including in-flight
    // cross-shard ones) have been produced and drained to the socket.
    if (c->closing && c->pipeline.empty() && c->out_sent >= c->out.size()) close_conn(c);
}

void Shard::handle_readable(Connection* c) {
    while (true) {
        std::size_t old = c->in.size();
        c->in.resize(old + kReadChunk);
        ssize_t n = ::read(c->fd, c->in.data() + old, kReadChunk);
        if (n > 0) {
            c->in.resize(old + static_cast<std::size_t>(n));
            metrics_.net_input_bytes.fetch_add(static_cast<std::uint64_t>(n),
                                               std::memory_order_relaxed);
            if (static_cast<std::size_t>(n) < kReadChunk) break;
        } else if (n == 0) {
            c->in.resize(old);
            c->closing = true;
            break;
        } else {
            c->in.resize(old);
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            c->closing = true;
            break;
        }
    }
    if (c->in.size() > kMaxInlineBuffer) c->closing = true;
    process_input(c);
    handle_writable(c);
    // Closing is decided by the caller (on_conn_ready) once nothing is owed.
}

void Shard::process_input(Connection* c) {
    std::size_t pos = 0;
    while (!c->closing) {
        Command cmd;
        std::size_t consumed = 0;
        ParseStatus st = c->parser.parse(c->in.data() + pos, c->in.size() - pos, cmd, consumed);
        if (st == ParseStatus::NeedMore) break;
        if (st == ParseStatus::ProtocolError) {
            // Enqueue the protocol error in order behind any in-flight replies.
            const std::uint64_t seq = c->next_seq++;
            std::string bytes;
            ReplyBuilder rb(bytes);
            rb.error(std::string("ERR Protocol error: ").append(c->parser.error()));
            c->pipeline[seq] = PendingReply{true, std::move(bytes), PendingReply::Kind::Single, 0,
                                            {}, 0};
            c->closing = true;
            break;
        }
        pos += consumed;
        // Each parsed command is dispatched without blocking; remote commands run
        // concurrently across shards while later pipelined commands keep flowing.
        if (!cmd.argv.empty()) dispatch(c, cmd);
    }
    if (pos > 0) c->in.erase(0, pos);
    flush_ready(c);
}

// Appends every leading ready reply (in submission order) to the output buffer.
void Shard::flush_ready(Connection* c) {
    while (!c->pipeline.empty()) {
        auto it = c->pipeline.begin();
        if (!it->second.ready) break;  // an earlier reply is still pending
        c->out.append(it->second.bytes);
        c->pipeline.erase(it);
    }
}

void Shard::handle_writable(Connection* c) {
    while (c->out_sent < c->out.size()) {
        ssize_t n = ::write(c->fd, c->out.data() + c->out_sent, c->out.size() - c->out_sent);
        if (n > 0) {
            c->out_sent += static_cast<std::size_t>(n);
            metrics_.net_output_bytes.fetch_add(static_cast<std::uint64_t>(n),
                                                std::memory_order_relaxed);
        } else {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (n < 0) c->closing = true;
            break;
        }
    }
    if (c->out_sent >= c->out.size()) {
        c->out.clear();
        c->out_sent = 0;
    }
    update_interest(c);
}

void Shard::update_interest(Connection* c) {
    std::uint32_t flags = kRead;
    if (c->out_sent < c->out.size()) flags |= kWrite;
    reactor_->mod(c->fd, flags, &c->token);
}

void Shard::close_conn(Connection* c) {
    if (c->dead) return;  // idempotent
    c->dead = true;
    reactor_->del(c->fd);
    ::close(c->fd);
    metrics_.connections_open.fetch_sub(1, std::memory_order_relaxed);
    // Park the node instead of freeing it: reactor tokens in the current batch may
    // still point at it. It is reclaimed at the end of the loop iteration.
    auto it = conns_.find(c->id);
    if (it != conns_.end()) {
        pending_delete_.push_back(std::move(it->second));
        conns_.erase(it);
    }
}

// ---------------------------------------------------------------------------
// Command routing and execution
// ---------------------------------------------------------------------------
void Shard::dispatch(Connection* c, const Command& cmd) {
    const std::uint64_t seq = c->next_seq++;
    auto local_reply = [&](auto&& fill) {
        std::string bytes;
        fill(bytes);
        c->pipeline[seq] = PendingReply{true, std::move(bytes), PendingReply::Kind::Single, 0, {}, 0};
    };

    std::string name;
    lowercase_into(cmd.name(), name);
    const CommandSpec* spec = CommandTable::instance().find(name);
    if (spec == nullptr) {
        local_reply([&](std::string& b) {
            ReplyBuilder rb(b);
            rb.error(std::string("ERR unknown command '").append(cmd.name()).append("'"));
        });
        return;
    }
    if (!arity_ok(spec, cmd.argv.size())) {
        local_reply([&](std::string& b) {
            ReplyBuilder rb(b);
            rb.error(std::string("ERR wrong number of arguments for '").append(name).append("' command"));
        });
        return;
    }

    const auto keys = extract_keys(*spec, cmd);
    if (keys.empty()) {
        local_reply([&](std::string& b) { exec_core(cmd, b); });
        return;
    }

    // Determine the distinct owning shards for this command's keys.
    unsigned first_owner = shard_for(keys.front());
    bool single = true;
    for (std::size_t i = 1; i < keys.size(); ++i) {
        if (shard_for(keys[i]) != first_owner) {
            single = false;
            break;
        }
    }

    if (single) {
        if (first_owner == id_)
            local_reply([&](std::string& b) { exec_core(cmd, b); });
        else
            begin_single_forward(c, seq, first_owner, cmd);
        return;
    }

    // Keys span multiple shards. Only a small set of commands is decomposable.
    static const std::unordered_map<std::string_view, int> decomposable = {
        {"mget", 1}, {"mset", 2}, {"del", 1}, {"unlink", 1}, {"exists", 1}, {"touch", 1}};
    if (decomposable.count(name)) {
        begin_multikey(c, seq, cmd, keys);
    } else {
        local_reply([&](std::string& b) {
            ReplyBuilder rb(b);
            rb.error("CROSSSLOT Keys in request map to different shards");
        });
    }
}

void Shard::exec_core(const Command& cmd, std::string& out) {
    ReplyBuilder rb(out);
    std::string name;
    lowercase_into(cmd.name(), name);
    const CommandSpec* spec = CommandTable::instance().find(name);
    if (spec == nullptr) {
        rb.error(std::string("ERR unknown command '").append(cmd.name()).append("'"));
        return;
    }
    if (!arity_ok(spec, cmd.argv.size())) {
        rb.error(std::string("ERR wrong number of arguments for '").append(name).append("' command"));
        return;
    }
    CommandContext ctx{cmd, rb, db_, *this};
    const std::uint64_t t0 = now_us();
    spec->fn(ctx);
    metrics_.observe(now_us() - t0);
    if (spec->write)
        metrics_.writes.fetch_add(1, std::memory_order_relaxed);
    else
        metrics_.reads.fetch_add(1, std::memory_order_relaxed);
    if (ctx.hits) metrics_.keyspace_hits.fetch_add(static_cast<std::uint64_t>(ctx.hits),
                                                   std::memory_order_relaxed);
    if (ctx.misses) metrics_.keyspace_misses.fetch_add(static_cast<std::uint64_t>(ctx.misses),
                                                       std::memory_order_relaxed);
    if (ctx.dirty && spec->write && aof_.enabled()) aof_.append(encode_multibulk(cmd.argv));
}

void Shard::exec_replay(const Command& cmd) {
    std::string sink;
    ReplyBuilder rb(sink);
    std::string name;
    lowercase_into(cmd.name(), name);
    const CommandSpec* spec = CommandTable::instance().find(name);
    if (spec == nullptr || !arity_ok(spec, cmd.argv.size())) return;
    CommandContext ctx{cmd, rb, db_, *this};
    spec->fn(ctx);
}

void Shard::execute_owned(const std::vector<std::string>& args, std::string& out) {
    Command cmd;
    cmd.argv.reserve(args.size());
    for (const auto& a : args) cmd.argv.emplace_back(a);
    exec_core(cmd, out);
}

// Forwards a whole single-owner command to its owning shard. The reply slot at
// `seq` stays pending until the owner responds; meanwhile the connection keeps
// parsing and dispatching later pipelined commands.
void Shard::begin_single_forward(Connection* c, std::uint64_t seq, unsigned target,
                                 const Command& cmd) {
    c->pipeline[seq] = PendingReply{false, {}, PendingReply::Kind::Single, 1, {std::string{}}, 0};

    std::vector<std::string> args;
    args.reserve(cmd.argv.size());
    for (auto sv : cmd.argv) args.emplace_back(sv);

    const std::uint64_t cid = c->id;
    Shard* origin = this;
    Shard* dst = &server_.shard(target);
    metrics_.cross_shard_ops.fetch_add(1, std::memory_order_relaxed);
    dst->post([dst, origin, cid, seq, args = std::move(args)]() mutable {
        std::string reply;
        dst->execute_owned(args, reply);
        origin->post([origin, cid, seq, reply = std::move(reply)]() mutable {
            origin->complete_fragment(cid, seq, 0, std::move(reply), false, 0);
        });
    });
}

// Splits a multi-key command (MGET/MSET/DEL/UNLINK/EXISTS/TOUCH) into single-key
// sub-requests routed to each key's owner. They execute in parallel across shards
// and the origin reassembles the reply once all fragments return.
void Shard::begin_multikey(Connection* c, std::uint64_t seq, const Command& cmd,
                           const std::vector<std::string_view>& keys) {
    std::string name;
    lowercase_into(cmd.name(), name);

    PendingReply::Kind kind;
    if (name == "mget")
        kind = PendingReply::Kind::MGet;
    else if (name == "mset")
        kind = PendingReply::Kind::MSet;
    else if (name == "exists")
        kind = PendingReply::Kind::ExistsCount;
    else
        kind = PendingReply::Kind::DelCount;

    const int n = static_cast<int>(keys.size());
    c->pipeline[seq] = PendingReply{false, {}, kind, n,
                                    std::vector<std::string>(static_cast<std::size_t>(n)), 0};

    const std::uint64_t cid = c->id;
    Shard* origin = this;
    metrics_.cross_shard_ops.fetch_add(static_cast<std::uint64_t>(n), std::memory_order_relaxed);

    for (int i = 0; i < n; ++i) {
        std::vector<std::string> sub;
        const bool is_int =
            (kind == PendingReply::Kind::DelCount || kind == PendingReply::Kind::ExistsCount);
        switch (kind) {
            case PendingReply::Kind::MGet:
                sub = {"GET", std::string(keys[static_cast<std::size_t>(i)])};
                break;
            case PendingReply::Kind::MSet: {
                // MSET argv layout: MSET k1 v1 k2 v2 ...; value follows each key.
                std::string_view val = cmd.argv[static_cast<std::size_t>(2 * i + 2)];
                sub = {"SET", std::string(keys[static_cast<std::size_t>(i)]), std::string(val)};
                break;
            }
            case PendingReply::Kind::ExistsCount:
                sub = {"EXISTS", std::string(keys[static_cast<std::size_t>(i)])};
                break;
            default:
                sub = {"DEL", std::string(keys[static_cast<std::size_t>(i)])};
                break;
        }
        unsigned target = shard_for(keys[static_cast<std::size_t>(i)]);
        Shard* dst = &server_.shard(target);
        dst->post([dst, origin, cid, seq, i, is_int, sub = std::move(sub)]() mutable {
            std::string reply;
            dst->execute_owned(sub, reply);
            long long iv = is_int ? parse_resp_integer(reply) : 0;
            origin->post([origin, cid, seq, i, reply = std::move(reply), is_int, iv]() mutable {
                origin->complete_fragment(cid, seq, i, std::move(reply), is_int, iv);
            });
        });
    }
}

// Assembles the final RESP bytes for a completed multi-part reply.
void Shard::assemble(PendingReply& pr) {
    std::string bytes;
    ReplyBuilder rb(bytes);
    switch (pr.kind) {
        case PendingReply::Kind::Single:
            bytes = std::move(pr.frags[0]);
            break;
        case PendingReply::Kind::MGet:
            rb.array_header(static_cast<std::int64_t>(pr.frags.size()));
            for (auto& f : pr.frags) rb.raw(f);
            break;
        case PendingReply::Kind::MSet:
            rb.simple_string("OK");
            break;
        case PendingReply::Kind::DelCount:
        case PendingReply::Kind::ExistsCount:
            rb.integer(pr.accum);
            break;
    }
    pr.bytes = std::move(bytes);
    pr.ready = true;
}

void Shard::complete_fragment(std::uint64_t cid, std::uint64_t seq, int index, std::string frag,
                              bool is_int, long long int_val) {
    Connection* c = find_conn(cid);
    if (c == nullptr) return;  // client disconnected while in flight
    auto it = c->pipeline.find(seq);
    if (it == c->pipeline.end()) return;
    PendingReply& pr = it->second;
    if (is_int)
        pr.accum += int_val;
    else
        pr.frags[static_cast<std::size_t>(index)] = std::move(frag);
    if (--pr.remaining == 0) assemble(pr);

    flush_ready(c);
    handle_writable(c);
    if (c->closing && c->pipeline.empty() && c->out_sent >= c->out.size()) close_conn(c);
}

// ---------------------------------------------------------------------------
// Periodic maintenance
// ---------------------------------------------------------------------------
void Shard::cron() {
    const std::uint64_t now = now_ms();
    const unsigned hz = std::max(1u, server_.config().active_expire_hz);
    if (now - last_cron_ms_ < 1000 / hz) return;
    last_cron_ms_ = now;

    std::vector<std::string> expired;
    std::size_t n = db_.active_expire_cycle(20, expired);
    if (n > 0) {
        metrics_.expired_keys.fetch_add(n, std::memory_order_relaxed);
        if (aof_.enabled()) {
            for (const auto& k : expired) {
                std::vector<std::string_view> del = {"DEL", k};
                aof_.append(encode_multibulk(del));
            }
        }
    }
    aof_.tick();
    metrics_.keys.store(db_.size(), std::memory_order_relaxed);
    metrics_.expires.store(db_.expires_size(), std::memory_order_relaxed);
    metrics_.memory_bytes.store(db_.approx_memory(), std::memory_order_relaxed);
}

void Shard::flush() {
    db_.clear();
    vindex_.clear();
    if (aof_.enabled()) {
        std::string err;
        const std::string path =
            server_.config().dir + "/" + server_.config().aof_path + "." + std::to_string(id_);
        aof_.reset_file(path, err);
    }
    metrics_.keys.store(0, std::memory_order_relaxed);
    metrics_.expires.store(0, std::memory_order_relaxed);
}

bool Shard::save_snapshot(std::string& err) {
    const auto& cfg = server_.config();
    const std::string snap = cfg.dir + "/" + cfg.snapshot_path + "." + std::to_string(id_);
    return snapshot::save(snap, db_, err);
}

}  // namespace blazekv

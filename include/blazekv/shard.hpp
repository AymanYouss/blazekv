#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "blazekv/hnsw.hpp"
#include "blazekv/keyspace.hpp"
#include "blazekv/metrics.hpp"
#include "blazekv/mpsc.hpp"
#include "blazekv/persist.hpp"
#include "blazekv/reactor.hpp"
#include "blazekv/resp.hpp"

namespace blazekv {

class Server;
struct Connection;

// Reactor callback tokens distinguish the listener, the cross-shard waker, and
// per-client sockets without an extra lookup.
enum class TokenKind : std::uint8_t { Listen, Wake, Conn };
struct Token {
    TokenKind kind;
    Connection* conn;
};

// One slot in a connection's reply pipeline. Local commands fill `bytes` and are
// marked ready immediately; cross-shard commands leave it pending until their
// sub-requests return. Slots are keyed by submission sequence and flushed to the
// socket strictly in order, so pipelining stays correct even though remote ops
// complete out of order and in parallel across shards.
struct PendingReply {
    enum class Kind { Single, MGet, MSet, DelCount, ExistsCount };
    bool ready = false;
    std::string bytes;
    Kind kind = Kind::Single;
    int remaining = 0;
    std::vector<std::string> frags;
    long long accum = 0;
};

struct Connection {
    int fd = -1;
    std::uint64_t id = 0;
    std::string in;
    std::string out;
    std::size_t out_sent = 0;
    RequestParser parser;
    bool want_write = false;
    bool closing = false;
    bool dead = false;  // closed but kept alive until the current event batch ends
    // Ordered reply pipeline (submission seq -> slot). std::map keeps it sorted so
    // the front is always the next reply owed to the client.
    std::map<std::uint64_t, PendingReply> pipeline;
    std::uint64_t next_seq = 0;
    Token token{TokenKind::Conn, nullptr};

    std::size_t outstanding() const { return pipeline.size(); }
};

// One shard: a shared-nothing slice of the database pinned to a core. It owns its
// keyspace, AOF, vector indexes, metrics, event loop, and client connections. The
// only cross-core interaction is via its lock-free mailbox.
class Shard {
   public:
    Shard(unsigned id, Server& server);
    ~Shard();

    unsigned id() const { return id_; }
    Keyspace& db() { return db_; }
    ShardMetrics& metrics() { return metrics_; }
    Aof& aof() { return aof_; }
    Server& server() { return server_; }
    std::unordered_map<std::string, HnswIndex>& vector_indexes() { return vindex_; }

    // Lifecycle (called by Server).
    bool init(int listen_fd, std::string& err);
    void start();          // spawns the thread and runs the loop
    void stop();           // asks the loop to exit
    void join();

    // Post work to this shard's thread from any thread.
    void post(std::function<void()> fn) {
        mailbox_.push(std::move(fn));
        waker_.notify();
    }

    // Append an encoded write command to this shard's AOF (called on its thread).
    void propagate(std::string_view encoded) { aof_.append(encoded); }

    // Executes a fully-owned command (all keys local) against this keyspace,
    // writing the RESP reply into `out`. Used by the cross-shard path.
    void execute_owned(const std::vector<std::string>& args, std::string& out);

    // Snapshot this shard's keyspace to its snapshot file (runs in a forked child).
    bool save_snapshot(std::string& err);
    bool load_persistence(std::string& err);

    // Clears all data owned by this shard and truncates its AOF (FLUSHALL/FLUSHDB).
    void flush();

    std::size_t connection_count() const { return conns_.size(); }

   private:
    friend class Server;

    void run();
    void on_listen_ready();
    void on_conn_ready(Connection* c, std::uint32_t flags);
    void handle_readable(Connection* c);
    void handle_writable(Connection* c);
    void process_input(Connection* c);
    void update_interest(Connection* c);
    void close_conn(Connection* c);

    // Command routing / execution. Each command is assigned a submission sequence
    // and dispatched without blocking the connection; replies are flushed in order.
    void dispatch(Connection* c, const Command& cmd);
    void exec_core(const Command& cmd, std::string& out);
    void exec_replay(const Command& cmd);
    void begin_single_forward(Connection* c, std::uint64_t seq, unsigned target,
                              const Command& cmd);
    void begin_multikey(Connection* c, std::uint64_t seq, const Command& cmd,
                        const std::vector<std::string_view>& keys);
    void complete_fragment(std::uint64_t cid, std::uint64_t seq, int index, std::string frag,
                           bool is_int, long long int_val);
    void assemble(PendingReply& pr);
    void flush_ready(Connection* c);

    unsigned shard_for(std::string_view key) const;
    Connection* find_conn(std::uint64_t id);

    void cron();  // periodic: active expiry, AOF fsync tick

    unsigned id_;
    Server& server_;
    Keyspace db_;
    Aof aof_;
    ShardMetrics metrics_;
    std::unordered_map<std::string, HnswIndex> vindex_;

    std::unique_ptr<Reactor> reactor_;
    Waker waker_;
    MpscQueue<std::function<void()>> mailbox_;

    std::unordered_map<std::uint64_t, std::unique_ptr<Connection>> conns_;
    // Connections closed mid-batch are parked here and freed once the batch (and
    // its cross-shard completions) finish, so a stale reactor token can never
    // dereference freed memory.
    std::vector<std::unique_ptr<Connection>> pending_delete_;
    std::uint64_t next_conn_id_ = 1;

    int listen_fd_ = -1;
    Token listen_token_{TokenKind::Listen, nullptr};
    Token wake_token_{TokenKind::Wake, nullptr};

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::uint64_t last_cron_ms_ = 0;
};

}  // namespace blazekv

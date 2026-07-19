#include <string>
#include <vector>

#include "blazekv/command.hpp"
#include "blazekv/hnsw.hpp"
#include "blazekv/object.hpp"
#include "blazekv/shard.hpp"

namespace blazekv {
namespace cmd {
namespace {

bool parse_vector(const Command& cmd, std::size_t start, std::vector<float>& out) {
    out.clear();
    for (std::size_t i = start; i < cmd.argv.size(); ++i) {
        double v = 0;
        if (!parse_double(cmd.argv[i], v)) return false;
        out.push_back(static_cast<float>(v));
    }
    return !out.empty();
}

}  // namespace

// VADD key label v1 v2 ... vn  -> inserts/updates an embedding under `label`.
void vadd(CommandContext& c) {
    std::vector<float> vec;
    if (!parse_vector(c.cmd, 3, vec)) {
        c.err_not_float();
        return;
    }
    auto& indexes = c.shard.vector_indexes();
    std::string key(c.arg(1));
    auto it = indexes.find(key);
    if (it == indexes.end()) {
        it = indexes.emplace(key, HnswIndex(vec.size(), VectorMetric::Cosine)).first;
    } else if (it->second.dim() != vec.size()) {
        c.reply.error("ERR vector dimension mismatch");
        return;
    }
    bool added = it->second.add(std::string(c.arg(2)), vec.data());
    c.mark_dirty();
    c.reply.integer(added ? 1 : 0);
}

// VSIM key count v1 v2 ... vn  -> returns the `count` nearest labels with scores.
void vsim(CommandContext& c) {
    std::int64_t count = 0;
    if (!parse_int(c.arg(2), count) || count <= 0) {
        c.err_not_int();
        return;
    }
    std::vector<float> query;
    if (!parse_vector(c.cmd, 3, query)) {
        c.err_not_float();
        return;
    }
    auto& indexes = c.shard.vector_indexes();
    auto it = indexes.find(std::string(c.arg(1)));
    if (it == indexes.end()) {
        c.reply.array_header(0);
        return;
    }
    if (it->second.dim() != query.size()) {
        c.reply.error("ERR vector dimension mismatch");
        return;
    }
    const std::size_t ef = static_cast<std::size_t>(count) * 4 + 32;
    auto results = it->second.search(query.data(), static_cast<std::size_t>(count), ef);
    c.reply.array_header(static_cast<std::int64_t>(results.size()) * 2);
    for (auto& [label, score] : results) {
        c.reply.bulk(label);
        c.reply.double_reply(static_cast<double>(score));
    }
}

void vcard(CommandContext& c) {
    auto& indexes = c.shard.vector_indexes();
    auto it = indexes.find(std::string(c.arg(1)));
    c.reply.integer(it == indexes.end() ? 0 : static_cast<std::int64_t>(it->second.size()));
}

void vrem(CommandContext& c) {
    auto& indexes = c.shard.vector_indexes();
    auto it = indexes.find(std::string(c.arg(1)));
    if (it == indexes.end()) {
        c.reply.integer(0);
        return;
    }
    bool removed = it->second.remove(std::string(c.arg(2)));
    if (removed) c.mark_dirty();
    c.reply.integer(removed ? 1 : 0);
}

}  // namespace cmd
}  // namespace blazekv

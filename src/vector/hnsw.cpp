#include "blazekv/hnsw.hpp"

#include <algorithm>
#include <cmath>
#include <queue>

namespace blazekv {

HnswIndex::HnswIndex(std::size_t dim, VectorMetric metric, std::size_t m,
                     std::size_t ef_construction)
    : dim_(dim),
      metric_(metric),
      m_(m),
      m0_(m * 2),
      ef_construction_(ef_construction) {}

std::vector<float> HnswIndex::prepare(const float* vec) const {
    std::vector<float> v(vec, vec + dim_);
    if (metric_ == VectorMetric::Cosine) {
        double norm = 0.0;
        for (float x : v) norm += static_cast<double>(x) * x;
        norm = std::sqrt(norm);
        if (norm > 0) {
            const float inv = static_cast<float>(1.0 / norm);
            for (float& x : v) x *= inv;
        }
    }
    return v;
}

float HnswIndex::distance(const float* a, const float* b) const {
    if (metric_ == VectorMetric::Cosine) {
        float dot = 0.0f;
        for (std::size_t i = 0; i < dim_; ++i) dot += a[i] * b[i];
        return 1.0f - dot;  // vectors are normalized, so this is cosine distance
    }
    float sum = 0.0f;
    for (std::size_t i = 0; i < dim_; ++i) {
        const float d = a[i] - b[i];
        sum += d * d;
    }
    return sum;  // squared L2 preserves ordering
}

int HnswIndex::random_level() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    double r = dist(rng_);
    if (r <= 0.0) r = 1e-12;
    return static_cast<int>(-std::log(r) * level_mult_);
}

// Greedy best-first expansion of `ef` candidates on a single layer.
std::vector<std::uint32_t> HnswIndex::search_layer(const float* q, std::uint32_t entry,
                                                   std::size_t ef, int level) const {
    std::vector<char> visited(nodes_.size(), 0);
    // Min-heap of candidates to expand (closest first).
    using Item = std::pair<float, std::uint32_t>;
    std::priority_queue<Item, std::vector<Item>, std::greater<>> candidates;
    // Max-heap of the current best results (farthest on top for easy eviction).
    std::priority_queue<Item> results;

    const float d0 = distance(q, nodes_[entry].vec.data());
    candidates.emplace(d0, entry);
    results.emplace(d0, entry);
    visited[entry] = 1;

    while (!candidates.empty()) {
        auto [cd, cur] = candidates.top();
        candidates.pop();
        if (cd > results.top().first && results.size() >= ef) break;
        const auto& node = nodes_[cur];
        if (level < static_cast<int>(node.links.size())) {
            for (std::uint32_t nb : node.links[static_cast<std::size_t>(level)]) {
                if (visited[nb]) continue;
                visited[nb] = 1;
                const float d = distance(q, nodes_[nb].vec.data());
                if (results.size() < ef || d < results.top().first) {
                    candidates.emplace(d, nb);
                    results.emplace(d, nb);
                    if (results.size() > ef) results.pop();
                }
            }
        }
    }

    std::vector<std::uint32_t> out;
    out.reserve(results.size());
    while (!results.empty()) {
        out.push_back(results.top().second);
        results.pop();
    }
    std::reverse(out.begin(), out.end());  // now nearest-first
    return out;
}

void HnswIndex::connect(std::uint32_t id, int level, const std::vector<std::uint32_t>& candidates) {
    const std::size_t max_links = level == 0 ? m0_ : m_;
    auto& links = nodes_[id].links[static_cast<std::size_t>(level)];
    links.clear();
    for (std::uint32_t c : candidates) {
        if (c == id) continue;
        links.push_back(c);
        if (links.size() >= max_links) break;
    }
    // Add a back-link from each neighbor, pruning it to its closest max_links.
    for (std::uint32_t c : links) {
        auto& clinks = nodes_[c].links[static_cast<std::size_t>(level)];
        clinks.push_back(id);
        if (clinks.size() > max_links) {
            const float* base = nodes_[c].vec.data();
            std::sort(clinks.begin(), clinks.end(), [&](std::uint32_t a, std::uint32_t b) {
                return distance(base, nodes_[a].vec.data()) < distance(base, nodes_[b].vec.data());
            });
            clinks.resize(max_links);
        }
    }
}

bool HnswIndex::add(const std::string& label, const float* vec) {
    auto it = labels_.find(label);
    if (it != labels_.end()) {
        nodes_[it->second].vec = prepare(vec);
        nodes_[it->second].deleted = false;
        return false;
    }

    const auto id = static_cast<std::uint32_t>(nodes_.size());
    const int level = random_level();
    Node node;
    node.label = label;
    node.vec = prepare(vec);
    node.links.resize(static_cast<std::size_t>(level) + 1);
    nodes_.push_back(std::move(node));
    labels_.emplace(label, id);

    if (entry_point_ < 0) {
        entry_point_ = id;
        max_level_ = level;
        return true;
    }

    auto cur = static_cast<std::uint32_t>(entry_point_);
    const float* q = nodes_[id].vec.data();
    // Descend from the top layer to just above the new node's level.
    for (int lvl = max_level_; lvl > level; --lvl) {
        auto nearest = search_layer(q, cur, 1, lvl);
        if (!nearest.empty()) cur = nearest.front();
    }
    // Insert into every layer from min(level, max_level) down to 0.
    for (int lvl = std::min(level, max_level_); lvl >= 0; --lvl) {
        auto candidates = search_layer(q, cur, ef_construction_, lvl);
        connect(id, lvl, candidates);
        if (!candidates.empty()) cur = candidates.front();
    }
    if (level > max_level_) {
        max_level_ = level;
        entry_point_ = id;
    }
    return true;
}

bool HnswIndex::remove(const std::string& label) {
    auto it = labels_.find(label);
    if (it == labels_.end()) return false;
    // Tombstone the node: it stays in the graph for connectivity but is filtered
    // out of results. A background rebuild would compact these; for a cache this
    // lazy scheme keeps deletes O(1).
    nodes_[it->second].deleted = true;
    labels_.erase(it);
    return true;
}

std::vector<std::pair<std::string, float>> HnswIndex::search(const float* query, std::size_t k,
                                                             std::size_t ef) const {
    std::vector<std::pair<std::string, float>> out;
    if (entry_point_ < 0 || k == 0) return out;

    const std::vector<float> q = prepare(query);
    auto cur = static_cast<std::uint32_t>(entry_point_);
    for (int lvl = max_level_; lvl > 0; --lvl) {
        auto nearest = search_layer(q.data(), cur, 1, lvl);
        if (!nearest.empty()) cur = nearest.front();
    }
    auto found = search_layer(q.data(), cur, std::max(ef, k), 0);

    for (std::uint32_t id : found) {
        if (nodes_[id].deleted) continue;
        const float d = distance(q.data(), nodes_[id].vec.data());
        const float score = metric_ == VectorMetric::Cosine ? 1.0f - d : -std::sqrt(d);
        out.emplace_back(nodes_[id].label, score);
        if (out.size() >= k) break;
    }
    return out;
}

}  // namespace blazekv

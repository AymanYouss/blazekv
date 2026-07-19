#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace blazekv {

enum class VectorMetric { Cosine, L2 };

// A Hierarchical Navigable Small World graph for approximate nearest-neighbor
// search. This is what lets BlazeKV act as a semantic-cache backend: VADD inserts
// an embedding under a label, VSIM returns the closest labels to a query vector in
// logarithmic expected time. The implementation follows Malkov & Yashunin (2016).
class HnswIndex {
   public:
    HnswIndex() = default;
    HnswIndex(std::size_t dim, VectorMetric metric, std::size_t m = 16,
              std::size_t ef_construction = 200);

    std::size_t dim() const { return dim_; }
    std::size_t size() const { return labels_.size(); }
    VectorMetric metric() const { return metric_; }

    // Inserts or replaces the vector stored under `label`. Returns true if the
    // label was newly added. `vec` must have exactly dim() elements.
    bool add(const std::string& label, const float* vec);

    bool remove(const std::string& label);

    // Returns up to k (label, score) pairs ordered best-first. For cosine the score
    // is similarity in [-1,1]; for L2 it is the (negated) distance.
    std::vector<std::pair<std::string, float>> search(const float* query, std::size_t k,
                                                      std::size_t ef) const;

   private:
    struct Node {
        std::string label;
        std::vector<float> vec;                       // normalized for cosine
        std::vector<std::vector<std::uint32_t>> links;  // neighbors per level
        bool deleted = false;
    };

    float distance(const float* a, const float* b) const;
    int random_level();
    std::vector<std::uint32_t> search_layer(const float* q, std::uint32_t entry, std::size_t ef,
                                            int level) const;
    void connect(std::uint32_t id, int level, const std::vector<std::uint32_t>& candidates);
    std::vector<float> prepare(const float* vec) const;

    std::size_t dim_ = 0;
    VectorMetric metric_ = VectorMetric::Cosine;
    std::size_t m_ = 16;         // max neighbors per node (per upper layer)
    std::size_t m0_ = 32;        // max neighbors at layer 0
    std::size_t ef_construction_ = 200;
    double level_mult_ = 1.0 / 0.6931471805599453;  // 1/ln(2)

    std::vector<Node> nodes_;
    std::unordered_map<std::string, std::uint32_t> labels_;
    std::int64_t entry_point_ = -1;
    int max_level_ = -1;
    std::mt19937 rng_{0xC0FFEE};
};

}  // namespace blazekv

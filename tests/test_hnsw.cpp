#include "blazekv/hnsw.hpp"

#include <gtest/gtest.h>

#include <random>
#include <vector>

using namespace blazekv;

TEST(Hnsw, ExactNearestOnCanonicalBasis) {
    HnswIndex index(3, VectorMetric::Cosine);
    float e0[] = {1, 0, 0};
    float e1[] = {0, 1, 0};
    float e2[] = {0, 0, 1};
    EXPECT_TRUE(index.add("x", e0));
    EXPECT_TRUE(index.add("y", e1));
    EXPECT_TRUE(index.add("z", e2));
    EXPECT_EQ(index.size(), 3u);

    float q[] = {0.9f, 0.1f, 0.0f};
    auto res = index.search(q, 1, 16);
    ASSERT_FALSE(res.empty());
    EXPECT_EQ(res.front().first, "x");
}

TEST(Hnsw, RecallOnRandomVectors) {
    const std::size_t dim = 32;
    const int n = 2000;
    HnswIndex index(dim, VectorMetric::L2);
    std::mt19937 rng(123);
    std::normal_distribution<float> nd(0, 1);
    std::vector<std::vector<float>> data(n, std::vector<float>(dim));
    for (int i = 0; i < n; ++i) {
        for (auto& v : data[i]) v = nd(rng);
        index.add("v" + std::to_string(i), data[i].data());
    }
    // Query with an existing vector: it should be its own nearest neighbor.
    int hits = 0;
    for (int trial = 0; trial < 100; ++trial) {
        int idx = trial * 7 % n;
        auto res = index.search(data[idx].data(), 1, 64);
        if (!res.empty() && res.front().first == "v" + std::to_string(idx)) ++hits;
    }
    EXPECT_GE(hits, 95);  // >=95% recall@1 for self-queries
}

TEST(Hnsw, RemoveTombstonesResults) {
    HnswIndex index(2, VectorMetric::Cosine);
    float a[] = {1, 0};
    float b[] = {0, 1};
    index.add("a", a);
    index.add("b", b);
    EXPECT_TRUE(index.remove("a"));
    float q[] = {1, 0};
    auto res = index.search(q, 2, 16);
    for (auto& [label, score] : res) EXPECT_NE(label, "a");
}

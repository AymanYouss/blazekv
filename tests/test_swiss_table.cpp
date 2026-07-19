#include "blazekv/swiss_table.hpp"

#include <gtest/gtest.h>

#include <string>
#include <unordered_set>

using blazekv::SwissTable;

TEST(SwissTable, InsertFindErase) {
    SwissTable<std::string, int> t;
    EXPECT_TRUE(t.empty());
    auto [v, inserted] = t.insert_or_assign("a", 1);
    EXPECT_TRUE(inserted);
    EXPECT_EQ(*v, 1);
    EXPECT_EQ(t.size(), 1u);

    int* found = t.find("a");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(*found, 1);
    EXPECT_EQ(t.find("missing"), nullptr);

    EXPECT_TRUE(t.erase("a"));
    EXPECT_FALSE(t.erase("a"));
    EXPECT_EQ(t.find("a"), nullptr);
    EXPECT_TRUE(t.empty());
}

TEST(SwissTable, Overwrite) {
    SwissTable<std::string, int> t;
    t.insert_or_assign("k", 1);
    auto [v, inserted] = t.insert_or_assign("k", 42);
    EXPECT_FALSE(inserted);
    EXPECT_EQ(*v, 42);
    EXPECT_EQ(t.size(), 1u);
}

TEST(SwissTable, GrowthAndIntegrity) {
    SwissTable<std::string, int> t;
    constexpr int kN = 20000;
    for (int i = 0; i < kN; ++i) t.insert_or_assign("key:" + std::to_string(i), i);
    EXPECT_EQ(t.size(), static_cast<size_t>(kN));
    for (int i = 0; i < kN; ++i) {
        int* v = t.find("key:" + std::to_string(i));
        ASSERT_NE(v, nullptr) << i;
        EXPECT_EQ(*v, i);
    }
    // Erase evens, verify odds survive.
    for (int i = 0; i < kN; i += 2) EXPECT_TRUE(t.erase("key:" + std::to_string(i)));
    EXPECT_EQ(t.size(), static_cast<size_t>(kN / 2));
    for (int i = 1; i < kN; i += 2) {
        int* v = t.find("key:" + std::to_string(i));
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(*v, i);
    }
}

TEST(SwissTable, ForEachVisitsAll) {
    SwissTable<std::string, int> t;
    std::unordered_set<std::string> expected;
    for (int i = 0; i < 500; ++i) {
        t.insert_or_assign("m" + std::to_string(i), i);
        expected.insert("m" + std::to_string(i));
    }
    std::unordered_set<std::string> seen;
    t.for_each([&](const std::string& k, int&) { seen.insert(k); });
    EXPECT_EQ(seen, expected);
}

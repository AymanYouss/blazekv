#include "blazekv/skiplist.hpp"

#include <gtest/gtest.h>

#include <string>

using blazekv::SkipList;

TEST(SkipList, OrderedInsertAndRank) {
    SkipList sl;
    sl.insert("b", 2.0);
    sl.insert("a", 1.0);
    sl.insert("c", 3.0);
    EXPECT_EQ(sl.size(), 3u);

    EXPECT_EQ(sl.rank_of("a", 1.0), 1u);
    EXPECT_EQ(sl.rank_of("b", 2.0), 2u);
    EXPECT_EQ(sl.rank_of("c", 3.0), 3u);

    auto* n = sl.at_rank(1);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->member, "a");
    EXPECT_EQ(sl.at_rank(3)->member, "c");
}

TEST(SkipList, TieBreakByMember) {
    SkipList sl;
    sl.insert("bravo", 1.0);
    sl.insert("alpha", 1.0);
    sl.insert("charlie", 1.0);
    // Equal scores order lexicographically by member.
    EXPECT_EQ(sl.at_rank(1)->member, "alpha");
    EXPECT_EQ(sl.at_rank(2)->member, "bravo");
    EXPECT_EQ(sl.at_rank(3)->member, "charlie");
}

TEST(SkipList, EraseMaintainsOrder) {
    SkipList sl;
    for (int i = 0; i < 100; ++i) sl.insert("m" + std::to_string(i), static_cast<double>(i));
    EXPECT_TRUE(sl.erase("m50", 50.0));
    EXPECT_FALSE(sl.erase("m50", 50.0));
    EXPECT_EQ(sl.size(), 99u);
    // Ranks below the removed element are unchanged; ranks above shift down by one.
    EXPECT_EQ(sl.rank_of("m49", 49.0), 50u);
    EXPECT_EQ(sl.rank_of("m51", 51.0), 51u);
}

TEST(SkipList, RangeScanFromScore) {
    SkipList sl;
    for (int i = 0; i < 10; ++i) sl.insert("m" + std::to_string(i), static_cast<double>(i));
    auto* n = sl.first_gte(5.0);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->score, 5.0);
    int count = 0;
    for (; n != nullptr; n = n->level[0].forward) ++count;
    EXPECT_EQ(count, 5);
}

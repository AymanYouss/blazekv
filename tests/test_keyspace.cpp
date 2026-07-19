#include "blazekv/keyspace.hpp"

#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "blazekv/object.hpp"

using namespace blazekv;

TEST(Keyspace, SetLookupErase) {
    Keyspace ks;
    ks.set("k", Object(std::string("v")));
    Object* o = ks.lookup("k");
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->str(), "v");
    EXPECT_TRUE(ks.erase("k"));
    EXPECT_EQ(ks.lookup("k"), nullptr);
}

TEST(Keyspace, LazyExpiration) {
    Keyspace ks;
    ks.set("k", Object(std::string("v")));
    ks.set_deadline("k", now_ms() - 1);  // already expired
    EXPECT_EQ(ks.lookup("k"), nullptr);  // lazily removed on access
    EXPECT_EQ(ks.size(), 0u);
}

TEST(Keyspace, TtlReporting) {
    Keyspace ks;
    ks.set("k", Object(std::string("v")));
    EXPECT_EQ(ks.ttl_ms("k"), -1);       // no expire
    EXPECT_EQ(ks.ttl_ms("missing"), -2);  // no key
    ks.set_deadline("k", now_ms() + 5000);
    std::int64_t ttl = ks.ttl_ms("k");
    EXPECT_GT(ttl, 4000);
    EXPECT_LE(ttl, 5000);
}

TEST(Keyspace, ActiveExpireCycle) {
    Keyspace ks;
    for (int i = 0; i < 50; ++i) {
        ks.set("k" + std::to_string(i), Object(std::string("v")));
        ks.set_deadline("k" + std::to_string(i), now_ms() - 1);
    }
    ks.set("survivor", Object(std::string("v")));
    std::vector<std::string> expired;
    std::size_t n = ks.active_expire_cycle(1000, expired);
    EXPECT_EQ(n, 50u);
    EXPECT_EQ(expired.size(), 50u);
    EXPECT_NE(ks.lookup("survivor"), nullptr);
}

TEST(Keyspace, GetOrCreate) {
    Keyspace ks;
    Object& o = ks.get_or_create("h", [] { return Object::make_hash(); });
    o.hash().insert_or_assign("f", "v");
    EXPECT_EQ(ks.lookup("h")->hash().size(), 1u);
    // Second call returns the existing object.
    Object& o2 = ks.get_or_create("h", [] { return Object::make_hash(); });
    EXPECT_EQ(o2.hash().size(), 1u);
}

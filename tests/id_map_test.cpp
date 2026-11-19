#include "lob/id_map.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <random>
#include <unordered_map>

using lob::IdMap;

TEST(IdMap, InsertFindAndDuplicate) {
  IdMap m;
  EXPECT_TRUE(m.insert(1, 10));
  EXPECT_FALSE(m.insert(1, 99));
  ASSERT_NE(m.find(1), nullptr);
  EXPECT_EQ(*m.find(1), 10u);
  EXPECT_EQ(m.find(2), nullptr);
  EXPECT_EQ(m.size(), 1u);
}

TEST(IdMap, EraseMissingReturnsFalse) {
  IdMap m;
  EXPECT_FALSE(m.erase(5));
  EXPECT_EQ(m.size(), 0u);
}

TEST(IdMap, GrowsAndKeepsEverything) {
  IdMap m(4);
  for (std::uint64_t k = 1; k <= 1000; ++k) {
    ASSERT_TRUE(m.insert(k, static_cast<std::uint32_t>(k)));
  }
  EXPECT_EQ(m.size(), 1000u);
  EXPECT_LE(m.size() * 2, m.capacity());
  for (std::uint64_t k = 1; k <= 1000; ++k) {
    ASSERT_NE(m.find(k), nullptr);
    EXPECT_EQ(*m.find(k), static_cast<std::uint32_t>(k));
  }
}

TEST(IdMap, EraseAllLeavesEmptyTableWithoutTombstones) {
  IdMap m(8);
  for (std::uint64_t k = 1; k <= 100; ++k) ASSERT_TRUE(m.insert(k, 1));
  for (std::uint64_t k = 1; k <= 100; ++k) EXPECT_TRUE(m.erase(k));
  EXPECT_EQ(m.size(), 0u);
  for (std::uint64_t k = 1; k <= 100; ++k) EXPECT_EQ(m.find(k), nullptr);
  EXPECT_TRUE(m.insert(7, 70));
  EXPECT_EQ(*m.find(7), 70u);
}

TEST(IdMap, DifferentialAgainstUnorderedMap) {
  IdMap m;
  std::unordered_map<std::uint64_t, std::uint32_t> ref;
  std::mt19937_64 rng(12345);
  std::uniform_int_distribution<std::uint64_t> key_dist(1, 2000);
  std::uniform_int_distribution<int> op_dist(0, 99);
  for (int step = 0; step < 1000000; ++step) {
    const std::uint64_t k = key_dist(rng);
    const int op = op_dist(rng);
    if (op < 50) {
      const auto v = static_cast<std::uint32_t>(step);
      const bool got = m.insert(k, v);
      const bool want = ref.emplace(k, v).second;
      ASSERT_EQ(got, want) << "insert " << k << " at step " << step;
    } else if (op < 80) {
      const bool got = m.erase(k);
      const bool want = ref.erase(k) == 1;
      ASSERT_EQ(got, want) << "erase " << k << " at step " << step;
    } else {
      const std::uint32_t* got = m.find(k);
      const auto it = ref.find(k);
      ASSERT_EQ(got != nullptr, it != ref.end()) << "find " << k << " at step " << step;
      if (got != nullptr) {
        ASSERT_EQ(*got, it->second);
      }
    }
    ASSERT_EQ(m.size(), ref.size());
  }
  for (const auto& [k, v] : ref) {
    const std::uint32_t* got = m.find(k);
    ASSERT_NE(got, nullptr);
    ASSERT_EQ(*got, v);
  }
}

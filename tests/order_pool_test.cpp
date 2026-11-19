#include "lob/order_pool.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <vector>

using lob::kNullIndex;
using lob::OrderPool;
using lob::PoolIndex;

TEST(OrderPool, EmptyPoolHasFullCapacityAndNothingInUse) {
  OrderPool pool(8);
  EXPECT_EQ(pool.capacity(), 8u);
  EXPECT_EQ(pool.in_use(), 0u);
}

TEST(OrderPool, ZeroCapacityAllocReturnsNull) {
  OrderPool pool(0);
  EXPECT_EQ(pool.alloc(), kNullIndex);
}

TEST(OrderPool, FillToCapacityThenReturnsNull) {
  OrderPool pool(4);
  std::set<PoolIndex> seen;
  for (int k = 0; k < 4; ++k) {
    const PoolIndex i = pool.alloc();
    ASSERT_NE(i, kNullIndex);
    EXPECT_LT(i, 4u);
    EXPECT_TRUE(seen.insert(i).second) << "index handed out twice: " << i;
    EXPECT_TRUE(pool.is_live(i));
  }
  EXPECT_EQ(pool.in_use(), 4u);
  EXPECT_EQ(pool.alloc(), kNullIndex);
  EXPECT_EQ(pool.in_use(), 4u);
}

TEST(OrderPool, FreeAndReuseIsLifo) {
  OrderPool pool(2);
  const PoolIndex a = pool.alloc();
  const PoolIndex b = pool.alloc();
  ASSERT_EQ(pool.alloc(), kNullIndex);
  EXPECT_TRUE(pool.release(a));
  EXPECT_FALSE(pool.is_live(a));
  EXPECT_EQ(pool.in_use(), 1u);
  const PoolIndex c = pool.alloc();
  EXPECT_EQ(c, a);
  EXPECT_NE(c, b);
  EXPECT_EQ(pool.in_use(), 2u);
}

TEST(OrderPool, ReusedSlotIsReset) {
  OrderPool pool(1);
  const PoolIndex a = pool.alloc();
  pool[a].id = 42;
  pool[a].total = 10;
  EXPECT_TRUE(pool.release(a));
  const PoolIndex b = pool.alloc();
  EXPECT_EQ(b, a);
  EXPECT_EQ(pool[b].id, 0u);
  EXPECT_EQ(pool[b].total, 0);
  EXPECT_TRUE(pool[b].live);
}

TEST(OrderPool, ReleaseRejectsDoubleFreeAndInvalidIndex) {
  OrderPool pool(2);
  const PoolIndex a = pool.alloc();
  EXPECT_TRUE(pool.release(a));
  EXPECT_FALSE(pool.release(a));           // double free
  EXPECT_FALSE(pool.release(2));           // out of range
  EXPECT_FALSE(pool.release(kNullIndex));  // null index
  EXPECT_EQ(pool.in_use(), 0u);
}

TEST(OrderPool, ChurnKeepsCountsConsistent) {
  OrderPool pool(16);
  std::vector<PoolIndex> live;
  std::uint32_t seed = 1;
  for (int step = 0; step < 10000; ++step) {
    seed = seed * 1103515245u + 12345u;
    if (live.size() < 16 && (seed >> 16) % 2 == 0) {
      const PoolIndex i = pool.alloc();
      ASSERT_NE(i, kNullIndex);
      live.push_back(i);
    } else if (!live.empty()) {
      const std::size_t k = (seed >> 8) % live.size();
      ASSERT_TRUE(pool.release(live[k]));
      live[k] = live.back();
      live.pop_back();
    }
    ASSERT_EQ(pool.in_use(), live.size());
  }
}

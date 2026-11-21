#include "lob/price_levels.hpp"

#include <gtest/gtest.h>

#include <optional>

using lob::kNullIndex;
using lob::OrderPool;
using lob::PoolIndex;
using lob::Price;
using lob::PriceLevels;
using lob::Qty;

namespace {

PoolIndex make(OrderPool& pool, Price px, Qty total, Qty filled = 0) {
  const PoolIndex i = pool.alloc();
  pool[i].price = px;
  pool[i].total = total;
  pool[i].filled = filled;
  return i;
}

}  // namespace

TEST(PriceLevels, EmptySideHasNoBest) {
  PriceLevels bids(90, 110, true);
  PriceLevels asks(90, 110, false);
  EXPECT_FALSE(bids.best().has_value());
  EXPECT_FALSE(asks.best().has_value());
}

TEST(PriceLevels, BidBestIsHighestAndAskBestIsLowest) {
  OrderPool pool(8);
  PriceLevels bids(90, 110, true);
  PriceLevels asks(90, 110, false);
  const PoolIndex b1 = make(pool, 95, 1);
  const PoolIndex b2 = make(pool, 100, 1);
  const PoolIndex a1 = make(pool, 108, 1);
  const PoolIndex a2 = make(pool, 102, 1);
  bids.push_back(pool, 95, b1);
  bids.push_back(pool, 100, b2);
  asks.push_back(pool, 108, a1);
  asks.push_back(pool, 102, a2);
  ASSERT_TRUE(bids.best());
  EXPECT_EQ(*bids.best(), 100);
  ASSERT_TRUE(asks.best());
  EXPECT_EQ(*asks.best(), 102);
}

TEST(PriceLevels, LevelTotalCountsLeavesNotTotal) {
  OrderPool pool(2);
  PriceLevels bids(90, 110, true);
  const PoolIndex a = make(pool, 100, 10, 4);
  bids.push_back(pool, 100, a);
  EXPECT_EQ(bids.level(100).total, 6);
  EXPECT_EQ(bids.level(100).count, 1u);
}

TEST(PriceLevels, EmptyingLevelRefreshesBest) {
  OrderPool pool(8);
  PriceLevels bids(90, 110, true);
  const PoolIndex a = make(pool, 100, 10);
  const PoolIndex b = make(pool, 100, 5);
  const PoolIndex c = make(pool, 105, 7);
  bids.push_back(pool, 100, a);
  bids.push_back(pool, 100, b);
  bids.push_back(pool, 105, c);
  EXPECT_EQ(bids.level(100).total, 15);
  EXPECT_EQ(bids.level(100).count, 2u);
  ASSERT_TRUE(bids.best());
  EXPECT_EQ(*bids.best(), 105);

  bids.unlink(pool, 105, c);
  ASSERT_TRUE(bids.best());
  EXPECT_EQ(*bids.best(), 100);

  bids.unlink(pool, 100, a);
  EXPECT_EQ(bids.level(100).total, 5);
  EXPECT_EQ(bids.level(100).head, b);

  bids.unlink(pool, 100, b);
  EXPECT_FALSE(bids.best().has_value());
  EXPECT_EQ(bids.level(100).count, 0u);
  EXPECT_EQ(bids.level(100).head, kNullIndex);
  EXPECT_EQ(bids.level(100).total, 0);
}

TEST(PriceLevels, RefillAfterEmptyingRestoresBest) {
  OrderPool pool(4);
  PriceLevels asks(90, 110, false);
  const PoolIndex a = make(pool, 103, 2);
  asks.push_back(pool, 103, a);
  asks.unlink(pool, 103, a);
  EXPECT_FALSE(asks.best().has_value());
  const PoolIndex b = make(pool, 103, 3);
  asks.push_back(pool, 103, b);
  ASSERT_TRUE(asks.best());
  EXPECT_EQ(*asks.best(), 103);
}

TEST(PriceLevels, UnlinkMiddleKeepsFifoLinks) {
  OrderPool pool(4);
  PriceLevels bids(90, 110, true);
  const PoolIndex a = make(pool, 100, 1);
  const PoolIndex b = make(pool, 100, 2);
  const PoolIndex c = make(pool, 100, 3);
  bids.push_back(pool, 100, a);
  bids.push_back(pool, 100, b);
  bids.push_back(pool, 100, c);
  bids.unlink(pool, 100, b);
  EXPECT_EQ(pool[a].next, c);
  EXPECT_EQ(pool[c].prev, a);
  EXPECT_EQ(bids.level(100).head, a);
  EXPECT_EQ(bids.level(100).tail, c);
  EXPECT_EQ(bids.level(100).total, 4);
  EXPECT_EQ(bids.level(100).count, 2u);
}

TEST(PriceLevels, BandEndsAreUsable) {
  OrderPool pool(4);
  PriceLevels bids(0, 4095, true);
  PriceLevels asks(0, 4095, false);
  const PoolIndex b0 = make(pool, 0, 1);
  const PoolIndex b1 = make(pool, 4095, 1);
  const PoolIndex a0 = make(pool, 4095, 1);
  const PoolIndex a1 = make(pool, 0, 1);
  bids.push_back(pool, 0, b0);
  bids.push_back(pool, 4095, b1);
  asks.push_back(pool, 4095, a0);
  asks.push_back(pool, 0, a1);
  ASSERT_TRUE(bids.best());
  EXPECT_EQ(*bids.best(), 4095);
  ASSERT_TRUE(asks.best());
  EXPECT_EQ(*asks.best(), 0);
  bids.unlink(pool, 4095, b1);
  ASSERT_TRUE(bids.best());
  EXPECT_EQ(*bids.best(), 0);
}

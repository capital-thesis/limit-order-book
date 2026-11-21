#include "lob/book.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <map>
#include <random>
#include <vector>

using lob::CancelReason;
using lob::Event;
using lob::FastBook;
using lob::FastTotals;
using lob::NewOrder;
using lob::OrderId;
using lob::OrderType;
using lob::Price;
using lob::Qty;
using lob::RejectReason;
using lob::Side;
using lob::Tif;
using lob::ev::accepted;
using lob::ev::cancelled;
using lob::ev::rejected;
using lob::ev::rested;

namespace {

struct Collect {
  std::vector<Event> events;
  void operator()(const Event& e) { events.push_back(e); }
};

NewOrder limit(Side s, Price p, Qty q) {
  NewOrder o;
  o.side = s;
  o.type = OrderType::Limit;
  o.tif = Tif::Gtc;
  o.price = p;
  o.qty = q;
  return o;
}

}  // namespace

TEST(FastBookAddCancel, EmptyBookHasNoBest) {
  FastBook b(90, 110, 16);
  EXPECT_FALSE(b.best_bid().has_value());
  EXPECT_FALSE(b.best_ask().has_value());
  EXPECT_EQ(b.resting_qty(), 0);
}

TEST(FastBookAddCancel, RestingBuyEmitsAcceptedThenRested) {
  FastBook b(90, 110, 16);
  Collect c;
  EXPECT_EQ(b.add(limit(Side::Buy, 100, 10), c), 1u);
  ASSERT_EQ(c.events.size(), 2u);
  EXPECT_EQ(c.events[0], accepted(1));
  EXPECT_EQ(c.events[1], rested(1, 100, 10));
  ASSERT_TRUE(b.best_bid());
  EXPECT_EQ(*b.best_bid(), 100);
  EXPECT_EQ(b.resting_qty(), 10);
  EXPECT_EQ(b.totals().submitted, 10);
}

TEST(FastBookAddCancel, InvalidQtyConsumesIdAndINotCounted) {
  FastBook b(90, 110, 16);
  Collect c;
  EXPECT_EQ(b.add(limit(Side::Buy, 100, 0), c), 1u);
  ASSERT_EQ(c.events.size(), 1u);
  EXPECT_EQ(c.events[0], rejected(1, RejectReason::InvalidQty));
  EXPECT_EQ(b.totals().submitted, 0);
  EXPECT_EQ(b.add(limit(Side::Buy, 100, 5), c), 2u);
}

TEST(FastBookAddCancel, OutOfBandIsRejectedAndCounted) {
  FastBook b(90, 110, 16);
  Collect c;
  EXPECT_EQ(b.add(limit(Side::Sell, 200, 5), c), 1u);
  ASSERT_EQ(c.events.size(), 1u);
  EXPECT_EQ(c.events[0], rejected(1, RejectReason::OutOfBand));
  EXPECT_EQ(b.totals().submitted, 5);
  EXPECT_EQ(b.totals().rejected, 5);
  EXPECT_EQ(b.resting_qty(), 0);
}

TEST(FastBookAddCancel, BestBidIsHighestAndBestAskIsLowest) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 1), c);
  b.add(limit(Side::Buy, 101, 1), c);
  b.add(limit(Side::Buy, 99, 1), c);
  b.add(limit(Side::Sell, 108, 1), c);
  b.add(limit(Side::Sell, 110, 1), c);
  b.add(limit(Side::Sell, 109, 1), c);
  ASSERT_TRUE(b.best_bid());
  EXPECT_EQ(*b.best_bid(), 101);
  ASSERT_TRUE(b.best_ask());
  EXPECT_EQ(*b.best_ask(), 108);
}

TEST(FastBookAddCancel, CancelMiddleOfLevelKeepsOthers) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 1), c);  // id 1
  b.add(limit(Side::Buy, 100, 2), c);  // id 2
  b.add(limit(Side::Buy, 100, 3), c);  // id 3
  c.events.clear();
  b.cancel(2, c);
  ASSERT_EQ(c.events.size(), 1u);
  EXPECT_EQ(c.events[0], cancelled(2, CancelReason::UserCancel, 2));
  EXPECT_EQ(b.resting_qty(), 4);
  EXPECT_EQ(b.order_count(), 2u);
  ASSERT_TRUE(b.best_bid());
  EXPECT_EQ(*b.best_bid(), 100);
}

TEST(FastBookAddCancel, CancelRemovesLevelAndRefreshesBest) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 1), c);  // id 1
  b.add(limit(Side::Buy, 101, 1), c);  // id 2
  b.cancel(2, c);
  ASSERT_TRUE(b.best_bid());
  EXPECT_EQ(*b.best_bid(), 100);
  b.cancel(1, c);
  EXPECT_FALSE(b.best_bid().has_value());
  EXPECT_EQ(b.resting_qty(), 0);
}

TEST(FastBookAddCancel, CancelUnknownAndFinishedAreRejected) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 5), c);  // id 1
  b.cancel(1, c);
  c.events.clear();
  b.cancel(1, c);
  ASSERT_EQ(c.events.size(), 1u);
  EXPECT_EQ(c.events[0], rejected(1, RejectReason::UnknownOrder));
  c.events.clear();
  b.cancel(42, c);
  ASSERT_EQ(c.events.size(), 1u);
  EXPECT_EQ(c.events[0], rejected(42, RejectReason::UnknownOrder));
  EXPECT_EQ(b.totals().cancelled, 5);
  EXPECT_EQ(b.resting_qty(), 0);
}

TEST(FastBookAddCancel, RandomAddCancelKeepsAccounting) {
  // At most 70% of 50000 steps add, so 40000 slots can never run out.
  FastBook b(80, 120, 40000);
  std::map<OrderId, Qty> live;  // id -> leaves, for an independent resting sum
  Qty resting = 0;
  std::mt19937 rng(7);
  Collect c;
  OrderId max_id = 0;
  for (int step = 0; step < 50000; ++step) {
    if (rng() % 10 < 7) {
      const bool buy = rng() % 2 == 0;
      const Price p = buy ? 80 + static_cast<Price>(rng() % 20)     // 80..99
                          : 101 + static_cast<Price>(rng() % 20);     // 101..120
      const Qty q = 1 + static_cast<Qty>(rng() % 100);
      c.events.clear();
      const OrderId id = b.add(limit(buy ? Side::Buy : Side::Sell, p, q), c);
      max_id = id;
      live[id] = q;
      resting += q;
    } else if (max_id > 0) {
      const OrderId id = 1 + rng() % max_id;  // love, finished, or never resting
      c.events.clear();
      b.cancel(id, c);
      const auto it = live.find(id);
      if (it != live.end()) {
        resting -= it->second;
        live.erase(it);
      }
    }
    ASSERT_EQ(b.resting_qty(), resting) << "step " << step;
    const FastTotals& t = b.totals();
    ASSERT_EQ(t.submitted, resting + t.cancelled + t.rejected) << "step" << step;
    ASSERT_EQ(b.order_count(), live.size()) << "step " << step;
  }
}

#include "lob/book.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

using lob::Event;
using lob::FastBook;
using lob::NewOrder;
using lob::OrderType;
using lob::Price;
using lob::Qty;
using lob::RejectReason;
using lob::Side;
using lob::Tif;
namespace ev = lob::ev;

namespace {

struct Collect {
  std::vector<Event> events;
  void operator()(const Event& e) { events.push_back(e); }
};

NewOrder limit(Side s, Price p, Qty q, Tif t = Tif::Gtc) {
  NewOrder o;
  o.side = s;
  o.type = OrderType::Limit;
  o.tif = t;
  o.price = p;
  o.qty = q;
  return o;
}

NewOrder market_fok(Side s, Qty q) {
  NewOrder o;
  o.side = s;
  o.type = OrderType::Market;
  o.tif = Tif::Fok;
  o.qty = q;
  return o;
}

void expect_events(const Collect& c, const std::vector<Event>& want) {
  ASSERT_EQ(c.events.size(), want.size());
  for (std::size_t k = 0; k < want.size(); ++k) {
    EXPECT_EQ(c.events[k], want[k]) << "event " << k;
  }
}

}  // namespace

TEST(BookMatch, FokFullyFillableFillsCompletelyAcrossLevels) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 3), c);  // id 1
  b.add(limit(Side::Sell, 102, 2), c);  // id 2
  c.events.clear();
  b.submit(limit(Side::Buy, 102, 5, Tif::Fok), c);  // id 3
  expect_events(c, {
      ev::accepted(3),
      ev::trade(1, 3, 101, 3),
      ev::filled(1),
      ev::trade(2, 3, 102, 2),
      ev::filled(2),
      ev::filled(3),
  });
  EXPECT_FALSE(b.best_ask().has_value());
  EXPECT_EQ(b.resting_qty(), 0);
  b.check_invariants();
}

TEST(BookMatch, FokThatCannotFillIsRejectedAndBookUnchanged) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 3), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Buy, 101, 4, Tif::Fok), c);  // id 2
  expect_events(c, {ev::rejected(2, RejectReason::FokUnfillable)});
  EXPECT_EQ(b.totals().submitted, 7);  // 3 resting + 4 rejected
  EXPECT_EQ(b.totals().rejected, 4);
  EXPECT_EQ(b.totals().traded, 0);
  ASSERT_TRUE(b.best_ask());
  EXPECT_EQ(*b.best_ask(), 101);
  EXPECT_EQ(b.resting_qty(), 3);
  EXPECT_EQ(b.order_count(), 1u);
  b.check_invariants();
}

TEST(BookMatch, FokMissingByOneShareIsRejected) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 3), c);  // id 1
  b.add(limit(Side::Sell, 102, 2), c);  // id 2
  c.events.clear();
  b.submit(limit(Side::Buy, 102, 6, Tif::Fok), c);  // id 3, needs 6, only 5 cross
  expect_events(c, {ev::rejected(3, RejectReason::FokUnfillable)});
  EXPECT_EQ(b.resting_qty(), 5);
  b.check_invariants();
}

TEST(BookMatch, FokCountsOnlyLiquidityWithinItsLimit) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 2), c);   // id 1
  b.add(limit(Side::Sell, 105, 10), c);  // id 2, beyond the limit
  c.events.clear();
  b.submit(limit(Side::Buy, 102, 5, Tif::Fok), c);  // id 3
  expect_events(c, {ev::rejected(3, RejectReason::FokUnfillable)});
  EXPECT_EQ(b.resting_qty(), 12);
  b.check_invariants();
}

TEST(BookMatch, FokIntoEmptySideIsFokUnfillableNotNoLiquidity) {
  FastBook b(90, 110, 16);
  Collect c;
  b.submit(limit(Side::Buy, 100, 2, Tif::Fok), c);  // id 1
  expect_events(c, {ev::rejected(1, RejectReason::FokUnfillable)});
  b.check_invariants();
}

TEST(BookMatch, FokSellSideFillsExactly) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 4), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Sell, 100, 4, Tif::Fok), c);  // id 2
  expect_events(c, {ev::accepted(2), ev::trade(1, 2, 100, 4), ev::filled(1), ev::filled(2)});
  b.check_invariants();
}

TEST(BookMatch, FokMarketAllOrNothing) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 2), c);  // id 1
  c.events.clear();
  b.submit(market_fok(Side::Buy, 3), c);  // id 2: 2 available, needs 3
  expect_events(c, {ev::rejected(2, RejectReason::FokUnfillable)});
  EXPECT_EQ(b.resting_qty(), 2);
  c.events.clear();
  b.submit(market_fok(Side::Buy, 2), c);  // id 3: exactly enough
  expect_events(c, {ev::accepted(3), ev::trade(1, 3, 101, 2), ev::filled(1), ev::filled(3)});
  b.check_invariants();
}

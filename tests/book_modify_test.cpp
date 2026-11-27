#include "lob/book.hpp"

#include "gtest/gtest.h"

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

void expect_events(const Collect& c, const std::vector<Event>& want) {
  ASSERT_EQ(c.events.size(), want.size());
  for (std::size_t k = 0; k < want.size(); ++k) {
    EXPECT_EQ(c.events[k], want[k]) << "event " << k;
  }
}

}  // namespace

// S5.1: same-price decrease keeps priority

TEST(BookModify, DownKeepsPriorityAtHeadOfLevel) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 5), c);  // id 1, head of the queue
  b.add(limit(Side::Buy, 100, 5), c);  // id 2
  c.events.clear();
  b.modify(1, 3, 100, c);
  expect_events(c, {ev::modified(1, 100, 3, 3)});
  EXPECT_EQ(b.totals().reduced, 2);
  EXPECT_EQ(b.resting_qty(), 8);
  // Id 1 still fills first.
  c.events.clear();
  b.submit(limit(Side::Sell, 100, 3, Tif::Ioc), c);  // id 3
  expect_events(c, {ev::accepted(3), ev::trade(1, 3, 100, 3), ev::filled(1), ev::filled(3)});
  b.check_invariants();
}

TEST(BookModify, DownSamePriceSameTotalIsNoOpEmittingOnlyModified) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 5), c);  // id 1
  b.add(limit(Side::Buy, 100, 5), c);  // id 2
  c.events.clear();
  b.modify(1, 5, 100, c);
  expect_events(c, {ev::modified(1, 100, 5, 5)});
  EXPECT_EQ(b.totals().reduced, 0);
  EXPECT_EQ(b.totals().increased, 0);
  EXPECT_EQ(b.resting_qty(), 10);
  b.check_invariants();
}

TEST(BookModify, DownAfterPartialFillKeepsPriorityAndReducesLeaves) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 10), c);  // id 1: filled 4 below, leaves 6
  b.submit(limit(Side::Sell, 100, 4, Tif::Ioc), c);  // id 2
  c.events.clear();
  b.modify(1, 6, 100, c);  // total 6, filled 4, leaves 2
  expect_events(c, {ev::modified(1, 100, 6, 2)});
  EXPECT_EQ(b.resting_qty(), 2);
  EXPECT_EQ(b.totals().reduced, 4);
  b.check_invariants();
}

// S5.2: increase and price change lose priority and re-enter as aggressor

TEST(BookModify, UpLosesPriorityAndGoesToBackOfLevel) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 5), c);  // id 1
  b.add(limit(Side::Buy, 100, 5), c);  // id 2
  c.events.clear();
  b.modify(1, 8, 100, c);
  expect_events(c, {ev::modified(1, 100, 8, 8), ev::rested(1, 100, 8)});
  EXPECT_EQ(b.totals().increased, 3);
  EXPECT_EQ(b.resting_qty(), 13);
  // Id 2 now fills first.
  c.events.clear();
  b.submit(limit(Side::Sell, 100, 5, Tif::Ioc), c);  // id 3
  expect_events(c, {ev::accepted(3), ev::trade(2, 3, 100, 5), ev::filled(2), ev::filled(3)});
  b.check_invariants();
}

TEST(BookModify, PriceChangeLosesPriorityAndRestsAtNewPrice) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 5), c);  // id 1
  c.events.clear();
  b.modify(1, 5, 99, c);
  expect_events(c, {ev::modified(1, 99, 5, 5), ev::rested(1, 99, 5)});
  ASSERT_TRUE(b.best_bid().has_value());
  EXPECT_EQ(*b.best_bid(), 99);
  EXPECT_EQ(b.resting_qty(), 5);
  b.check_invariants();
}

TEST(BookModify, PriceChangeCrossesAsAggressorAndRestsRemainder) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 102, 3), c);  // id 1
  b.add(limit(Side::Buy, 100, 5), c);   // id 2, rests
  c.events.clear();
  b.modify(2, 5, 102, c);
  expect_events(c, {
      ev::modified(2, 102, 5, 5),
      ev::trade(1, 2, 102, 3),
      ev::filled(1),
      ev::rested(2, 102, 2),
  });
  ASSERT_TRUE(b.best_bid().has_value());
  EXPECT_EQ(*b.best_bid(), 102);
  EXPECT_FALSE(b.best_ask().has_value());
  EXPECT_EQ(b.resting_qty(), 2);
  b.check_invariants();
}

TEST(BookModify, PriceChangeFullyCrossesAndFillsBothSides) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 2), c);  // id 1
  b.add(limit(Side::Buy, 100, 2), c);   // id 2
  c.events.clear();
  b.modify(2, 2, 101, c);
  expect_events(c, {
      ev::modified(2, 101, 2, 2),
      ev::trade(1, 2, 101, 2),
      ev::filled(1),
      ev::filled(2),
  });
  EXPECT_EQ(b.resting_qty(), 0);
  EXPECT_FALSE(b.best_bid().has_value());
  EXPECT_FALSE(b.best_ask().has_value());
  b.check_invariants();
}

TEST(BookModify, UpAfterPartialFillRestsWithNewRemainder) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 10), c);  // id 1
  b.submit(limit(Side::Sell, 100, 4, Tif::Ioc), c);  // id 2, fills 4 of id 1
  c.events.clear();
  b.modify(1, 12, 100, c);  // total 12, filled 4, leaves 8
  expect_events(c, {ev::modified(1, 100, 12, 8), ev::rested(1, 100, 8)});
  EXPECT_EQ(b.resting_qty(), 8);
  EXPECT_EQ(b.totals().increased, 2);
  b.check_invariants();
}

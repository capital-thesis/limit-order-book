#include "lob/book.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <vector>

using lob::CancelReason;
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

NewOrder market(Side s, Qty q) {
  NewOrder o;
  o.side = s;
  o.type = OrderType::Market;
  o.tif = Tif::Ioc;
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

// S4.1: market and IOC sweep

TEST(BookMatch, MarketIntoEmptySideRejectsNoLiquidity) {
  FastBook b(90, 110, 16);
  Collect c;
  b.submit(market(Side::Buy, 5), c);
  expect_events(c, {ev::rejected(1, RejectReason::NoLiquidity)});
  EXPECT_EQ(b.totals().submitted, 5);
  EXPECT_EQ(b.totals().rejected, 5);
  EXPECT_EQ(b.totals().traded, 0);
  b.check_invariants();
}

TEST(BookMatch, MarketWalksLevelsAndFillsCompletely) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 3), c);  // id 1
  b.add(limit(Side::Sell, 102, 4), c);  // id 2
  c.events.clear();
  b.submit(market(Side::Buy, 5), c);  // id 3
  expect_events(c, {
      ev::accepted(3),
      ev::trade(1, 3, 101, 3),
      ev::filled(1),
      ev::trade(2, 3, 102, 2),
      ev::filled(3),
  });
  ASSERT_TRUE(b.best_ask());
  EXPECT_EQ(*b.best_ask(), 102);
  EXPECT_EQ(b.resting_qty(), 2);
  EXPECT_EQ(b.totals().traded, 5);
  b.check_invariants();
}

TEST(BookMatch, MarketFullFillEmitsFilledForMakerAndTaker) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 2), c);  // id 1
  c.events.clear();
  b.submit(market(Side::Buy, 2), c);  // id 2
  expect_events(c, {ev::accepted(2), ev::trade(1, 2, 101, 2), ev::filled(1), ev::filled(2)});
  EXPECT_FALSE(b.best_ask().has_value());
  EXPECT_EQ(b.resting_qty(), 0);
  b.check_invariants();
}

TEST(BookMatch, MarketRemainderIsCancelledAsIocRemainder) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 2), c);  // id 1
  c.events.clear();
  b.submit(market(Side::Buy, 5), c);  // id 2
  expect_events(c, {
      ev::accepted(2),
      ev::trade(1, 2, 101, 2),
      ev::filled(1),
      ev::cancelled(2, CancelReason::IocRemainder, 3),
  });
  EXPECT_EQ(b.totals().cancelled, 3);
  EXPECT_EQ(b.totals().traded, 2);
  EXPECT_EQ(b.totals().submitted, 7);
  EXPECT_FALSE(b.best_bid().has_value());
  b.check_invariants();
}

TEST(BookMatch, MarketIdIsUnknownToCancel) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 101, 2), c);  // id 1
  b.submit(market(Side::Buy, 2), c);    // id 2, finishes at once
  c.events.clear();
  b.cancel(2, c);
  expect_events(c, {ev::rejected(2, RejectReason::UnknownOrder)});
  b.check_invariants();
}

TEST(BookMatch, IocPartialFillCancelsRemainderAndDoesNotRest) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 101, 3), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Sell, 100, 5, Tif::Ioc), c);  // id 2
  expect_events(c, {
      ev::accepted(2),
      ev::trade(1, 2, 101, 3),
      ev::filled(1),
      ev::cancelled(2, CancelReason::IocRemainder, 2),
  });
  EXPECT_FALSE(b.best_bid().has_value());
  EXPECT_FALSE(b.best_ask().has_value());
  EXPECT_EQ(b.resting_qty(), 0);
  b.check_invariants();
}

TEST(BookMatch, IocWithNoCrossCancelsWhollyAndLeavesBookAlone) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 105, 4), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Buy, 100, 4, Tif::Ioc), c);  // id 2
  expect_events(c, {ev::accepted(2), ev::cancelled(2, CancelReason::IocRemainder, 4)});
  ASSERT_TRUE(b.best_ask());
  EXPECT_EQ(*b.best_ask(), 105);
  EXPECT_EQ(b.resting_qty(), 4);
  b.check_invariants();
}

TEST(BookMatch, IocExactFillEmitsFilledForTaker) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 100, 4), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Buy, 100, 4, Tif::Ioc), c);  // id 2
  expect_events(c, {ev::accepted(2), ev::trade(1, 2, 100, 4), ev::filled(1), ev::filled(2)});
  b.check_invariants();
}

// S4.2: aggressor limits cross within their price and rest the remainder

TEST(BookMatch, LimitCrossingFillsAtMakerPrice) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 100, 5), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Buy, 101, 5), c);  // id 2
  expect_events(c, {ev::accepted(2), ev::trade(1, 2, 100, 5), ev::filled(1), ev::filled(2)});
  EXPECT_EQ(b.totals().traded, 5);
  b.check_invariants();
}

TEST(BookMatch, LimitPartialFillRestsRemainder) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 100, 3), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Buy, 101, 5), c);  // id 2
  expect_events(c, {
      ev::accepted(2),
      ev::trade(1, 2, 100, 3),
      ev::filled(1),
      ev::rested(2, 101, 2),
  });
  ASSERT_TRUE(b.best_bid());
  EXPECT_EQ(*b.best_bid(), 101);
  EXPECT_EQ(b.resting_qty(), 2);
  b.check_invariants();
}

TEST(BookMatch, LimitDoesNotCrossBeyondItsPrice) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 102, 3), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Buy, 101, 2), c);  // id 2
  expect_events(c, {ev::accepted(2), ev::rested(2, 101, 2)});
  ASSERT_TRUE(b.best_ask());
  EXPECT_EQ(*b.best_ask(), 102);
  b.check_invariants();
}

TEST(BookMatch, LimitSellWalksBidsDownToItsPrice) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 2), c);  // id 1
  b.add(limit(Side::Buy, 99, 2), c);   // id 2
  c.events.clear();
  b.submit(limit(Side::Sell, 99, 3), c);  // id 3
  expect_events(c, {
      ev::accepted(3),
      ev::trade(1, 3, 100, 2),
      ev::filled(1),
      ev::trade(2, 3, 99, 1),
      ev::filled(3),
  });
  ASSERT_TRUE(b.best_bid());
  EXPECT_EQ(*b.best_bid(), 99);
  EXPECT_EQ(b.resting_qty(), 1);
  b.check_invariants();
}

TEST(BookMatch, LimitSellBeyondBidsRests) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Buy, 100, 2), c);  // id 1
  c.events.clear();
  b.submit(limit(Side::Sell, 101, 4), c);  // id 2
  expect_events(c, {ev::accepted(2), ev::rested(2, 101, 4)});
  EXPECT_EQ(*b.best_bid(), 100);
  EXPECT_EQ(*b.best_ask(), 101);
  b.check_invariants();
}

TEST(BookMatch, LimitFifoWithinLevelIsRespectedBySweep) {
  FastBook b(90, 110, 16);
  Collect c;
  b.add(limit(Side::Sell, 100, 2), c);  // id 1, first in the queue
  b.add(limit(Side::Sell, 100, 2), c);  // id 2
  c.events.clear();
  b.submit(limit(Side::Buy, 100, 3), c);  // id 3
  expect_events(c, {
      ev::accepted(3),
      ev::trade(1, 3, 100, 2),
      ev::filled(1),
      ev::trade(2, 3, 100, 1),
      ev::filled(3),
  });
  EXPECT_EQ(b.resting_qty(), 1);
  b.check_invariants();
}

// Unit tests for the naive reference book. Each test pins a clause of
// docs/CONTRACTS.md. Every operation ends with check_invariants().

#include <gtest/gtest.h>

#include <ostream>
#include <vector>

#include "reference_book.hpp"

namespace lob {

std::ostream& operator<<(std::ostream& os, const Event& e) {
  os << "Event{kind=" << static_cast<int>(e.kind)
     << " reject=" << static_cast<int>(e.reject)
     << " cancel=" << static_cast<int>(e.cancel) << " order=" << e.order
     << " maker=" << e.maker << " taker=" << e.taker << " price=" << e.price
     << " qty=" << e.qty << " leaves=" << e.leaves << " total=" << e.total
     << "}";
  return os;
}

}  // namespace lob

namespace {

using namespace lob;

constexpr Price kMin = 0;
constexpr Price kMax = 1000;

struct Collector {
  std::vector<Event> events;
  void operator()(const Event& e) { events.push_back(e); }
  void clear() { events.clear(); }
};

NewOrder limit(Side s, Price px, Qty q, Tif t = Tif::Gtc) {
  NewOrder o;
  o.side = s;
  o.type = OrderType::Limit;
  o.tif = t;
  o.price = px;
  o.qty = q;
  return o;
}

NewOrder market(Side s, Qty q, Tif t = Tif::Ioc) {
  NewOrder o;
  o.side = s;
  o.type = OrderType::Market;
  o.tif = t;
  o.qty = q;
  return o;
}

using Events = std::vector<Event>;

}  // namespace

// ---------- empty book and rest ----------

TEST(ReferenceBook, EmptyBookInvariantsHold) {
  ReferenceBook b(kMin, kMax);
  EXPECT_FALSE(b.best_bid().has_value());
  EXPECT_FALSE(b.best_ask().has_value());
  EXPECT_EQ(b.resting_qty(), 0);
  b.check_invariants();
}

TEST(ReferenceBook, RestingBuyEmitsAcceptedThenRested) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  EXPECT_EQ(b.submit(limit(Side::Buy, 100, 10), s), 1u);
  EXPECT_EQ(s.events, (Events{ev::accepted(1), ev::rested(1, 100, 10)}));
  EXPECT_EQ(b.best_bid(), std::optional<Price>(100));
  EXPECT_EQ(b.resting_qty(), 10);
  EXPECT_EQ(b.totals().submitted, 10);
  b.check_invariants();
}

TEST(ReferenceBook, EveryCallConsumesAnIdIncludingRejects) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  EXPECT_EQ(b.submit(limit(Side::Buy, 100, 0), s), 1u);  // invalid qty
  EXPECT_EQ(s.events, (Events{ev::rejected(1, RejectReason::InvalidQty)}));
  s.clear();
  EXPECT_EQ(b.submit(limit(Side::Buy, 100, 5), s), 2u);
  EXPECT_EQ(s.events[0], ev::accepted(2));
  b.check_invariants();
}

TEST(ReferenceBook, InvalidQtyChangesNoAccounting) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, -5), s);
  EXPECT_EQ(b.totals().submitted, 0);
  EXPECT_EQ(b.totals().rejected, 0);
  b.check_invariants();
}

TEST(ReferenceBook, PriceBandBoundariesAreInclusive) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, kMax + 1, 5), s);
  EXPECT_EQ(s.events, (Events{ev::rejected(1, RejectReason::OutOfBand)}));
  EXPECT_EQ(b.totals().rejected, 5);
  s.clear();
  b.submit(limit(Side::Buy, kMax, 5), s);
  EXPECT_EQ(s.events[0], ev::accepted(2));
  s.clear();
  b.submit(limit(Side::Sell, kMin - 1, 5), s);
  EXPECT_EQ(s.events, (Events{ev::rejected(3, RejectReason::OutOfBand)}));
  b.check_invariants();
}

// ---------- matching: crossing, market, IOC, FOK ----------

TEST(ReferenceBook, CrossingLimitFillsCompletelyAndPrintsAtMakerPrice) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 10), s);  // id 1 rests
  s.clear();
  b.submit(limit(Side::Buy, 101, 10), s);  // id 2, limit above the ask
  EXPECT_EQ(s.events, (Events{ev::accepted(2), ev::trade(1, 2, 100, 10),
                              ev::filled(1), ev::filled(2)}));
  EXPECT_FALSE(b.best_ask().has_value());
  EXPECT_EQ(b.totals().traded, 10);
  b.check_invariants();
}

TEST(ReferenceBook, AggressorPartialFillRestsRemainder) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 4), s);  // id 1
  s.clear();
  b.submit(limit(Side::Buy, 100, 10), s);  // id 2
  EXPECT_EQ(s.events, (Events{ev::accepted(2), ev::trade(1, 2, 100, 4),
                              ev::filled(1), ev::rested(2, 100, 6)}));
  EXPECT_EQ(b.best_bid(), std::optional<Price>(100));
  EXPECT_EQ(b.resting_qty(), 6);
  b.check_invariants();
}

TEST(ReferenceBook, MarketIntoEmptyOppositeSideRejectsNoLiquidity) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(market(Side::Buy, 5), s);
  EXPECT_EQ(s.events, (Events{ev::rejected(1, RejectReason::NoLiquidity)}));
  EXPECT_EQ(b.totals().rejected, 5);
  b.check_invariants();
}

TEST(ReferenceBook, MarketOrderWalksLevelsAndNeverRests) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 3), s);  // id 1
  b.submit(limit(Side::Sell, 101, 3), s);  // id 2
  s.clear();
  b.submit(market(Side::Buy, 5), s);  // id 3
  EXPECT_EQ(s.events, (Events{ev::accepted(3), ev::trade(1, 3, 100, 3),
                              ev::filled(1), ev::trade(2, 3, 101, 2),
                              ev::filled(3)}));
  EXPECT_EQ(b.best_ask(), std::optional<Price>(101));
  EXPECT_EQ(b.resting_qty(), 1);
  b.check_invariants();
}

TEST(ReferenceBook, MarketRemainderIsCancelledNotRested) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 3), s);  // id 1
  s.clear();
  b.submit(market(Side::Buy, 5), s);  // id 2, IOC behaviour
  EXPECT_EQ(s.events, (Events{ev::accepted(2), ev::trade(1, 2, 100, 3),
                              ev::filled(1),
                              ev::cancelled(2, CancelReason::IocRemainder, 2)}));
  EXPECT_FALSE(b.best_bid().has_value());
  b.check_invariants();
}

TEST(ReferenceBook, IocPartialFillCancelsRemainder) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 3), s);  // id 1
  s.clear();
  b.submit(limit(Side::Buy, 100, 5, Tif::Ioc), s);  // id 2
  EXPECT_EQ(s.events, (Events{ev::accepted(2), ev::trade(1, 2, 100, 3),
                              ev::filled(1),
                              ev::cancelled(2, CancelReason::IocRemainder, 2)}));
  EXPECT_EQ(b.totals().cancelled, 2);
  b.check_invariants();
}

TEST(ReferenceBook, FokThatCannotFillIsRejectedAndBookUnchanged) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 3), s);  // id 1
  s.clear();
  b.submit(limit(Side::Buy, 100, 5, Tif::Fok), s);  // id 2
  EXPECT_EQ(s.events, (Events{ev::rejected(2, RejectReason::FokUnfillable)}));
  EXPECT_EQ(b.best_ask(), std::optional<Price>(100));
  EXPECT_EQ(b.resting_qty(), 3);
  EXPECT_EQ(b.totals().rejected, 5);
  b.check_invariants();
}

TEST(ReferenceBook, FokMissingByOneShareIsRejected) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 4), s);  // id 1
  s.clear();
  b.submit(limit(Side::Buy, 100, 5, Tif::Fok), s);
  EXPECT_EQ(s.events, (Events{ev::rejected(2, RejectReason::FokUnfillable)}));
  b.check_invariants();
}

TEST(ReferenceBook, FokFillsAcrossLevelsWithinLimit) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 2), s);  // id 1
  b.submit(limit(Side::Sell, 101, 3), s);  // id 2
  s.clear();
  b.submit(limit(Side::Buy, 101, 5, Tif::Fok), s);  // id 3
  EXPECT_EQ(s.events, (Events{ev::accepted(3), ev::trade(1, 3, 100, 2),
                              ev::filled(1), ev::trade(2, 3, 101, 3),
                              ev::filled(2), ev::filled(3)}));
  b.check_invariants();
}

TEST(ReferenceBook, FokIgnoresLiquidityBeyondItsLimit) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 3), s);  // id 1
  b.submit(limit(Side::Sell, 102, 10), s);  // id 2, out of reach
  s.clear();
  b.submit(limit(Side::Buy, 101, 5, Tif::Fok), s);  // id 3
  EXPECT_EQ(s.events, (Events{ev::rejected(3, RejectReason::FokUnfillable)}));
  b.check_invariants();
}

TEST(ReferenceBook, PricePriorityBeforeTimePriority) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 101, 1), s);  // id 1, earlier but worse
  b.submit(limit(Side::Sell, 100, 1), s);  // id 2, better
  s.clear();
  b.submit(limit(Side::Buy, 101, 1), s);  // id 3
  EXPECT_EQ(s.events, (Events{ev::accepted(3), ev::trade(2, 3, 100, 1),
                              ev::filled(2), ev::filled(3)}));
  b.check_invariants();
}

TEST(ReferenceBook, FifoWithinLevel) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 1), s);  // id 1
  b.submit(limit(Side::Sell, 100, 1), s);  // id 2
  b.submit(limit(Side::Sell, 100, 1), s);  // id 3
  s.clear();
  b.submit(limit(Side::Buy, 100, 2), s);  // id 4
  EXPECT_EQ(s.events, (Events{ev::accepted(4), ev::trade(1, 4, 100, 1),
                              ev::filled(1), ev::trade(2, 4, 100, 1),
                              ev::filled(2), ev::filled(4)}));
  EXPECT_EQ(b.best_ask(), std::optional<Price>(100));
  EXPECT_EQ(b.resting_qty(), 1);
  b.check_invariants();
}

// ---------- cancel ----------

TEST(ReferenceBook, CancelRestingOrderEmitsCancelledAndAccounts) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);  // id 1
  s.clear();
  b.cancel(1, s);
  EXPECT_EQ(s.events, (Events{ev::cancelled(1, CancelReason::UserCancel, 10)}));
  EXPECT_FALSE(b.best_bid().has_value());
  EXPECT_EQ(b.totals().cancelled, 10);
  b.check_invariants();
}

TEST(ReferenceBook, CancelUnknownOrFinishedOrderRejects) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.cancel(99, s);
  EXPECT_EQ(s.events, (Events{ev::rejected(99, RejectReason::UnknownOrder)}));
  s.clear();
  b.submit(limit(Side::Sell, 100, 5), s);  // id 2
  b.submit(limit(Side::Buy, 100, 5), s);   // id 3, fully fills id 2
  s.clear();
  b.cancel(2, s);
  EXPECT_EQ(s.events, (Events{ev::rejected(2, RejectReason::UnknownOrder)}));
  b.check_invariants();
}

TEST(ReferenceBook, CancelFromMiddleKeepsFifoOfOthers) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 5), s);  // id 1
  b.submit(limit(Side::Buy, 100, 5), s);  // id 2
  b.submit(limit(Side::Buy, 100, 5), s);  // id 3
  b.cancel(2, s);
  s.clear();
  b.submit(limit(Side::Sell, 100, 10), s);  // id 4
  EXPECT_EQ(s.events, (Events{ev::accepted(4), ev::trade(1, 4, 100, 5),
                              ev::filled(1), ev::trade(3, 4, 100, 5),
                              ev::filled(3), ev::filled(4)}));
  b.check_invariants();
}

TEST(ReferenceBook, MarketOrderIdIsUnknownToModifyAndCancel) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Sell, 100, 5), s);  // id 1
  b.submit(market(Side::Buy, 3), s);       // id 2, finishes immediately
  s.clear();
  b.modify(2, 3, 100, s);
  b.cancel(2, s);
  EXPECT_EQ(s.events, (Events{ev::rejected(2, RejectReason::UnknownOrder),
                              ev::rejected(2, RejectReason::UnknownOrder)}));
  b.check_invariants();
}

// ---------- modify ----------

TEST(ReferenceBook, ModifyDownKeepsPriority) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);  // id 1
  b.submit(limit(Side::Buy, 100, 10), s);  // id 2
  s.clear();
  b.modify(1, 5, 100, s);
  EXPECT_EQ(s.events, (Events{ev::modified(1, 100, 5, 5)}));
  s.clear();
  b.submit(market(Side::Sell, 5), s);  // id 3
  EXPECT_EQ(s.events, (Events{ev::accepted(3), ev::trade(1, 3, 100, 5),
                              ev::filled(1), ev::filled(3)}));
  EXPECT_EQ(b.resting_qty(), 10);  // id 2 untouched
  b.check_invariants();
}

TEST(ReferenceBook, ModifyUpLosesPriority) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);  // id 1
  b.submit(limit(Side::Buy, 100, 10), s);  // id 2
  s.clear();
  b.modify(1, 12, 100, s);
  EXPECT_EQ(s.events, (Events{ev::modified(1, 100, 12, 12), ev::rested(1, 100, 12)}));
  s.clear();
  b.submit(market(Side::Sell, 10), s);  // id 3
  EXPECT_EQ(s.events, (Events{ev::accepted(3), ev::trade(2, 3, 100, 10),
                              ev::filled(2), ev::filled(3)}));
  EXPECT_EQ(b.resting_qty(), 12);  // id 1 now at the back, still 12
  b.check_invariants();
}

TEST(ReferenceBook, ModifyNoOpKeepsPriorityAndEmitsOnlyModified) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);  // id 1
  b.submit(limit(Side::Buy, 100, 10), s);  // id 2
  s.clear();
  b.modify(1, 10, 100, s);
  EXPECT_EQ(s.events, (Events{ev::modified(1, 100, 10, 10)}));
  s.clear();
  b.submit(market(Side::Sell, 10), s);
  EXPECT_EQ(s.events[1], ev::trade(1, 3, 100, 10));
  b.check_invariants();
}

TEST(ReferenceBook, ModifyPriceChangeLosesPriorityAndCrossesAsAggressor) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);   // id 1
  b.submit(limit(Side::Sell, 101, 10), s);  // id 2
  s.clear();
  b.modify(1, 10, 101, s);
  EXPECT_EQ(s.events, (Events{ev::modified(1, 101, 10, 10),
                              ev::trade(2, 1, 101, 10), ev::filled(2),
                              ev::filled(1)}));
  EXPECT_EQ(b.totals().traded, 10);
  b.check_invariants();
}

TEST(ReferenceBook, ModifyPriceChangeThatPartlyCrossesThenRests) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);   // id 1
  b.submit(limit(Side::Sell, 101, 4), s);   // id 2
  s.clear();
  b.modify(1, 10, 101, s);
  EXPECT_EQ(s.events, (Events{ev::modified(1, 101, 10, 10),
                              ev::trade(2, 1, 101, 4), ev::filled(2),
                              ev::rested(1, 101, 6)}));
  EXPECT_EQ(b.best_bid(), std::optional<Price>(101));
  b.check_invariants();
}

TEST(ReferenceBook, ModifyToTotalNotAboveFilledRejects) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);  // id 1
  b.submit(market(Side::Sell, 4), s);      // id 2, fills id 1 by 4
  s.clear();
  b.modify(1, 4, 100, s);
  EXPECT_EQ(s.events, (Events{ev::rejected(1, RejectReason::QtyNotAboveFilled)}));
  s.clear();
  b.modify(1, 5, 100, s);
  EXPECT_EQ(s.events, (Events{ev::modified(1, 100, 5, 1)}));
  b.check_invariants();
}

TEST(ReferenceBook, ModifyTotalFiftyFromTwoHundredWithHundredFilled) {
  // Total semantics: 200 submitted, 100 filled, modify total to 150 leaves 50.
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 200), s);  // id 1
  b.submit(market(Side::Sell, 100), s);     // id 2
  s.clear();
  b.modify(1, 150, 100, s);
  EXPECT_EQ(s.events, (Events{ev::modified(1, 100, 150, 50)}));
  EXPECT_EQ(b.resting_qty(), 50);
  b.check_invariants();
}

TEST(ReferenceBook, ModifyUnknownOrderRejects) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.modify(42, 10, 100, s);
  EXPECT_EQ(s.events, (Events{ev::rejected(42, RejectReason::UnknownOrder)}));
  b.check_invariants();
}

TEST(ReferenceBook, ModifyInvalidTotalOrPriceRejects) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);  // id 1
  s.clear();
  b.modify(1, 0, 100, s);
  EXPECT_EQ(s.events, (Events{ev::rejected(1, RejectReason::InvalidQty)}));
  s.clear();
  b.modify(1, 10, kMax + 1, s);
  EXPECT_EQ(s.events, (Events{ev::rejected(1, RejectReason::OutOfBand)}));
  EXPECT_EQ(b.resting_qty(), 10);  // rejected modifies change nothing
  b.check_invariants();
}

// ---------- accounting ----------

TEST(ReferenceBook, GlobalConservationAcrossMixedOperations) {
  ReferenceBook b(kMin, kMax);
  Collector s;
  b.submit(limit(Side::Buy, 100, 10), s);
  b.submit(limit(Side::Sell, 100, 4), s);
  b.check_invariants();
  b.modify(1, 20, 100, s);
  b.check_invariants();
  b.modify(1, 8, 100, s);
  b.check_invariants();
  b.submit(limit(Side::Sell, 99, 7, Tif::Ioc), s);
  b.check_invariants();
  b.submit(limit(Side::Buy, 98, 3, Tif::Fok), s);
  b.check_invariants();
  b.cancel(1, s);
  b.check_invariants();
  EXPECT_EQ(b.resting_qty(), 0);
  EXPECT_EQ(b.totals().rejected, 3);
}

TEST(ReferenceBook, ConstructorRejectsEmptyPriceBand) {
  EXPECT_THROW(ReferenceBook(10, 5), std::invalid_argument);
}

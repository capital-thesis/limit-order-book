#include <gtest/gtest.h>

#include <cstdint>

#include "invariants.hpp"
#include "lob/book.hpp"
#include "reference_book.hpp"

namespace {

lob::NewOrder order(lob::Side s, lob::Price p, lob::Qty q) {
  lob::NewOrder o;
  o.side = s;
  o.type = lob::OrderType::Limit;
  o.tif = lob::Tif::Gtc;
  o.price = p; 
  o.qty = q;
  return o;
}

}  // namespace

TEST(Invariant, EmptyBooksPass) {
  lob::ReferenceBook ref(90, 110);
  lob::FastBook fast(90, 110, 16);
  testing_support::expect_same_state(ref, fast);
}

TEST(Invariant, ScriptedAddsAndCancelsPass) {
  lob::ReferenceBook ref(90, 110);
  lob::FastBook fast(90, 110, 64);
  auto drop = [](const lob::Event&) {};
  for (lob::Price p = 91; p <= 99; ++p) {
    ref.submit(order(lob::Side::Buy, p, p - 85), drop);
    fast.add(order(lob::Side::Buy, p, p - 85), drop);
    testing_support::expect_same_state(ref, fast);
  }
  for (lob::Price p = 101; p <= 109; ++p) {
    ref.submit(order(lob::Side::Sell, p, p - 90), drop);
    fast.add(order(lob::Side::Sell, p, p - 90), drop);
    testing_support::expect_same_state(ref, fast);
  }
  for (lob::OrderId id = 2; id <= 18; id += 3) {
    ref.cancel(id, drop);
    fast.cancel(id, drop);
    testing_support::expect_same_state(ref, fast);
  }
}

TEST(Invariant, FastBookCheckerRunsOnEveryStep) {
  lob::FastBook fast(90, 110, 16);
  auto drop = [](const lob::Event&) {};
  for (int i = 0; i < 8; ++i) {
    fast.add(order(lob::Side::Buy, 95, 1 + i), drop);
    fast.check_invariants();
  }
  for (lob::OrderId id = 1; id <= 8; id += 2) {
    fast.cancel(id, drop);
    fast.check_invariants();
  }
  EXPECT_EQ(fast.resting_qty(), 2 + 4 + 6 + 8);
}

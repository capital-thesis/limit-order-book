#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "invariants.hpp"
#include "lob/book.hpp"
#include "reference_book.hpp"

namespace {

constexpr lob::Price kMin = 90;
constexpr lob::Price kMax = 110;
constexpr lob::Price kMid = 100;  // buys sit below mid, sells above: add-only streams never cross

struct Collect {
  std::vector<lob::Event> events;
  void operator()(const lob::Event& e) { events.push_back(e); }
};

struct Op {
  bool is_add = false;
  lob::NewOrder order;
  lob::OrderId target = 0;
};

// Ids are predictable: every add consumes one, so the generator tracks `issued`.
Op next_op(std::mt19937& rng, lob::OrderId issued) {
  Op op;
  if (rng() % 100 < 65) {
    op.is_add = true;
    op.order.side = rng() % 2 == 0 ? lob::Side::Buy : lob::Side::Sell;
    op.order.type = lob::OrderType::Limit;
    op.order.tif = lob::Tif::Gtc;
    const unsigned roll = rng() % 100;
    if (roll < 1) {
      op.order.qty = 0;
    } else if (roll < 2) {
      op.order.qty = -5;
    } else {
      op.order.qty = 1 + static_cast<lob::Qty>(rng() % 100);
    }
    if (rng() % 100 < 3) {
      op.order.price = rng() % 2 == 0 ? kMin - 1 : kMax + 1;  // out of band
    } else if (op.order.side == lob::Side::Buy) {
      op.order.price = kMid - 1 - static_cast<lob::Price>(rng() % 10);  // 90..99
    } else {
      op.order.price = kMid + 1 + static_cast<lob::Price>(rng() % 10);  // 101..110
    }
  } else {
    op.is_add = false;
    op.target = 1 + rng() % (issued + 2);  // includes ids that never existed
  }
  return op;
}

std::string describe(std::uint32_t seed, int step, const Op& op) {
  std:: ostringstream os;
  os << "seed " << seed << ", step " << step << ", ";
  if (op.is_add) {
    os << "add " << (op.order.side == lob::Side::Buy ? "buy" : "sell")
       << " px " << op.order.price << " qty " << op.order.qty;
  } else {
    os << "cancel " << op.target;
  }
  return os.str();
}

void run_seed(std::uint32_t seed, int ops) {
    lob::ReferenceBook ref(kMin, kMax);
    lob::FastBook fast(kMin, kMax, 4096);
    std::mt19937 rng(seed);
    lob::OrderId issued = 0;
    for (int step = 0; step < ops; ++step) {
      const Op op = next_op(rng, issued);
      Collect expected;
      Collect actual;
      if (op.is_add) {
        ref.submit(op.order, expected);
        fast.add(op.order, actual);
        ++issued;
      } else {
        ref.cancel(op.target, expected);
        fast.cancel(op.target, actual);
      }
      ASSERT_EQ(expected.events.size(), actual.events.size()) << describe(seed, step, op);
      for (std::size_t k = 0; k < expected.events.size(); ++k) {
        ASSERT_EQ(expected.events[k], actual.events[k])
            << "event " << k << " of " << expected.events.size() << "; "
            << describe(seed, step, op);
      }
      testing_support::compare_state(ref, fast);
      // Full scans are linear in the book size, so run them every 16 steps and at the end.
      if (step % 16 == 15 || step == ops - 1) testing_support::check_both(ref, fast);
    }
}

}  // namespace

TEST(Differential, AddCancel) {
  for (std::uint32_t seed = 1; seed <= 1000; ++seed) {
    run_seed(seed, 2000);
    if (::testing::Test::HasFailure()) break;  // stop at the first failing seed
  }
}

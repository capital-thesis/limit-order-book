#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
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

// add-only: GTC limits, buys below mid and sells above, so nothing crosses.
// full: market, IOC, FOK and GTC limits, near-touch and whole-band prices, so trades happen.
enum class Mix { AddOnly, Full };

lob::Tif pick_tif(std::mt19937& rng) {
  const unsigned r = rng() % 100;
  if (r < 70) return lob::Tif::Gtc;
  if (r < 85) return lob::Tif::Ioc;
  return lob::Tif::Fok;
}

lob::Price pick_price(std::mt19937& rng, Mix mix, lob::Side side) {
  if (mix == Mix::AddOnly) {
    return side == lob::Side::Buy ? kMid - 1 - static_cast<lob::Price>(rng() % 10)
                                  : kMid + 1 + static_cast<lob::Price>(rng() % 10);
  }
  if (rng() % 2 == 0) return kMid - 4 + static_cast<lob::Price>(rng() % 9);  // near touch
  return kMin + static_cast<lob::Price>(rng() % (kMax - kMin + 1));          // whole band
}

// Ids are predictable: every add consumes one, so the generator tracks `issued`.
Op next_op(std::mt19937& rng, lob::OrderId issued, Mix mix) {
  Op op;
  if (rng() % 100 < 65) {
    op.is_add = true;
    op.order.side = rng() % 2 == 0 ? lob::Side::Buy : lob::Side::Sell;
    const unsigned roll = rng() % 100;
    if (roll < 1) {
      op.order.qty = 0;
    } else if (roll < 2) {
      op.order.qty = -5;
    } else {
      op.order.qty = 1 + static_cast<lob::Qty>(rng() % 100);
    }
    if (mix == Mix::Full && rng() % 100 < 10) {
      op.order.type = lob::OrderType::Market;
      op.order.tif = lob::Tif::Ioc;
      op.order.price = kMid;  // ignored by both books
    } else {
      op.order.type = lob::OrderType::Limit;
      op.order.tif = mix == Mix::Full ? pick_tif(rng) : lob::Tif::Gtc;
      if (rng() % 100 < 3) {
        op.order.price = rng() % 2 == 0 ? kMin - 1 : kMax + 1;  // out of band
      } else {
        op.order.price = pick_price(rng, mix, op.order.side);
      } 
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
    os << (op.order.type == lob::OrderType::Market ? "market " : "limit ")
       << (op.order.side == lob::Side::Buy ? "buy" : "sell")
       << " px " << op.order.price << " qty " << op.order.qty
       << " tif " << static_cast<int>(op.order.tif);
  } else {
    os << "cancel " << op.target;
  }
  return os.str();
}

void run_seed(std::uint32_t seed, int ops, Mix mix) {
    lob::ReferenceBook ref(kMin, kMax);
    lob::FastBook fast(kMin, kMax, 4096);
    std::mt19937 rng(seed);
    lob::OrderId issued = 0;
    for (int step = 0; step < ops; ++step) {
      const Op op = next_op(rng, issued, mix);
      Collect expected;
      Collect actual;
      if (op.is_add) {
        ref.submit(op.order, expected);
        fast.submit(op.order, actual);
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

// LOB_DIFF_SEEDS overrides the seed count for long local runs (10,000 per WORKLOG S4.4).
std::uint32_t seed_count(std::uint32_t default_count) {
  const char* v = std::getenv("LOB_DIFF_SEEDS");
  return v != nullptr ? static_cast<std::uint32_t>(std::strtoul(v, nullptr, 10)) : default_count;
}

}  // namespace

TEST(Differential, AddCancel) {
  for (std::uint32_t seed = 1; seed <= 1000; ++seed) {
    run_seed(seed, 2000, Mix::AddOnly);
    if (::testing::Test::HasFailure()) break;  // stop at the first failing seed
  }
}

TEST(Differential, Match) {
  const std::uint32_t n = seed_count(1000);
  for (std::uint32_t seed = 1; seed <= n; ++seed) {
    run_seed(seed, 2000, Mix::Full);
    if (::testing::Test::HasFailure()) break;  // stop at the first failing seed
  }
}

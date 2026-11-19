#include "lob/price_bitmap.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <iterator>
#include <random>
#include <set>

using lob::kNoSlot;
using lob::PriceBitmap;

TEST(PriceBitmap, EmptyFindsNothing) {
  PriceBitmap b(4096);
  EXPECT_EQ(b.find_first(), kNoSlot);
  EXPECT_EQ(b.find_last(), kNoSlot);
  EXPECT_EQ(b.next_set(0), kNoSlot);
  EXPECT_EQ(b.prev_set(4095), kNoSlot);
}

TEST(PriceBitmap, SingleBit) {
  PriceBitmap b(4096);
  b.set(5);
  EXPECT_TRUE(b.test(5));
  EXPECT_EQ(b.find_first(), 5u);
  EXPECT_EQ(b.find_last(), 5u);
  EXPECT_EQ(b.next_set(0), 5u);
  EXPECT_EQ(b.next_set(5), 5u);
  EXPECT_EQ(b.next_set(6), kNoSlot);
  EXPECT_EQ(b.prev_set(5), 5u);
  EXPECT_EQ(b.prev_set(4), kNoSlot);
}

TEST(PriceBitmap, WordEdges) {
  PriceBitmap b(4096);
  b.set(63);
  b.set(64);
  b.set(127);
  b.set(128);
  EXPECT_EQ(b.next_set(63), 63u);
  EXPECT_EQ(b.next_set(64), 64u);
  EXPECT_EQ(b.next_set(65), 127u);
  EXPECT_EQ(b.next_set(128), 128u);
  EXPECT_EQ(b.prev_set(63), 63u);
  EXPECT_EQ(b.prev_set(62), kNoSlot);
  EXPECT_EQ(b.prev_set(64), 64u);
  EXPECT_EQ(b.prev_set(126), 64u);
  EXPECT_EQ(b.prev_set(127), 127u);
  EXPECT_EQ(b.find_first(), 63u);
  EXPECT_EQ(b.find_last(), 128u);
}

TEST(PriceBitmap, BandEnds) {
  PriceBitmap b(4096);
  b.set(0);
  b.set(4095);
  EXPECT_EQ(b.find_first(), 0u);
  EXPECT_EQ(b.find_last(), 4095u);
  EXPECT_EQ(b.next_set(1), 4095u);
  EXPECT_EQ(b.prev_set(4094), 0u);
  EXPECT_EQ(b.prev_set(4095), 4095u);
  EXPECT_EQ(b.next_set(4096), kNoSlot);
}

TEST(PriceBitmap, ClearLastBitEmptiesSummary) {
  PriceBitmap b(4096);
  b.set(70);
  b.clear(70);
  EXPECT_FALSE(b.test(70));
  EXPECT_EQ(b.find_first(), kNoSlot);
  EXPECT_EQ(b.find_last(), kNoSlot);
}

TEST(PriceBitmap, ClearKeepsOtherBits) {
  PriceBitmap b(4096);
  b.set(10);
  b.set(20);
  b.clear(10);
  EXPECT_EQ(b.find_first(), 20u);
  EXPECT_EQ(b.find_last(), 20u);
}

namespace {

void run_random(std::size_t slots, std::uint32_t seed, int ops) {
  PriceBitmap b(slots);
  std::set<std::size_t> ref;
  std::mt19937 rng(seed);
  std::uniform_int_distribution<std::size_t> pos(0, slots - 1);
  std::uniform_int_distribution<int> op(0, 99);
  for (int step = 0; step < ops; ++step) {
    const std::size_t i = pos(rng);
    const int o = op(rng);
    if (o < 35) {
      b.set(i);
      ref.insert(i);
    } else if (o < 60) {
      b.clear(i);
      ref.erase(i);
    } else if (o < 70) {
      ASSERT_EQ(b.test(i), ref.count(i) == 1) << "test " << i << " step " << step;
    } else if (o < 80) {
      const auto it = ref.lower_bound(i);
      const std::size_t want = it == ref.end() ? kNoSlot : *it;
      ASSERT_EQ(b.next_set(i), want) << "next_set(" << i << ") step " << step;
    } else if (o < 90) {
      std::size_t want = kNoSlot;
      const auto it = ref.upper_bound(i);
      if (it != ref.begin()) want = *std::prev(it);
      ASSERT_EQ(b.prev_set(i), want) << "prev_set(" << i << ") step " << step;
    } else {
      const std::size_t wf = ref.empty() ? kNoSlot : *ref.begin();
      const std::size_t wl = ref.empty() ? kNoSlot : *ref.rbegin();
      ASSERT_EQ(b.find_first(), wf) << "step " << step;
      ASSERT_EQ(b.find_last(), wl) << "step " << step;
    }
  }
}

}  // namespace

TEST(PriceBitmap, RandomAgainstStdSet) {
  run_random(4096, 1, 200000);
  run_random(130, 2, 200000);
  run_random(1, 3, 1000);
}

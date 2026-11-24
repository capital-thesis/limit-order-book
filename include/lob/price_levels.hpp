#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "lob/order_pool.hpp"
#include "lob/price_bitmap.hpp"
#include "lob/types.hpp"

namespace lob {

struct Level {
  PoolIndex head = kNullIndex;
  PoolIndex tail = kNullIndex;
  Qty total = 0;  // sum of leaves of the orders in the level
  std::uint32_t count = 0;
};

// One side of the book; a flat array of levels indexed by price over the band.
// A bitmap marks occupied levels. The best price is the highest occupied level
// for bids and the lowest for asks, so emptying a level refreshes the best price
// with no extra bookkeeping.
class PriceLevels {
public:
  PriceLevels(Price min_price, Price max_price, bool best_is_high)
      : min_(min_price),
        max_(max_price),
        levels_(static_cast<std::size_t>(max_price - min_price + 1)),
        bitmap_(levels_.size()),
        best_is_high_(best_is_high) {
    assert(min_price <= max_price);
    assert(levels_.size() <= PriceBitmap::kMaxSlots);
    }

    bool in_band(Price p) const noexcept { return p >= min_ && p <= max_; }

    Level& level(Price p ) noexcept { return levels_[slot_of(p)]; }
    const Level& level(Price p ) const noexcept { return levels_[slot_of(p)]; }

    // Appends order i at the tail of its level. The pool entry must already hold
    // price, total and filled.
    void push_back(OrderPool& pool, Price p, PoolIndex i) {
      Level& lv = level(p);
      PoolOrder& o = pool[i];
      o.prev = lv.tail;
      o.next = kNullIndex;
      if (lv.tail == kNullIndex) {
        lv.head = i;
      } else {
        pool[lv.tail].next = i;
      }
      lv.tail = i;
      lv.total += o.total - o.filled;
      if (lv.count == 0) bitmap_.set(slot_of(p));
      ++lv.count;
    }

    // Removes order i from its level in O(1). The order must be in that level.
    void unlink(OrderPool& pool, Price p, PoolIndex i) {
      Level& lv = level(p);
      PoolOrder& o = pool[i];
      if (o.prev == kNullIndex) {
        lv.head = o.next;
      } else {
        pool[o.prev].next = o.next;
      }
      if (o.next == kNullIndex) {
        lv.tail = o.prev;
      } else {
        pool[o.next].prev = o.prev;
      }
      lv.total -= o.total - o.filled;
      o.prev = kNullIndex;
      o.next = kNullIndex;
      assert(lv.count > 0);
      --lv.count;
      if (lv.count == 0) bitmap_.clear(slot_of(p));
    }
    
    Price min_price() const noexcept { return min_; }
    Price max_price() const noexcept { return max_; }
    bool occupied(Price p) const noexcept { return bitmap_.test(slot_of(p)); }
    
    // Called when a resting order fills `amount` and stays in its level.
    // Keeps the level total equal to the sum of leaves.
    void consume(Price p, Qty amount) noexcept { level(p).total -= amount; }

    // The next occupied level after p, moving away from the best price, or nullopt.
    std::optional<Price> next_worse(Price p) const noexcept {
      const std::size_t s = slot_of(p);
      const std::size_t t = best_is_high_
          ? (s == 0 ? kNoSlot : bitmap_.prev_set(s - 1))
          : bitmap_.next_set(s + 1);
      if (t == kNoSlot) return std::nullopt;
      return price_of(t);
    }

    std::optional<Price> best() const {
      const std::size_t s =
          best_is_high_ ? bitmap_.find_last() : bitmap_.find_first();
      if (s == kNoSlot) return std::nullopt;
      return price_of(s);
    }

private:
  std::size_t slot_of(Price p) const noexcept {
    assert(in_band(p));
    return static_cast<std::size_t>(p - min_);
  }
  Price price_of(std::size_t s) const noexcept {
    return min_ + static_cast<Price>(s);
  }

  Price min_;
  Price max_;
  std::vector<Level> levels_;
  PriceBitmap bitmap_;
  bool best_is_high_;
};

}  // namespace lob

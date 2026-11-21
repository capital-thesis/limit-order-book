#pragma once

#include <cassert>
#include <cstdint>
#include <optional>

#include "lob/id_map.hpp"
#include "lob/order_pool.hpp"
#include "lob/price_levels.hpp"
#include "lob/types.hpp"

namespace lob {

struct FastTotals {
  Qty submitted = 0;
  Qty increased = 0;
  Qty reduced = 0;
  Qty traded = 0;
  Qty cancelled = 0;
  Qty rejected = 0;
};

// Fast book, step 1: resting limit orders only. add() rests an order without
// matching, so the caller must not send an order that crosses the opposite best.
// Matching arrives in phase 4. Capacity must cover the peak number of resting orders.
class FastBook {
public:
  FastBook(Price min_price, Price max_price, std::uint32_t capacity)
      : pool_(capacity),
        index_(capacity),
        bids_(min_price, max_price, true),
        asks_(min_price, max_price, false) {}

  // Returns the id. Every call consumes one id, including rejected calls.
  template <class Sink>
  OrderId add(const NewOrder& o, Sink& sink) {
    const OrderId id = next_id_++;
    if (o.qty <= 0) {
      sink(ev::rejected(id, RejectReason::InvalidQty));
      return id;
    }
    if (!bids_.in_band(o.price)) {
      totals_.submitted += o.qty;
      totals_.rejected += o.qty;
      sink(ev::rejected(id, RejectReason::OutOfBand));
      return id;
    }
    const bool buy = o.side == Side::Buy;
    assert(!crosses(buy, o.price) && "add() does not match; the order would cross");

    totals_.submitted += o.qty;
    sink(ev::accepted(id));

    const PoolIndex i = pool_.alloc();
    assert(i != kNullIndex && "order pool is full");
    PoolOrder& p = pool_[i];
    p.id = id;
    p.price = o.price;
    p.total = o.qty;
    p.filled = 0;
    p.buy = buy;
    p.seq = next_seq_++;
    const bool inserted = index_.insert(id, i);
    assert(inserted);
    (void)inserted;

    side(buy).push_back(pool_, o.price, i);
    resting_ += o.qty;
    sink(ev::rested(id, o.price, o.qty));
    return id;
  }

  template <class Sink>
  void cancel(OrderId id, Sink& sink) {
    const IdMap::Value* found = index_.find(id);
    if (found == nullptr) {
      sink(ev::rejected(id, RejectReason::UnknownOrder));
      return;
    }
    const PoolIndex i = *found;
    const PoolOrder& o = pool_[i];
    const Qty leaves = o.total - o.filled;
    const Price price = o.price;
    const bool buy = o.buy;
    side(buy).unlink(pool_, price, i);
    index_.erase(id);
    pool_.release(i);
    resting_ -= leaves;
    totals_.cancelled += leaves;
    sink(ev::cancelled(id, CancelReason::UserCancel, leaves));
  }

  std::optional<Price> best_bid() const { return bids_.best(); }
  std::optional<Price> best_ask() const { return asks_.best(); }
  Qty resting_qty() const { return resting_; }
  std::size_t order_count() const { return index_.size(); }
  const FastTotals& totals() const { return totals_; }

private:
  PriceLevels& side(bool buy) { return buy ? bids_ : asks_;}

  bool crosses(bool buy, Price p) const {
    if (buy) {
      const std::optional<Price> a = asks_.best();
      return a && p >= *a;
    }
    const std::optional<Price> b = bids_.best();
    return b && p <= *b;
  }

  OrderPool pool_;
  IdMap index_;
  PriceLevels bids_;
  PriceLevels asks_;
  OrderId next_id_ = 1;
  Seq next_seq_ = 1;
  Qty resting_ = 0;
  FastTotals totals_;
};

}  // namespace lob

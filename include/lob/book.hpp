#pragma once

#include <cassert>
#include <cstdint>
#include <optional>
#include <stdexcept>

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

  // Full scan of both sides against the cached state. Throws std::logic_error on
  // the first violation. Call between operations only.
  void check_invariants() const {
    std::size_t count = 0;
    Qty resting = 0;
    std::optional<Price> bid_scan;
    std::optional<Price> ask_scan;
    check_side(bids_, true, count, resting, bid_scan);
    check_side(asks_, false, count, resting, ask_scan);
    require(bid_scan == best_bid(), "best bid differs from full scan");
    require(ask_scan == best_ask(), "best ask differs from full scan");
    require(!(bid_scan && ask_scan && *bid_scan >= *ask_scan), "crossed book at rest");
    require(count == index_.size(), "index size differs from book size");
    require(resting == resting_, "resting counter differs from scan");
    require(totals_.submitted + totals_.increased - totals_.reduced ==
                2 * totals_.traded + resting + totals_.cancelled + totals_.rejected,
            "global conservation identity");
  }  

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

  static void require(bool ok, const char* what) {
    if (!ok) throw std::logic_error(what);
  }

  void check_side(const PriceLevels& side, bool buy, std::size_t& count,
                  Qty& resting, std::optional<Price>& scan_best) const {
    for (Price p = side.min_price(); p <= side.max_price(); ++p) {
      const Level& lv = side.level(p);
      if (lv.count == 0) {
        require(!side.occupied(p), "bitmap marks an empty level");
        require(lv.head == kNullIndex && lv.tail == kNullIndex && lv.total == 0,
                "empty level has residue");
        continue;
      }
      require(side.occupied(p), "non-empty level not marked in bitmap");
      if (buy || !scan_best) scan_best = p;  // ascending scan: last hit is highest

      Qty level_sum = 0;
      std::uint32_t n = 0;
      PoolIndex prev = kNullIndex;
      for (PoolIndex i = lv.head; i != kNullIndex; i = pool_[i].next) {
        require(n < pool_.capacity(), "cycle in level list");
        require(pool_.is_live(i), "level list reaches a free slot");
        const PoolOrder& o = pool_[i];
        require(o.prev == prev, "broken prev link");
        require(o.buy == buy && o.price == p, "order stored under wrong side or level");
        const Qty leaves = o.total - o.filled;
        require(leaves > 0, "non-positive leaves in book");
        const IdMap::Value* found = index_.find(o.id);
        require(found != nullptr && *found == i, "index does not point at this order");
        level_sum += leaves;
        resting += leaves;
        ++n;
        ++count;
        prev = i;
      }
      require(lv.tail == prev, "tail does not match last node");
      require(lv.count == n, "level count differs from list length");
      require(lv.total == level_sum, "level total differs from sum of leaves");
    }
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

#pragma once
// Native reference limit order book: the oracle for differential testing.
// Clarity over speed. Standard library only. Semantics are defined by
// docs/CONTRACTS.md; if this file and the contract disagree, the contract wins
// and this file is the bug.

#include <algorithm>
#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <stdexcept>
#include <unordered_map>

#include "lob/types.hpp"

namespace lob {

struct Totals {
  Qty submitted = 0;
  Qty increased = 0;
  Qty reduced = 0;
  Qty traded = 0;
  Qty cancelled = 0;
  Qty rejected = 0;
};

namespace detail {

struct Order {
  OrderId id;
  Side side;
  Price price;
  Qty total;   // current total, changed only by modify
  Qty filled;
  Qty leaves;  // total - filled while live
  Seq seq;     // priority sequence; FIFO key within a level
};

using Queue = std::list<Order>;

struct Handle {
  Side side;
  Price price;
  Queue::iterator it;  // std::list iterators stay valid across other erasures
};

}  // namespace detail

class ReferenceBook {
 public:
  ReferenceBook(Price min_price, Price max_price)
      : min_(min_price), max_(max_price) {
    if (min_price > max_price) throw std::invalid_argument("empty price band");
  }

  // Every call consumes one id, including rejected submissions.
  template <class Sink>
  OrderId submit(const NewOrder& o, Sink& sink) {
    const OrderId id = next_id_++;
    if (o.qty <= 0) {
      sink(ev::rejected(id, RejectReason::InvalidQty));
      return id;
    }
    const bool market = o.type == OrderType::Market;
    if (!market && !in_band(o.price)) {
      reject_counted(id, o.qty, RejectReason::OutOfBand, sink);
      return id;
    }
    const bool buy = o.side == Side::Buy;
    if (market && opposite_empty(buy)) {
      reject_counted(id, o.qty, RejectReason::NoLiquidity, sink);
      return id;
    }
    if (o.tif == Tif::Fok &&
        crossable_qty(buy, market, o.price, o.qty) < o.qty) {
      reject_counted(id, o.qty, RejectReason::FokUnfillable, sink);
      return id;
    }

    totals_.submitted += o.qty;
    sink(ev::accepted(id));
    const Qty left = match(buy, market, o.price, id, o.qty, sink);
    if (left == 0) {
      sink(ev::filled(id));
    } else if (market || o.tif == Tif::Ioc) {
      totals_.cancelled += left;
      sink(ev::cancelled(id, CancelReason::IocRemainder, left));
    } else {
      rest(detail::Order{id, o.side, o.price, o.qty, o.qty - left, left,
                         next_seq_++},
            sink);
    }
    return id;
  }

  template <class Sink>
  void cancel(OrderId id, Sink& sink) {
    auto found = index_.find(id);
    if (found == index_.end()) {
      sink(ev::rejected(id, RejectReason::UnknownOrder));
      return;
    }
    const detail::Handle h = found->second;
    const Qty leaves = h.it->leaves;
    remove_handle(h, id);
    totals_.cancelled += leaves;
      sink(ev::cancelled(id, CancelReason::UserCancel, leaves));
  }
  
  // new_total is the TOTAL quantity, not remaining (FIX Cancel/Replace).
  template <class Sink>
  void modify(OrderId id, Qty new_total, Price new_price, Sink& sink) {
    auto found = index_.find(id);
    if (found == index_.end()) {
      sink(ev::rejected(id, RejectReason::UnknownOrder));
      return;
    }
    if (new_total <= 0) {
      sink(ev::rejected(id, RejectReason::InvalidQty));
      return;
    }
    if (!in_band(new_price)) {
      sink(ev::rejected(id, RejectReason::OutOfBand));
      return;
    }
    const detail::Handle h = found->second;  // copy: the node may move below
    const detail::Order cur = *h.it;
    if (new_total <= cur.filled) {
      sink(ev::rejected(id, RejectReason::QtyNotAboveFilled));
      return;
    }

    if (new_price == cur.price && new_total <= cur.total) {
      // Keeps priority: the order stays where it is in the level.
      const Qty delta = cur.total - new_total;
      h.it->total = new_total;
      h.it->leaves -= delta;
      totals_.reduced += delta;
      sink(ev::modified(id, new_price, new_total, h.it->leaves));
      return;
    }

    // Loses priority: remove it, apply the change, and re-enter as an aggressor.
    remove_handle(h, id);
    const Qty delta = new_total - cur.total;
    if (delta > 0) {
      totals_.increased += delta;
    } else {
      totals_.reduced -= delta;
    }
    detail::Order o = cur;
    o.price = new_price;
    o.total = new_total;
    o.leaves = new_total - o.filled;
    sink(ev::modified(id, new_price, new_total, o.leaves));
    reenter(o, sink);
  }

  std::optional<Price> best_bid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
  }

  std::optional<Price> best_ask() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
  }

  Qty resting_qty() const {
    Qty total = 0;
    for (const auto& entry : index_) total += entry.second.it->leaves;
    return total;
  }

  std::size_t order_count() const { return index_.size(); }
  const Totals& totals() const { return totals_; }

  // Throws std::logic_error on the first violation. Call between operations only.
  void check_invariants() const {
    if (best_bid() && best_ask() && *best_bid() >= *best_ask()) {
      require(false, "crossed book at rest");
    }
    std::size_t count = 0;
    Qty resting = 0;
    scan_levels(bids_, Side::Buy, count, resting);
    scan_levels(asks_, Side::Sell, count, resting);

    require(count == index_.size(), "index size differs from book size");
    for (const auto& entry : index_) {
      require(entry.second.it->id == entry.first, "index points at wrong order");
      require(entry.second.it->side == entry.second.side, "index side mismatch");
    }
    require(totals_.submitted + totals_.increased - totals_.reduced ==
                2 * totals_.traded + resting + totals_.cancelled +
                    totals_.rejected,
                "global conservation identity");
  }

private:
 using Order = detail::Order;
 using Queue = detail::Queue;
 using Handle = detail::Handle;

 static void require(bool ok, const char* what) {
   if (!ok) throw std::logic_error(what);
 }

 bool in_band(Price p) const { return p >= min_ && p <= max_; }

 bool opposite_empty(bool buy) const {
   return buy ? asks_.empty() : bids_.empty();
 }

 template <class Sink>
 void reject_counted(OrderId id, Qty qty, RejectReason r, Sink& sink) {
   totals_.submitted += qty;
   totals_.rejected += qty;
   sink(ev::rejected(id, r));
 }

 // Sum of leaves over opposite levels that cross the limit, stopping at the cap.
 template <class OppMap, class Crosses>
 static Qty sum_crossable(const OppMap& opp, Crosses crosses, Qty cap) {
   Qty total = 0;
   for (const auto& level : opp) {
     if (!crosses(level.first)) break;
     for (const Order& o : level.second) total += o.leaves;
     if (total >= cap) break; 
   }
   return total;
 }

 Qty crossable_qty(bool buy, bool market, Price limit, Qty cap) const {
   if (buy) {
     return sum_crossable(
         asks_, [=](Price p) { return market || p <= limit; }, cap);
   }
   return sum_crossable(
         bids_, [=](Price p) { return market || p >= limit; }, cap);
 }

 template <class Sink>
 Qty match(bool buy, bool market, Price limit, OrderId taker, Qty qty,
           Sink& sink) {
   if (buy) {
     return sweep(asks_, taker, qty,
                  [=](Price p) { return market || p <= limit; }, sink);
   }
   return sweep(bids_, taker, qty,
                  [=](Price p) { return market || p >= limit; }, sink);
 }

 // Walks opposite levels best-first while they cross. Each trade prints at the
 // maker's price, and a Filled for the maker follows its own Trade immediately.
 template <class OppMap, class Crosses, class Sink>
 Qty sweep(OppMap& opp, OrderId taker, Qty leaves, Crosses crosses,
           Sink& sink) {
    while (leaves > 0 && !opp.empty()) {
      auto lvl = opp.begin();
      if (!crosses(lvl->first)) break;
      Queue& q = lvl->second;
      while (leaves > 0 && !q.empty()) {
        Order& m = q.front();
        const Qty fill = std::min(leaves, m.leaves);
        m.leaves -= fill;
        m.filled += fill;
        leaves -= fill;
        totals_.traded += fill;
        sink(ev::trade(m.id, taker, lvl->first, fill));
        if (m.leaves == 0) {
          const OrderId mid = m.id;
          index_.erase(mid);
          q.pop_front();
          sink(ev::filled(mid));
        }
      }
      if (q.empty()) opp.erase(lvl);
    }
    return leaves;
  }

  // Re-entry after a priority-losing modify. The order is not in the book.
  template <class Sink>
  void reenter(detail::Order o, Sink& sink) {
    const bool buy = o.side == Side::Buy;
    const Price limit = o.price;
    const Qty left =
        buy ? sweep(asks_, o.id, o.leaves,
                    [=](Price p) { return p <= limit; }, sink)
            : sweep(bids_, o.id, o.leaves,
                    [=](Price p) { return p >= limit; }, sink);
    if (left == 0) {
      sink(ev::filled(o.id));
      return;
    }
    o.leaves = left;
    o.filled = o.total - left;
    o.seq = next_seq_++;
    rest(o, sink);
  }

  template <class Sink>
  void rest(const detail::Order& o, Sink& sink) {
    if (o.side == Side::Buy) {
      Queue& q = bids_[o.price];
      q.push_back(o);
      index_[o.id] = Handle{Side::Buy, o.price, std::prev(q.end())};
    } else {
      Queue& q = asks_[o.price];
      q.push_back(o);
      index_[o.id] = Handle{Side::Sell, o.price, std::prev(q.end())};
    }
    sink(ev::rested(o.id, o.price, o.leaves));
  }

  void remove_handle(const Handle& h, OrderId id) {
    if (h.side == Side::Buy) {
      erase_node(bids_, h.price, h.it);
    } else {
      erase_node(asks_, h.price, h.it);
    }
    index_.erase(id);
  }

  template <class Map>
  static void erase_node(Map& levels, Price price, Queue::iterator it) {
    auto lvl = levels.find(price);
    lvl->second.erase(it);
    if (lvl->second.empty()) levels.erase(lvl);
  }

  template <class Map>
  static void scan_levels(const Map& levels, Side side, std::size_t& count,
                          Qty& resting) {
    for (const auto& level : levels) {
      require(!level.second.empty(), "empty level kept in book");
      Seq prev = 0;
      for (const Order& o : level.second) {
        ++count;
        require(o.side == side && o.price == level.first,
                "order stored under wrong level");
        require(o.leaves > 0, "non-positive leaves in book");
        require(o.total == o.filled + o.leaves, "pre-order identity");
        require(o.seq > prev, "FIFO violated within level");
        prev = o.seq;
        resting += o.leaves;
      }
    }
  }

  Price min_;
  Price max_;
  OrderId next_id_ = 1;
  Seq next_seq_ = 1;
  // Best bid is begin() of bids_ (descending). Best ask is begin() of asks_.
  std::map<Price, Queue, std::greater<Price>> bids_;
  std::map<Price, Queue> asks_;
  std::unordered_map<OrderId, Handle> index_;
  Totals totals_;
};

}  // namespace lob

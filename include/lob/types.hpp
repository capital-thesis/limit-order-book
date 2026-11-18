#pragma once
// Shared vocabulary for the reference book and the fast book.
// Normative semantics live in docs/CONTRACTS.md, not in the file.

#include <cstdint>

namespace lob {

using Price = std::int64_t;     // ticks
using Qty = std::int64_t;       // lots
using OrderId = std::uint64_t;  // engine-assigned, starts at 1
using Seq = std::uint64_t;      // priority sequence, assigned when an order starts resting

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };
enum class Tif : std::uint8_t { Gtc, Ioc, Fok };

enum class RejectReason : std::uint8_t {
    None,
    InvalidQty,
    OutOfBand,
    NoLiquidity,
    FokUnfillable,
    UnknownOrder,
    QtyNotAboveFilled,
};

enum class CancelReason : std::uint8_t { None, UserCancel, IocRemainder };

enum class EventKind : std::uint8_t {
    Accepted,
    Rejected,
    Rested,
    Trade,
    Filled,
    Cancelled,
    Modified,
};

struct NewOrder {
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    Tif tif = Tif::Gtc;
    Price price = 0;  // ignored for market orders
    Qty qty = 0;
};

// Fixed-size POD. No heap. Fields not used by a kind are zero.
struct Event {
    EventKind kind = EventKind::Accepted;
    RejectReason reject = RejectReason::None;  // Rejected
    CancelReason cancel = CancelReason::None;  // Cancelled
    OrderId order = 0;  // Accepted, Rejected, Rested, Filled, Cancelled, Modified
    OrderId maker = 0;  // Trade
    OrderId taker = 0;  // Trade
    Price price = 0;    // Rested, Modified, Trade (maker's price)
    Qty qty = 0;        // Trade (fill qty)
    Qty leaves = 0;     // Rested, Cancelled, Modified
    Qty total = 0;      // Modified (new total)

    friend bool operator==(const Event& a, const Event& b) {
        return a.kind == b.kind && a.reject == b.reject && a.cancel == b.cancel &&
               a.order == b.order && a.maker == b.maker && a.taker == b.taker &&
               a.price == b.price && a.qty == b.qty && a.leaves == b.leaves &&
               a.total == b.total;
    }
    friend bool operator!=(const Event& a, const Event& b) { return !(a == b); }
};

// Constructors for each event kind. Keeps call sites readable in both books and tests.
namespace ev {

inline Event accepted(OrderId id) {
    Event e;
    e.kind = EventKind::Accepted;
    e.order = id;
    return e;
}

inline Event rejected(OrderId id, RejectReason r) {
    Event e;
    e.kind = EventKind::Rejected;
    e.order = id;
    e.reject = r;
    return e;
}

inline Event rested(OrderId id, Price px, Qty leaves) {
    Event e;
    e.kind = EventKind::Rested;
    e.order = id;
    e.price = px;
    e.leaves = leaves;
    return e;
}

inline Event trade(OrderId maker, OrderId taker, Price px, Qty qty) {
    Event e;
    e.kind = EventKind::Trade;
    e.maker = maker;
    e.taker = taker;
    e.price = px;
    e.qty = qty;
    return e;
}

inline Event filled(OrderId id) {
    Event e;
    e.kind = EventKind::Filled;
    e.order = id;
    return e;
}

inline Event cancelled(OrderId id, CancelReason r, Qty leaves) {
    Event e;
    e.kind = EventKind::Cancelled;
    e.order = id;
    e.cancel = r;
    e.leaves = leaves;
    return e;
}

inline Event modified(OrderId id, Price px, Qty total, Qty leaves) {
    Event e;
    e.kind = EventKind::Modified;
    e.order = id;
    e.price = px;
    e.total = total;
    e.leaves = leaves;
    return e;
}

}  // namespace ev

}  // namespace lob

# Engine Contracts (v0.3)

These are the normative semantics. The reference book implements them, the differential test checks against them, and the fast book must match them event for event. Anything not stated here is undefined and must not relied on.

## 0. Changes from v0.2

- **Modify quantity is the new TOTAL quantity** (FIX Order Cancel/Replace semantics), not remaining. Example: submit 200, fill 100, modify total to 150 gives 50 resting.
- **Accounting identity restarted** with `increased` and `reduced` counters. Under total semantics a modify changes the order's total, so it is not a new submission.
- **Ack-first** for every operation. Maker events come directly after their `Trade`, not batched at the end.
- **L2 is not in the event stream.** The book emits L3 events only. L2 is derived downstream.
- **`Rested` event added** so the L3 stream alone can track order state.
- **`Expired` removed.** An IOC remainder is `Cancelled(IocRemainder)`.
- **`ModifyNotAllowed` removed.** Market orders never rest, so they are unknown to cancel and modify.
- **`SelfTradeCancel` removed as a kind.** Self-trade prevention, when it lands, emits `Cancelled(SelfTrade)`. The reason is reserved and not implemented in phase 1.
- **Stream generator** replaced by the six configurations in section 11.

## 1. Identifiers and units

- **Order id:** engine-assigned, monotonic `uint64`, starting at 1. Every call to `submit` consumes one id, including rejected submissions. The id is stable for the life of the order. Modify does not change it.
- **Priority sequence:** monotonic `uint64`, assigned whenever an order starts resting. FIFO within a level is defined by this sequence, not by the id.
- **Price:** `int64` ticks, inclusive band `[min, max]` given at construction.
- **Qty:** `int64` lots. A submission with qty ≤ 0 is invalid.
- Client ids and client timestamps are ignored.

## 2. Order types

- **Limit** with TIF `GTC`, `IOC`, or `FOK`.
- **Market** has no price and behaves as IOC. A market `FOK` is all-or-nothing. A market order never rests, and after it finishes its id is unknown to cancel and modify.

## 3. Events (L3)

Every event is a fixed-size POD. Fields not listed for a kind are zero.

| Kind | Fields | Meaning |
|---|---|---|
| `Accepted` | order | The submission passed validation and is now matching. |
| `Rejected` | order, reject | Nothing changed (except submitted/rejected accounting where stated). Terminal for that operation. |
| `Rested` | order, price, leaves | The order is now in the book. |
| `Trade` | maker, taker, price (maker's price), qty | One fill. |
| `Filled` | order | The order is fully executed and gone from the book. | 
| `Cancelled` | order, cancel, leaves | Removed from the book with `leaves` quantity. Reason is `UserCancel` or `IocRemainder`. |
| `Modified` | order, price, total, leaves | The modify was accepted. The new total and price are in effect. |

Reject reasons: `InvalidQty`, `OutOfBand`, `NoLiquidity`, `FokUnfillable`, `UnknownOrder`, `QtyNotAboveFilled`.

## 4. Per-operation event sequences (normative)

The order of events within one operation is the list order below.

### Submit
- qty ≤ 0: `Rejected(InvalidQty)`. The id is consumed. No accounting changes.
- Limit price outside the band: `Rejected(OutOfBand)`. Counted as rejected.
- Market order with the opposite side empty: `Rejected(NoLiquidity)`. Counted as rejected.
- FOK whose crossable quantity is less than qty: `Rejected(FokUnfillable)`. Counted as rejected. Book unchanged.
- Otherwise:
  1. `Accepted(id)`.
  2. For each fill against a maker: `Trade`, then `Filled(maker)` if the maker is exhausted.
  3. A terminal event for the taker:
     - leaves = 0: `Filled(taker)`.
     - Market or IOC with leaves > 0: `Cancelled(IocRemainder, leaves)`.
     - GTC limit with leaves > 0: `Rested(price, leaves)`.

Agressor cases, explicitly:
- **Market, full fill:** `Accepted`, `Trade`..., `Filled(maker)` as they exhaust, `Filled(taker)`.
- **IOC, partial:** `Accepted`, `Trade`..., `Cancelled(IocRemainder, leaves)`.
- **Aggressor limit, partial, rests:** `Accepted`, `Trade`..., `Rested(price, leaves)`.

### Cancel
- Id resting: `Cancelled(UserCancel, leaves)`. Accounting: `cancelled += leaves`.
- Id not resting (unknown, finished, or market): `Rejected(UnknownOrder)`. No state change.

### Modify (id, new_total, new_price)
- Id not resting: `Rejected(UnknownOrder)`.
- new_total ≤ 0: `Rejected(InvalidQty)`.
- new_price outside the band: `Rejected(OutOfBand)`.
- new_total ≤ filled: `Rejected(QtyNotAboveFilled)`. To finish an order, cancel it.
- Otherwise `Modified(id, new_price, new_total, leaves)` is emitted first, always.
  - **No-op** (same price and same total): emits exactly one `Modified` with the unchanged `leaves`. It changes no state: no priority change, no 'Rested', and no change to any `Totals` counter.
  - **Keeps priority** (same price and new_total ≤ current total): nothing else is emitted. Position in the level is changed.
  - **Loses priority** (new price, or new_total > current total): the order is removed and re-enters as an aggressor with a new priority sequence. Then:
    - `Trade`..., `Filled(maker)` as they exhaust, then `Filled(order)` if leaves = 0 or `Rested(new_price, leaves)`.

Modify and cancel of an order that is not resting always return `UnknownOrder`, which covers market orders.

## 5. Priority

- Price-time. Best price first. FIFO within a level by priority sequence.
- Modify keeps priority only for a same-price decrease or no-op. Any price change or increase loses it, and the order goes to the back of its new level.

## 6. Accounting

Per live order: `total = filled + leaves`. A live order has no cancelled part, because cancelling removes it. Total changes only through modify.

Global counters (`Totals`):
- `submitted`: sum of qty of submissions that passed validation, including those later rejected. Invalid qty is excluded.
- `increased`: sum of positive total deltas from modify.
- `reduced`: sum of negative total deltas from modify.
- `traded`: sum of trade qty, counted once per trade.
- `cancelled`: sum of `leaves` removed by `Cancelled`.
- `rejected`: qty of submissions rejected after validation (`OutOfBand`, `NoLiquidity`, `FokUnfillable`).

**Global identity, checked between operations:**

`submitted + increased - reduced = 2.traded + resting + cancelled + rejected`

Each trade fills one buy and sell, so it appears twice on the right. `resting` is the sum of leaves over all live orders. Rejected modifies change nothing.

Example: buy 10 rests. Sell 4 arrives. Modify the buy to total 20, then to total 8.
- submitted = 14, traded = 4, resting = 4, increased = 10, reduced = 12 → 14 + 10 - 12 = 12 = 8 + 4 ✓.

## 7. Matching rules

- A trade prints at the maker's price.
- A market order crosses every level. A buy limit crosses asks at or below its price. A sell limit crosses bids at or above its price.
- The FOK pre-check sums leaves over crossing levels and stops early once qty is reached. No events are emitted before the decision.
- Self-trade prevention is off. It is reserved for a later phase.

## 8. Price band

Limit prices must lie in `[min, max]`, inclusive. Both bounds are constructor arguments of the reference book. Market orders carry no price.

## 9. Invariant timing

- The not-crossed invariant (`best_bid < best_ask` when both sides exist) is checked only between operations.
- Mid-match, the book may be transiently crossed. For example, an aggressor may have consumed the best levels on the opposite side while its remainder has not yet been placed. This is allowed and is never observable through the API.
- The reference book completes each operation within one call, so the caller never observes a crossed book from it.
- The fast book may be transiently crossed inside a match, as above. Only its state between calls is constrained.

## 10. Interface (identical for reference and fast book)

```
OrderId submit(const NewOrder&, Sink&);
void    cancel(OrderId, Sink&);
void    modify(OrderId, Qty new_total, Price new_price, Sink&);
```
`Sink` is any callable taking `const Event&`. Queries: `best_bid()`, `best_ask()`, `resting_qty()`, `totals()`, `check_invariants()`. The differential test drives both books through this one interface and does not translate between APIs.

## 11. Differential stream generator (specification)

Phase 1 implements none of this. It is spec only. The generator arrives with the differential test in phase 3.

Six configurations. Each is run against both books, with event streams compared element-wise after every operation.

| Config | Shape |
|---|---|
| `near_touch` | Prices within ±2 ticks of mid. High match rate. |
| `far_from_touch` | Prices within ±20 ticks of mid. Mostly resting. |
| `bursty` | Poisson arrivals with 10x burst periods. |
| `cancel_heavy` | 80% cancels. |
| `modify_heavy` | 50% modifies, mixed up, down, and price change. |
| `edge` | Prices at band min and max. Quantities of 1 and of max. |

Common settings:
- Default mix: add 60%, cancel 30%, modify 10%. Pre-config mixes override this.
- Quantity roughly geometric over 1 to 100. About 1% invalid (zero or negative).
- TIF: GTC 70%, IOC 15%, FOK 15%. About 10% of adds are market orders.
- About 5% of cancels and modifies target finished ids.
- Run sizes: 10k operations per seed in Ci. 1M operations per config in the nightly run.
- Equality is `Event::operator==` over every field. Do not use `memcmp`, because padding is not defined.
- A failing seed and config are logged so the run can be replayed.

## 12. Stops (later phase)

- Trigger source: last trade price.
- Triggers are evaluated after the triggering operation completes, not mid-match.
- Triggered orders enter the matcher in stop sequence order.
- Cascades are bounded by a depth limit of 16. Exceeding it is documented and tested.

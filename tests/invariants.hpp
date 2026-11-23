#pragma once

#include <gtest/gtest.h>

#include "lob/book.hpp"
#include "reference_book.hpp"

namespace testing_support {

// Compares the observable state of the two books. Cheap: constant time.
inline void compare_state(const lob::ReferenceBook& ref, const lob::FastBook& fast) {
    ASSERT_EQ(ref.best_bid(), fast.best_bid());
    ASSERT_EQ(ref.best_ask(), fast.best_ask());
    ASSERT_EQ(ref.resting_qty(), fast.resting_qty());
    ASSERT_EQ(ref.order_count(), fast.order_count());
    const lob::Totals& rt = ref.totals();
    const lob::FastTotals& ft = fast.totals();
    ASSERT_EQ(rt.submitted, ft.submitted);
    ASSERT_EQ(rt.increased, ft.increased);
    ASSERT_EQ(rt.reduced, ft.reduced);
    ASSERT_EQ(rt.traded, ft.traded);
    ASSERT_EQ(rt.cancelled, ft.cancelled);
    ASSERT_EQ(rt.rejected, ft.rejected);
}

// Runs both full-scan checkers. Linear in the number of resting orders.
inline void check_both(const lob::ReferenceBook& ref, const lob::FastBook& fast) {
    ref.check_invariants();
    fast.check_invariants();
}

inline void expect_same_state(const lob::ReferenceBook& ref, const lob::FastBook& fast) {
    compare_state(ref, fast);
    check_both(ref, fast);
}

}  // namespace testing_support

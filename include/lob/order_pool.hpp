#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace lob {

using PoolIndex = std::uint32_t;
inline constexpr PoolIndex kNullIndex = 0xFFFF'FFFFu;

// One slot in the slab. Fields are placeholders for the book's order state;
// `next` doubles as the free-list link while the slot is free.
struct PoolOrder {
  std::uint64_t id = 0;
  std::int64_t price = 0;
  std::int64_t total = 0;
  std::int64_t filled = 0;
  std::uint64_t seq = 0;
  PoolIndex next = kNullIndex;
  PoolIndex prev = kNullIndex;
  bool live = false;
};

// Preallocated slab with an intrusive free list. All storage is allocated in
// the constructor; alloc() and release() never allocate.
class OrderPool {
public:
  explicit OrderPool(std::uint32_t capacity)
      : nodes_(capacity),
        free_head_(capacity == 0 ? kNullIndex : 0),
        free_count_(capacity) {
    assert(capacity < kNullIndex);
    for (std::uint32_t i = 0; i < capacity; ++i) {
      nodes_[i].next = (i + 1 < capacity) ? i + 1 : kNullIndex;
    }
  }

  // Returns kNullIndex when the pool is full.
  PoolIndex alloc() noexcept {
    if (free_head_ == kNullIndex) return kNullIndex;
    const PoolIndex i = free_head_;
    PoolOrder& o = nodes_[i];
    free_head_ = o.next;
    o = PoolOrder{};
    o.live = true;
    --free_count_;
    return i;
  }

  // Returns false for an out-of-range index, the null index, or a slot that is
  // not live (double free). Nothing changes in that case.
  bool release(PoolIndex i) noexcept {
    if (i >= nodes_.size()) return false;
    PoolOrder& o = nodes_[i];
    if (!o.live) return false;
    o.live = false;
    o.next = free_head_;
    free_head_ = i;
    ++free_count_;
    return true;
  }

  bool is_live(PoolIndex i) const noexcept {
    return i < nodes_.size() && nodes_[i].live;
  }

  PoolOrder& operator[](PoolIndex i) noexcept {
    assert(i < nodes_.size());
    return nodes_[i];
  }
  const PoolOrder& operator[](PoolIndex i) const noexcept {
    assert(i < nodes_.size());
    return nodes_[i];
  }

  std::size_t capacity() const noexcept { return nodes_.size(); }
  std::size_t in_use() const noexcept { return nodes_.size() - free_count_; }

private:
  std::vector<PoolOrder> nodes_;
  PoolIndex free_head_;
  std::size_t free_count_;
};

}  // namespace lob

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace lob {

// Open-addressing map from order id to a 32-bit slot index.
// Power-of-two table, linear probing, load factor <= 0.5, and backward-shift
// erase so no tombstones are left behind. Key 0 is reserved as the empty marker;
// order ids start at 1 (CONTRACTS §1).
class IdMap {
public:
  using Key = std::uint64_t; 
  using Value = std::uint32_t;
  static constexpr Key kEmpty = 0;
  static constexpr std::size_t npos = static_cast<std::size_t>(-1);

  explicit IdMap(std::size_t min_slots = 16) {
    std::size_t cap = 16;
    while (cap < min_slots * 2) cap <<= 1;
    keys_.assign(cap, kEmpty);
    vals_.assign(cap, 0);
    mask_ = cap - 1;
  }

  // Return false if the key is already present; the stored value is unchanged.
  bool insert(Key k, Value v) {
    assert(k != kEmpty);
    if ((size_ + 1) * 2 > keys_.size()) grow();
    std::size_t i = home(k);
    while (keys_[i] != kEmpty) {
      if (keys_[i] == k) return false;
      i = (i + 1) & mask_;
    }
    keys_[i] = k;
    vals_[i] = v;
    ++size_;
    return true;
  }

  // Returns false if the key is absent.
  bool erase(Key k) noexcept {
    std::size_t hole = slot_of(k);
    if (hole == npos) return false;
    --size_;
    std::size_t j = hole;
    for (;;) {
      j = (j + 1) & mask_;
      if (keys_[j] == kEmpty) break;
      const std::size_t h = home(keys_[j]);
      // The entry at j may stay if its home lies cyclically in (hole, j).
      const bool stays = (hole <= j) ? (hole < h && h <= j) : (hole < h || h <= j);
      if (stays) continue;
      keys_[hole] = keys_[j];
      vals_[hole] = vals_[j];
      hole = j;
    }
    keys_[hole] = kEmpty;
    vals_[hole] = 0;
    return true;
  }

  Value* find(Key k) noexcept {
    const std::size_t i = slot_of(k);
    return i == npos ? nullptr : &vals_[i];
  }
  const Value* find(Key k) const noexcept {
    const std::size_t i = slot_of(k);
    return i == npos ? nullptr : &vals_[i];
  }

  std::size_t size() const noexcept { return size_; }
  std::size_t capacity() const noexcept { return keys_.size(); }

private:
  static std::uint64_t mix(std::uint64_t x) noexcept {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
  }

  std::size_t home(Key k) const noexcept {
    return static_cast<std::size_t>(mix(k)) & mask_;
  }

  std::size_t slot_of(Key k) const noexcept {
    std::size_t i = home(k);
    while (keys_[i] != kEmpty) {
      if (keys_[i] == k) return i;
      i = (i + 1) & mask_;
    }
    return npos;
  }

  void grow() {
    std::vector<Key> old_keys = std::move(keys_);
    std::vector<Value> old_vals = std::move(vals_);
    const std::size_t cap = old_keys.size() * 2;
    keys_.assign(cap, kEmpty);
    vals_.assign(cap, 0);
    mask_ = cap - 1;
    size_ = 0;
    for (std::size_t i = 0; i < old_keys.size(); ++i) {
        if (old_keys[i] != kEmpty) insert(old_keys[i], old_vals[i]);
    }
  }

  std::vector<Key> keys_;
  std::vector<Value> vals_;
  std::size_t mask_ = 0;
  std::size_t size_ = 0;
};

}  // namespace lob
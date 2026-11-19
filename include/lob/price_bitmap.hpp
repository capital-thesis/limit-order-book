#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace lob {

inline constexpr std::size_t kNoSlot = ~std::size_t{0};

// Two-level 64-ary bitmap over up to 4096 price slots.
// words_[w] holds slots 64*w .. 64*w+63. summary_ bit w is set iff words_[w] != 0.
// Finds the lowest and highest set slot, and the nearest set slot on either side
// of a position, in a fixed number of word operations.
// Uses __builtin_ctzll and __builtin_clzll, which GCC and Clang provide.
class PriceBitmap {
public:
  static constexpr std::size_t kWords = 64;
  static constexpr std::size_t kMaxSlots = kWords * 64;

  explicit PriceBitmap(std::size_t slots) : slots_(slots) {
    assert(slots <= kMaxSlots);
  }

  void set(std::size_t i) noexcept {
    assert(i < slots_);
    const std::size_t w = i >> 6;
    words_[w] |= bit(i & 63);
    summary_ |= bit(w);
  }

  void clear(std::size_t i) noexcept {
    assert(i < slots_);
    const std::size_t w = i >> 6;
    words_[w] &= ~bit(i & 63);
    if (words_[w] == 0) summary_ &= ~bit(w);
  }

  bool test(std::size_t i) const noexcept {
    assert(i < slots_);
    return ((words_[i >> 6] >> (i & 63)) & 1u) != 0;
  }

  // Lowest set slot, or kNoSlot if none.
  std::size_t find_first() const noexcept {
    if (summary_ == 0) return kNoSlot;
    const std::size_t w = lowest(summary_);
    return (w << 6) | lowest(words_[w]);
  }

  // Highest set slot, or kNoSlot if none.
  std::size_t find_last() const noexcept {
    if (summary_ == 0) return kNoSlot;
    const std::size_t w = highest(summary_);
    return (w << 6) | highest(words_[w]);
  }

  // Smallest set slot >= i, or kNoSlot if none.
  std::size_t next_set(std::size_t i) const noexcept {
    if (i >= kMaxSlots) return kNoSlot;
    std::size_t w = i >> 6;
    const std::uint64_t m = words_[w] & (~std::uint64_t{0} << (i & 63));
    if (m != 0) return (w << 6) | lowest(m);
    if (w + 1 >= kWords) return kNoSlot;
    const std::uint64_t s = summary_ & (~std::uint64_t{0} << (w + 1));
    if (s == 0) return kNoSlot;
    w = lowest(s);
    return (w << 6) | lowest(words_[w]);
  }

  // Largest set slot <= i, or kNoSlot if none.
  std::size_t prev_set(std::size_t i) const noexcept {
    if (summary_ == 0) return kNoSlot;
    if (i >= kMaxSlots) i = kMaxSlots - 1;
    std::size_t w = i >> 6;
    const std::uint64_t m = words_[w] & (~std::uint64_t{0} >> (63 - (i & 63)));
    if (m != 0) return (w << 6) | highest(m);
    if (w == 0) return kNoSlot;
    const std::uint64_t s = summary_ & ((std::uint64_t{1} << w) - 1);
    if (s == 0) return kNoSlot;
    w = highest(s);
    return (w << 6) | highest(words_[w]);
  }

  std::size_t slots() const noexcept { return slots_; }

private:
  static std::uint64_t bit(std::size_t b) noexcept { return std::uint64_t{1} << b; }
  static std::size_t lowest(std::uint64_t x) noexcept {
    return static_cast<std::size_t>(__builtin_ctzll(x));  // x != 0
  }
  static std::size_t highest(std::uint64_t x) noexcept {
    return static_cast<std::size_t>(63 - __builtin_clzll(x));  // x != 0
  }

  std::array<std::uint64_t, kWords> words_{};
  std::uint64_t summary_ = 0;
  std::size_t slots_;
};

}  // namespace lob

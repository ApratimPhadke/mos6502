// trace.hpp - a fixed-size ring buffer of the most recent instructions.
//
// DATA-STRUCTURE CHOICE
// When a program goes wrong you want to see the LAST few hundred instructions,
// not the first. A ring buffer stores a fixed number of entries and silently
// overwrites the oldest one: O(1) push, no allocation, no memmove.
// Capacity is a power of two so "wrap around" is a bit-mask (& (N-1)) instead
// of a division.
#pragma once

#include <array>
#include <cstddef>

#include "mos6502/cpu.hpp"
#include "mos6502/types.hpp"

namespace mos6502 {

// Snapshot taken BEFORE an instruction executes.
struct TraceEntry {
  Cpu::Registers regs{};
  std::array<u8, 3> bytes{};  // opcode + up to two operand bytes at regs.pc
  u64 cycle = 0;              // total cycle count before the instruction
};

template <std::size_t Capacity>
class TraceBuffer {
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "Capacity must be a power of two");

 public:
  void push(const TraceEntry& entry) noexcept {
    entries_[head_ & (Capacity - 1)] = entry;
    ++head_;
  }

  [[nodiscard]] std::size_t size() const noexcept { return head_ < Capacity ? head_ : Capacity; }

  // index 0 = oldest retained entry, size()-1 = newest. Precondition: i < size().
  [[nodiscard]] const TraceEntry& at(std::size_t i) const noexcept {
    const std::size_t first = head_ < Capacity ? 0 : head_ - Capacity;
    return entries_[(first + i) & (Capacity - 1)];
  }

  void clear() noexcept { head_ = 0; }

 private:
  std::array<TraceEntry, Capacity> entries_{};
  std::size_t head_ = 0;  // total pushes so far (monotonic)
};

}  // namespace mos6502

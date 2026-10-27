// types.hpp - fixed-width types, address-space constants and tiny bit helpers.
//
// Every constant here is defined by the 6502 hardware, not by our design:
//   * the vectors live at $FFFA-$FFFF            [REF-1], BRK/IRQ vector [REF-3]
//   * the stack lives in page 1, $0100-$01FF     [REF-1]
// [REF-1] is a Commodore 64 memory map: it confirms these CPU-defined facts but
// ALSO lists many things that belong to that machine only (VIC-II, SID, the
// $0001 bank-switch port...). See docs/REFERENCES.md for how we separate them.
// Everything else in the memory map is defined by the *machine* around the CPU
// and therefore lives in the Bus, not here (see docs/REFERENCES.md, REF-1).
#pragma once

#include <concepts>
#include <cstdint>

namespace mos6502 {

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

inline constexpr u32 kAddressSpace = 0x10000;  // 64 KiB: 16 address lines
inline constexpr u32 kPageSize = 0x100;        // 256 bytes: "page" = high byte
inline constexpr u32 kPageCount = kAddressSpace / kPageSize;

inline constexpr u16 kStackBase = 0x0100;
inline constexpr u16 kNmiVector = 0xFFFA;
inline constexpr u16 kResetVector = 0xFFFC;
inline constexpr u16 kIrqVector = 0xFFFE;  // shared by IRQ and BRK

// Keep the low / high 8 bits of any integer expression. Arithmetic on u8 in C++
// is done in `int`, so every narrowing must be explicit (we build with
// -Wconversion) and these helpers make the intent visible at the call site.
template <std::integral T>
constexpr u8 lo8(T v) noexcept {
  return static_cast<u8>(static_cast<unsigned>(v) & 0xFFu);
}
template <std::integral T>
constexpr u8 hi8(T v) noexcept {
  return static_cast<u8>((static_cast<unsigned>(v) >> 8) & 0xFFu);
}

// The 6502 is little-endian: the low byte comes first in memory.
constexpr u16 make_word(u8 lo, u8 hi) noexcept {
  return static_cast<u16>(lo | (hi << 8));
}

// True when two addresses are in different 256-byte pages. Crossing a page
// boundary costs the real chip one extra cycle in several addressing modes.
constexpr bool crosses_page(u16 from, u16 to) noexcept {
  return ((from ^ to) & 0xFF00u) != 0;
}

}  // namespace mos6502

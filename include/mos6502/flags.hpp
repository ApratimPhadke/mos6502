// flags.hpp - bit masks for the processor status register (P).
//
//   bit:  7 6 5 4 3 2 1 0
//         N V U B D I Z C
//
// [REF-7] P is ONE byte. We store it as a plain u8 and use masks instead of a
// bit-field union: bit-field layout is implementation-defined, and reading a
// union member other than the one last written is undefined behaviour in C++.
#pragma once

#include "mos6502/types.hpp"

namespace mos6502::flag {

inline constexpr u8 C = 0x01;  // Carry
inline constexpr u8 Z = 0x02;  // Zero
inline constexpr u8 I = 0x04;  // Interrupt disable
inline constexpr u8 D = 0x08;  // Decimal mode
inline constexpr u8 B = 0x10;  // Break: exists only in the COPY pushed on the stack
inline constexpr u8 U = 0x20;  // Unused: always reads back as 1
inline constexpr u8 V = 0x40;  // Overflow
inline constexpr u8 N = 0x80;  // Negative

}  // namespace mos6502::flag

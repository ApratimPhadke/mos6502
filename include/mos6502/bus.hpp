// bus.hpp - the address space seen by the CPU.
//
// DATA-STRUCTURE CHOICE
// The 6502 has a 16-bit address, so there are only 65536 addresses. We split
// them into 256 pages of 256 bytes. The high byte of an address IS the page
// number, so decoding an address is one shift and one array index: O(1).
//
//   address 0xC123  ->  page = 0xC123 >> 8 = 0xC1,  offset = 0x23
//
// Each page can be mapped as RAM, ROM, a memory-mapped device, or left
// unmapped. (An ordered map of address ranges would cost O(log n) and a linear
// list of ranges O(n) per access - and we access memory on every cycle.)
//
// [REF-1] Real machines do exactly this kind of decoding; the Commodore 64
// even changes the mapping at run time through a port at $0001.
#pragma once

#include <array>
#include <optional>
#include <span>

#include "mos6502/types.hpp"

namespace mos6502 {

// A memory-mapped peripheral (UART, timer, GPIO...). Devices see the full
// 16-bit address so one object can serve several pages. Implementations must
// not throw. The Bus does not own devices; the protected non-virtual
// destructor makes "delete through a Device*" a compile error.
class Device {
 public:
  virtual u8 read(u16 address) = 0;
  virtual void write(u16 address, u8 value) = 0;

 protected:
  Device() = default;
  ~Device() = default;
};

class Bus {
 public:
  // All 256 pages start as plain RAM, zero-filled (deterministic power-on).
  Bus() noexcept;

  // The page table stores pointers into ram_, so copying or moving a Bus would
  // leave the copy pointing into the original. Forbid it.
  Bus(const Bus&) = delete;
  Bus& operator=(const Bus&) = delete;

  // ---- what the CPU uses (hot path, kept inline) ------------------------
  [[nodiscard]] u8 read(u16 address) {
    const Page& page = pages_[address >> 8];
    if (page.read != nullptr) return page.read[address & 0xFFu];
    return read_slow(address);
  }

  void write(u16 address, u8 value) {
    const Page& page = pages_[address >> 8];
    if (page.write != nullptr) {
      page.write[address & 0xFFu] = value;
      return;
    }
    write_slow(address, value);
  }

  // ---- what debuggers / disassemblers use --------------------------------
  // Side-effect free read. Device pages return 0xFF because reading a device
  // register may change its state, and a debugger must never do that.
  [[nodiscard]] u8 peek(u16 address) const noexcept;

  // Read-only view of all 64 KiB of backing store, ignoring the page mapping.
  // For tools: memory dumps, snapshots, comparing two machines.
  [[nodiscard]] std::span<const u8, kAddressSpace> backing_store() const noexcept { return ram_; }

  // ---- memory map configuration ------------------------------------------
  // Each returns false (and changes nothing) if the page range leaves 0..255.
  [[nodiscard]] bool map_ram(unsigned first_page, unsigned page_count) noexcept;
  [[nodiscard]] bool map_rom(unsigned first_page, unsigned page_count) noexcept;
  [[nodiscard]] bool map_device(unsigned first_page, unsigned page_count, Device& device) noexcept;
  [[nodiscard]] bool unmap(unsigned first_page, unsigned page_count) noexcept;

  // ---- loading programs ---------------------------------------------------
  // Copy bytes into the backing store, ignoring ROM protection (this is how a
  // ROM gets programmed). All-or-nothing: false if it would run past $FFFF.
  [[nodiscard]] bool load(u16 address, std::span<const u8> bytes) noexcept;

  // C64-style .prg image: a 2-byte little-endian load address, then the data.
  // Returns the load address, or nullopt if the image is malformed.
  [[nodiscard]] std::optional<u16> load_prg(std::span<const u8> prg) noexcept;

 private:
  struct Page {
    const u8* read = nullptr;  // non-null: plain read from memory
    u8* write = nullptr;       // non-null: plain write to memory (null for ROM)
    Device* device = nullptr;  // used only when the matching pointer is null
  };

  [[nodiscard]] u8 read_slow(u16 address);
  void write_slow(u16 address, u8 value);
  [[nodiscard]] bool range_ok(unsigned first_page, unsigned page_count) const noexcept;

  std::array<Page, kPageCount> pages_{};
  std::array<u8, kAddressSpace> ram_{};
};

}  // namespace mos6502

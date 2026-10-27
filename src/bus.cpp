#include "mos6502/bus.hpp"

#include <algorithm>
#include <cstddef>

namespace mos6502 {

Bus::Bus() noexcept {
  const bool ok = map_ram(0, kPageCount);
  (void)ok;  // cannot fail: the range is the whole address space
}

bool Bus::range_ok(unsigned first_page, unsigned page_count) const noexcept {
  return first_page < kPageCount && page_count <= kPageCount - first_page;
}

bool Bus::map_ram(unsigned first_page, unsigned page_count) noexcept {
  if (!range_ok(first_page, page_count)) return false;
  for (unsigned p = first_page; p < first_page + page_count; ++p) {
    u8* base = ram_.data() + static_cast<std::size_t>(p) * kPageSize;
    pages_[p] = Page{.read = base, .write = base, .device = nullptr};
  }
  return true;
}

bool Bus::map_rom(unsigned first_page, unsigned page_count) noexcept {
  if (!range_ok(first_page, page_count)) return false;
  for (unsigned p = first_page; p < first_page + page_count; ++p) {
    pages_[p] = Page{.read = ram_.data() + static_cast<std::size_t>(p) * kPageSize,
                     .write = nullptr,
                     .device = nullptr};
  }
  return true;
}

bool Bus::map_device(unsigned first_page, unsigned page_count, Device& device) noexcept {
  if (!range_ok(first_page, page_count)) return false;
  for (unsigned p = first_page; p < first_page + page_count; ++p) {
    pages_[p] = Page{.read = nullptr, .write = nullptr, .device = &device};
  }
  return true;
}

bool Bus::unmap(unsigned first_page, unsigned page_count) noexcept {
  if (!range_ok(first_page, page_count)) return false;
  for (unsigned p = first_page; p < first_page + page_count; ++p) {
    pages_[p] = Page{};
  }
  return true;
}

u8 Bus::read_slow(u16 address) {
  const Page& page = pages_[address >> 8];
  if (page.device != nullptr) return page.device->read(address);
  return 0xFF;  // unmapped: an undriven bus reads as all ones in this model
}

void Bus::write_slow(u16 address, u8 value) {
  const Page& page = pages_[address >> 8];
  if (page.device != nullptr) page.device->write(address, value);
  // ROM and unmapped pages silently ignore writes, like the real hardware.
}

u8 Bus::peek(u16 address) const noexcept {
  const Page& page = pages_[address >> 8];
  return page.read != nullptr ? page.read[address & 0xFFu] : u8{0xFF};
}

bool Bus::load(u16 address, std::span<const u8> bytes) noexcept {
  if (bytes.size() > kAddressSpace - address) return false;
  std::ranges::copy(bytes, ram_.begin() + address);
  return true;
}

std::optional<u16> Bus::load_prg(std::span<const u8> prg) noexcept {
  if (prg.size() < 2) return std::nullopt;
  const u16 address = make_word(prg[0], prg[1]);
  if (!load(address, prg.subspan(2))) return std::nullopt;
  return address;
}

}  // namespace mos6502

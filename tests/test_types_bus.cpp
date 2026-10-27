#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

#include "mos6502/bus.hpp"
#include "mos6502/types.hpp"

namespace mos6502 {
namespace {

// ---------------------------------------------------------------- helpers ---
TEST(Types, MakeWordIsLittleEndian) {
  EXPECT_EQ(make_word(0x34, 0x12), 0x1234);
  EXPECT_EQ(lo8(0x1234), 0x34);
  EXPECT_EQ(hi8(0x1234), 0x12);
  EXPECT_EQ(lo8(-1), 0xFF);  // two's complement narrowing is well defined
}

TEST(Types, CrossesPageComparesHighBytesOnly) {
  EXPECT_FALSE(crosses_page(0x12FF, 0x12FF));
  EXPECT_FALSE(crosses_page(0x1200, 0x12FF));
  EXPECT_TRUE(crosses_page(0x12FF, 0x1300));
  EXPECT_TRUE(crosses_page(0x0000, 0xFFFF));
}

TEST(Types, VectorsAndStackAreWhereTheHardwareExpectsThem) {
  EXPECT_EQ(kNmiVector, 0xFFFA);
  EXPECT_EQ(kResetVector, 0xFFFC);
  EXPECT_EQ(kIrqVector, 0xFFFE);
  EXPECT_EQ(kStackBase, 0x0100);
  EXPECT_EQ(kPageCount, 256u);
}

// -------------------------------------------------------------------- bus ---
struct Recorder final : Device {
  int reads = 0;
  int writes = 0;
  u16 last_address = 0;
  u8 last_value = 0;
  u8 read(u16 address) override {
    ++reads;
    last_address = address;
    return 0x5A;
  }
  void write(u16 address, u8 value) override {
    ++writes;
    last_address = address;
    last_value = value;
  }
};

TEST(Bus, StartsAsZeroFilledRam) {
  Bus bus;
  EXPECT_EQ(bus.read(0x0000), 0);
  EXPECT_EQ(bus.read(0xFFFF), 0);
  bus.write(0x1234, 0xAB);
  EXPECT_EQ(bus.read(0x1234), 0xAB);
}

TEST(Bus, RomIgnoresWritesButCanBeProgrammedWithLoad) {
  Bus bus;
  const std::array<u8, 3> image = {1, 2, 3};
  ASSERT_TRUE(bus.load(0xC000, image));
  ASSERT_TRUE(bus.map_rom(0xC0, 1));
  EXPECT_EQ(bus.read(0xC001), 2);
  bus.write(0xC001, 0x99);  // ignored: it is ROM
  EXPECT_EQ(bus.read(0xC001), 2);
  bus.write(0xBFFF, 0x77);  // the page below is still RAM
  EXPECT_EQ(bus.read(0xBFFF), 0x77);
}

TEST(Bus, DeviceSeesFullAddressAndIsCalledOnEveryAccess) {
  Bus bus;
  Recorder dev;
  ASSERT_TRUE(bus.map_device(0xD0, 2, dev));  // $D000-$D1FF
  EXPECT_EQ(bus.read(0xD123), 0x5A);
  EXPECT_EQ(dev.last_address, 0xD123);
  bus.write(0xD1FF, 0x42);
  EXPECT_EQ(dev.last_address, 0xD1FF);
  EXPECT_EQ(dev.last_value, 0x42);
  EXPECT_EQ(dev.reads, 1);
  EXPECT_EQ(dev.writes, 1);
  bus.write(0xD200, 0x11);  // outside the mapped range: plain RAM
  EXPECT_EQ(dev.writes, 1);
}

TEST(Bus, PeekNeverTouchesDevices) {
  Bus bus;
  Recorder dev;
  ASSERT_TRUE(bus.map_device(0xD0, 1, dev));
  EXPECT_EQ(bus.peek(0xD000), 0xFF);
  EXPECT_EQ(dev.reads, 0);
  bus.write(0x0010, 7);
  EXPECT_EQ(bus.peek(0x0010), 7);
}

TEST(Bus, UnmappedReadsAllOnesAndIgnoresWrites) {
  Bus bus;
  ASSERT_TRUE(bus.unmap(0x80, 1));
  bus.write(0x8000, 0x12);
  EXPECT_EQ(bus.read(0x8000), 0xFF);
}

TEST(Bus, RejectsPageRangesOutsideTheAddressSpace) {
  Bus bus;
  Recorder dev;
  EXPECT_FALSE(bus.map_ram(256, 1));
  EXPECT_FALSE(bus.map_rom(255, 2));
  EXPECT_FALSE(bus.map_device(250, 7, dev));
  EXPECT_FALSE(bus.unmap(0, 257));
  EXPECT_TRUE(bus.map_rom(255, 1));  // exactly the last page is fine
  EXPECT_TRUE(bus.map_ram(0, 0));    // an empty range is a harmless no-op
}

TEST(Bus, LoadIsAllOrNothingAndAcceptsExactFit) {
  Bus bus;
  const std::array<u8, 2> two = {0xAA, 0xBB};
  EXPECT_FALSE(bus.load(0xFFFF, two));
  EXPECT_EQ(bus.peek(0xFFFF), 0);  // nothing was written
  const std::array<u8, 1> one = {0xCC};
  EXPECT_TRUE(bus.load(0xFFFF, one));
  EXPECT_EQ(bus.peek(0xFFFF), 0xCC);
  EXPECT_TRUE(bus.load(0x1234, std::span<const u8>{}));  // empty is fine
}

TEST(Bus, LoadAcceptsASixteenKilobyteRomEndingExactlyAtTheTopOfMemory) {
  // The reference emulator's LoadPrg loops forever (and crashes) on this image,
  // because its 16-bit loop counter can never reach the end value 0x10000.
  Bus bus;
  const std::vector<u8> rom(0x4000, 0xEA);
  EXPECT_TRUE(bus.load(0xC000, rom));
  EXPECT_EQ(bus.peek(0xC000), 0xEA);
  EXPECT_EQ(bus.peek(0xFFFF), 0xEA);
}

TEST(Bus, LoadPrgReadsTheTwoByteHeader) {
  Bus bus;
  const std::array<u8, 5> prg = {0x01, 0x08, 0xA9, 0x42, 0x60};  // load at $0801
  const auto where = bus.load_prg(prg);
  ASSERT_TRUE(where.has_value());
  EXPECT_EQ(*where, 0x0801);
  EXPECT_EQ(bus.peek(0x0801), 0xA9);
  EXPECT_EQ(bus.peek(0x0803), 0x60);
}

TEST(Bus, LoadPrgRejectsMalformedImages) {
  Bus bus;
  const std::array<u8, 1> too_short = {0x01};
  EXPECT_FALSE(bus.load_prg(too_short).has_value());
  std::vector<u8> too_long = {0xFF, 0xFF, 1, 2};  // 2 bytes at $FFFF: overflows
  EXPECT_FALSE(bus.load_prg(too_long).has_value());
  const std::array<u8, 2> header_only = {0x00, 0x20};
  EXPECT_TRUE(bus.load_prg(header_only).has_value());  // valid, empty program
}

}  // namespace
}  // namespace mos6502

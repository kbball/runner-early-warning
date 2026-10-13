// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
#include <TriggerPacket.h>
#include <unity.h>

using ew::Packet;
using ew::PacketType;

void setUp() {}
void tearDown() {}

static Packet sample() { return Packet{ew::kVersion, PacketType::Trigger, 0xBEEF, 0x01020304u, 4012}; }

void test_round_trip() {
  uint8_t buf[ew::kManufDataSize];
  TEST_ASSERT_EQUAL(ew::kManufDataSize, ew::encode(sample(), buf, sizeof buf));
  Packet out{};
  TEST_ASSERT_TRUE(ew::decode(buf, sizeof buf, out));
  TEST_ASSERT_EQUAL_UINT8(ew::kVersion, out.version);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PacketType::Trigger), static_cast<uint8_t>(out.type));
  TEST_ASSERT_EQUAL_UINT16(0xBEEF, out.seq);
  TEST_ASSERT_EQUAL_UINT32(0x01020304u, out.uptimeMs);
  TEST_ASSERT_EQUAL_UINT16(4012, out.vbatMv);
}

void test_wire_layout_is_little_endian() {
  uint8_t buf[ew::kManufDataSize];
  ew::encode(sample(), buf, sizeof buf);
  const uint8_t want[ew::kManufDataSize] = {0xFF, 0xFF, 1, 2, 0xEF, 0xBE, 0x04, 0x03, 0x02, 0x01, 0xAC, 0x0F};
  TEST_ASSERT_EQUAL_UINT8_ARRAY(want, buf, sizeof want);
}

void test_encode_rejects_small_buffer() {
  uint8_t buf[ew::kManufDataSize - 1];
  TEST_ASSERT_EQUAL(0, ew::encode(sample(), buf, sizeof buf));
}

void test_decode_rejects_bad_input() {
  uint8_t buf[ew::kManufDataSize];
  ew::encode(sample(), buf, sizeof buf);
  Packet out{};

  TEST_ASSERT_FALSE(ew::decode(buf, sizeof buf - 1, out));  // short

  uint8_t bad[ew::kManufDataSize];
  for (size_t i = 0; i < sizeof bad; i++) bad[i] = buf[i];
  bad[0] = 0x34;  // wrong company id
  TEST_ASSERT_FALSE(ew::decode(bad, sizeof bad, out));

  for (size_t i = 0; i < sizeof bad; i++) bad[i] = buf[i];
  bad[2] = ew::kVersion + 1;  // unknown version
  TEST_ASSERT_FALSE(ew::decode(bad, sizeof bad, out));

  for (size_t i = 0; i < sizeof bad; i++) bad[i] = buf[i];
  bad[3] = 99;  // unknown type
  TEST_ASSERT_FALSE(ew::decode(bad, sizeof bad, out));
}

void test_heartbeat_type_survives() {
  Packet p = sample();
  p.type = PacketType::Heartbeat;
  uint8_t buf[ew::kManufDataSize];
  ew::encode(p, buf, sizeof buf);
  Packet out{};
  TEST_ASSERT_TRUE(ew::decode(buf, sizeof buf, out));
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(PacketType::Heartbeat), static_cast<uint8_t>(out.type));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_round_trip);
  RUN_TEST(test_wire_layout_is_little_endian);
  RUN_TEST(test_encode_rejects_small_buffer);
  RUN_TEST(test_decode_rejects_bad_input);
  RUN_TEST(test_heartbeat_type_survives);
  return UNITY_END();
}

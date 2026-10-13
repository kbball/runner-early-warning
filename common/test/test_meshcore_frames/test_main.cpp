// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
#include <MeshCoreFrames.h>
#include <unity.h>

using namespace ew;

void setUp() {}
void tearDown() {}

// The worked example in MeshCore docs/companion_protocol.md: "Hello" to channel 1 at timestamp 1234567890.
void test_channel_message_matches_the_documented_example() {
  uint8_t out[64];
  const size_t n = meshSendChannelText(1, 1234567890u, "Hello", out, sizeof out);
  const uint8_t payload[] = {0x03, 0x00, 0x01, 0xD2, 0x02, 0x96, 0x49, 0x48, 0x65, 0x6C, 0x6C, 0x6F};
  TEST_ASSERT_EQUAL(kMeshFrameHeader + sizeof payload, n);
  TEST_ASSERT_EQUAL_UINT8('<', out[0]);
  TEST_ASSERT_EQUAL_UINT8(sizeof payload, out[1]);  // length, low byte
  TEST_ASSERT_EQUAL_UINT8(0, out[2]);               // length, high byte
  TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, out + kMeshFrameHeader, sizeof payload);
}

void test_app_start_layout() {
  uint8_t out[64];
  const size_t n = meshAppStart("ew-camera", out, sizeof out);
  TEST_ASSERT_EQUAL(kMeshFrameHeader + 8 + 9, n);
  TEST_ASSERT_EQUAL_UINT8('<', out[0]);
  TEST_ASSERT_EQUAL_UINT8(8 + 9, out[1]);
  TEST_ASSERT_EQUAL_UINT8(0x01, out[3]);
  for (int i = 4; i < 3 + 8; i++) TEST_ASSERT_EQUAL_UINT8(0, out[i]);  // reserved bytes
  TEST_ASSERT_EQUAL_UINT8_ARRAY("ew-camera", out + 3 + 8, 9);
}

void test_long_app_name_is_truncated() {
  uint8_t out[128];
  const size_t n = meshAppStart("this-application-name-is-much-longer-than-thirty-two-bytes", out, sizeof out);
  TEST_ASSERT_EQUAL(kMeshFrameHeader + 8 + 32, n);
}

void test_rejects_empty_and_oversized_text() {
  uint8_t out[256];
  TEST_ASSERT_EQUAL(0, meshSendChannelText(0, 1, "", out, sizeof out));
  char big[kMeshMaxText + 2];
  memset(big, 'a', sizeof big);
  big[kMeshMaxText + 1] = '\0';
  TEST_ASSERT_EQUAL(0, meshSendChannelText(0, 1, big, out, sizeof out));
  big[kMeshMaxText] = '\0';  // exactly at the limit is fine
  TEST_ASSERT_EQUAL(kMeshFrameHeader + 7 + kMeshMaxText, meshSendChannelText(0, 1, big, out, sizeof out));
}

void test_rejects_small_output_buffer() {
  uint8_t out[8];
  TEST_ASSERT_EQUAL(0, meshSendChannelText(0, 1, "Hello", out, sizeof out));
  TEST_ASSERT_EQUAL(0, meshAppStart("ew-camera", out, sizeof out));
}

void test_frame_length_is_little_endian() {
  uint8_t payload[300];
  memset(payload, 7, sizeof payload);
  uint8_t out[400];
  const size_t n = meshFrame(payload, sizeof payload, out, sizeof out);
  TEST_ASSERT_EQUAL(kMeshFrameHeader + 300, n);
  TEST_ASSERT_EQUAL_UINT8(300 & 0xFF, out[1]);
  TEST_ASSERT_EQUAL_UINT8(300 >> 8, out[2]);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_channel_message_matches_the_documented_example);
  RUN_TEST(test_app_start_layout);
  RUN_TEST(test_long_app_name_is_truncated);
  RUN_TEST(test_rejects_empty_and_oversized_text);
  RUN_TEST(test_rejects_small_output_buffer);
  RUN_TEST(test_frame_length_is_little_endian);
  return UNITY_END();
}

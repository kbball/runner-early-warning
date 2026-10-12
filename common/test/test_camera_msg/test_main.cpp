#include <CameraMsg.h>
#include <unity.h>

using ew::CameraMsg;
using ew::CameraMsgType;
using ew::parseCameraLine;

void setUp() {}
void tearDown() {}

void test_boot() {
  CameraMsg m{};
  TEST_ASSERT_TRUE(parseCameraLine("EW,BOOT,812", m));
  TEST_ASSERT_EQUAL(static_cast<int>(CameraMsgType::Boot), static_cast<int>(m.type));
  TEST_ASSERT_EQUAL_UINT32(812, m.camMs);
}

void test_frame() {
  CameraMsg m{};
  TEST_ASSERT_TRUE(parseCameraLine("EW,FRAME,1450,3", m));
  TEST_ASSERT_EQUAL(static_cast<int>(CameraMsgType::Frame), static_cast<int>(m.type));
  TEST_ASSERT_EQUAL_UINT32(1450, m.camMs);
  TEST_ASSERT_EQUAL_UINT32(3, m.frame);
}

void test_verdict_person_and_none() {
  CameraMsg m{};
  TEST_ASSERT_TRUE(parseCameraLine("EW,VERDICT,2100,PERSON,87", m));
  TEST_ASSERT_EQUAL(static_cast<int>(CameraMsgType::Verdict), static_cast<int>(m.type));
  TEST_ASSERT_TRUE(m.person);
  TEST_ASSERT_EQUAL_UINT32(87, m.score);
  TEST_ASSERT_EQUAL_UINT32(2100, m.camMs);

  TEST_ASSERT_TRUE(parseCameraLine("EW,VERDICT,2200,NONE,4", m));
  TEST_ASSERT_FALSE(m.person);
  TEST_ASSERT_EQUAL_UINT32(4, m.score);
}

void test_done() {
  CameraMsg m{};
  TEST_ASSERT_TRUE(parseCameraLine("EW,DONE,31000", m));
  TEST_ASSERT_EQUAL(static_cast<int>(CameraMsgType::Done), static_cast<int>(m.type));
  TEST_ASSERT_EQUAL_UINT32(31000, m.camMs);
}

void test_rejects_noise_and_malformed() {
  CameraMsg m{};
  TEST_ASSERT_FALSE(parseCameraLine("", m));
  TEST_ASSERT_FALSE(parseCameraLine("ets Jun  8 2016 00:22:57", m));  // ROM boot noise
  TEST_ASSERT_FALSE(parseCameraLine("rst:0x1 (POWERON_RESET)", m));
  TEST_ASSERT_FALSE(parseCameraLine("EW", m));
  TEST_ASSERT_FALSE(parseCameraLine("EWX,BOOT,1", m));
  TEST_ASSERT_FALSE(parseCameraLine("EW,BOOT", m));
  TEST_ASSERT_FALSE(parseCameraLine("EW,BOOT,", m));
  TEST_ASSERT_FALSE(parseCameraLine("EW,BOOT,12x", m));
  TEST_ASSERT_FALSE(parseCameraLine("EW,BOOT,12,34", m));          // extra field
  TEST_ASSERT_FALSE(parseCameraLine("EW,BOOTS,12", m));
  TEST_ASSERT_FALSE(parseCameraLine("EW,FRAME,100", m));            // missing index
  TEST_ASSERT_FALSE(parseCameraLine("EW,VERDICT,100,MAYBE,50", m));
  TEST_ASSERT_FALSE(parseCameraLine("EW,VERDICT,100,PERSON,101", m));  // score out of range
  TEST_ASSERT_FALSE(parseCameraLine("EW,VERDICT,100,PERSON", m));
  TEST_ASSERT_FALSE(parseCameraLine("EW,BOOT,99999999999", m));    // overflows uint32
}

void test_failed_parse_leaves_output_alone() {
  CameraMsg m{CameraMsgType::Done, 7, 8, true, 9};
  TEST_ASSERT_FALSE(parseCameraLine("EW,FRAME,100", m));
  TEST_ASSERT_EQUAL_UINT32(7, m.camMs);
  TEST_ASSERT_EQUAL_UINT32(8, m.frame);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_boot);
  RUN_TEST(test_frame);
  RUN_TEST(test_verdict_person_and_none);
  RUN_TEST(test_done);
  RUN_TEST(test_rejects_noise_and_malformed);
  RUN_TEST(test_failed_parse_leaves_output_alone);
  return UNITY_END();
}

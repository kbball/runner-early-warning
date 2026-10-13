#include <CameraMsg.h>
#include <string.h>
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

void test_format_round_trips_through_the_parser() {
  const CameraMsg cases[] = {
      {CameraMsgType::Boot, 812, 0, false, 0},
      {CameraMsgType::Frame, 1450, 3, false, 0},
      {CameraMsgType::Verdict, 2100, 0, true, 87},
      {CameraMsgType::Verdict, 2200, 0, false, 4},
      {CameraMsgType::Done, 31000, 0, false, 0},
  };
  for (const CameraMsg& in : cases) {
    char line[64];
    const size_t n = ew::formatCameraLine(in, line, sizeof line);
    TEST_ASSERT_GREATER_THAN(0, n);
    CameraMsg out{};
    TEST_ASSERT_TRUE(parseCameraLine(line, out));
    TEST_ASSERT_EQUAL(static_cast<int>(in.type), static_cast<int>(out.type));
    TEST_ASSERT_EQUAL_UINT32(in.camMs, out.camMs);
    if (in.type == CameraMsgType::Frame) TEST_ASSERT_EQUAL_UINT32(in.frame, out.frame);
    if (in.type == CameraMsgType::Verdict) {
      TEST_ASSERT_EQUAL(in.person, out.person);
      TEST_ASSERT_EQUAL_UINT32(in.score, out.score);
    }
  }
}

void test_format_text_is_exact_and_rejects_small_buffer() {
  char line[64];
  const CameraMsg v{CameraMsgType::Verdict, 2100, 0, true, 87};
  TEST_ASSERT_EQUAL(strlen("EW,VERDICT,2100,PERSON,87"), ew::formatCameraLine(v, line, sizeof line));
  TEST_ASSERT_EQUAL_STRING("EW,VERDICT,2100,PERSON,87", line);
  char tiny[8];
  TEST_ASSERT_EQUAL(0, ew::formatCameraLine(v, tiny, sizeof tiny));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_format_round_trips_through_the_parser);
  RUN_TEST(test_format_text_is_exact_and_rejects_small_buffer);
  RUN_TEST(test_boot);
  RUN_TEST(test_frame);
  RUN_TEST(test_verdict_person_and_none);
  RUN_TEST(test_done);
  RUN_TEST(test_rejects_noise_and_malformed);
  RUN_TEST(test_failed_parse_leaves_output_alone);
  return UNITY_END();
}

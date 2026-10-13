// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
#include <TriggerLogic.h>
#include <unity.h>

using namespace ew;

void setUp() {}
void tearDown() {}

// ---- GroupTracker ---------------------------------------------------------------------------

void test_first_packet_opens_a_group() {
  GroupTracker t;
  RxResult r = t.onPacket(5, -60, 1000);
  TEST_ASSERT_TRUE(r.newGroup);
  TEST_ASSERT_FALSE(r.prevClosed);
  TEST_ASSERT_EQUAL_UINT16(0, r.gap);  // nothing to compare with yet
  TEST_ASSERT_FALSE(r.senderReset);
}

void test_repeats_in_a_group_are_counted_not_new() {
  GroupTracker t;
  t.onPacket(5, -60, 1000);
  RxResult r = t.onPacket(5, -50, 1020);
  TEST_ASSERT_FALSE(r.newGroup);
  t.onPacket(5, -70, 1040);
  RxResult next = t.onPacket(6, -55, 5000);
  TEST_ASSERT_TRUE(next.newGroup);
  TEST_ASSERT_TRUE(next.prevClosed);
  TEST_ASSERT_EQUAL_UINT16(5, next.prev.seq);
  TEST_ASSERT_EQUAL_UINT16(3, next.prev.count);
  TEST_ASSERT_EQUAL_INT8(-50, next.prev.bestRssi);
  TEST_ASSERT_EQUAL_UINT16(0, next.gap);
}

void test_gap_counts_missed_groups() {
  GroupTracker t;
  t.onPacket(10, -60, 0);
  RxResult r = t.onPacket(13, -60, 100);  // 11 and 12 never heard
  TEST_ASSERT_EQUAL_UINT16(2, r.gap);
  TEST_ASSERT_FALSE(r.senderReset);
}

void test_gap_survives_sequence_wrap() {
  GroupTracker t;
  t.onPacket(65535, -60, 0);
  RxResult r = t.onPacket(1, -60, 100);  // 0 missed
  TEST_ASSERT_EQUAL_UINT16(1, r.gap);
  TEST_ASSERT_FALSE(r.senderReset);
}

void test_backwards_or_far_jump_is_a_sender_reset() {
  GroupTracker t;
  t.onPacket(500, -60, 0);
  RxResult back = t.onPacket(2, -60, 100);
  TEST_ASSERT_TRUE(back.senderReset);
  TEST_ASSERT_EQUAL_UINT16(0, back.gap);

  GroupTracker u;
  u.onPacket(10, -60, 0);
  RxResult far = u.onPacket(10 + 5000, -60, 100);
  TEST_ASSERT_TRUE(far.senderReset);
}

void test_idle_flush_closes_the_group_once() {
  GroupTracker t;
  t.onPacket(7, -60, 1000);
  t.onPacket(7, -40, 1010);
  GroupSummary s{};
  TEST_ASSERT_FALSE(t.flushIfIdle(1500, 1000, s));  // heard recently
  TEST_ASSERT_TRUE(t.flushIfIdle(2010, 1000, s));
  TEST_ASSERT_EQUAL_UINT16(7, s.seq);
  TEST_ASSERT_EQUAL_UINT16(2, s.count);
  TEST_ASSERT_EQUAL_INT8(-40, s.bestRssi);
  TEST_ASSERT_FALSE(t.flushIfIdle(9000, 1000, s));  // already closed
}

void test_late_duplicate_after_flush_is_ignored() {
  GroupTracker t;
  t.onPacket(7, -60, 1000);
  GroupSummary s{};
  TEST_ASSERT_TRUE(t.flushIfIdle(2500, 1000, s));
  RxResult late = t.onPacket(7, -60, 2600);  // would otherwise look like a fresh trigger
  TEST_ASSERT_FALSE(late.newGroup);
  RxResult next = t.onPacket(8, -60, 6000);
  TEST_ASSERT_TRUE(next.newGroup);
  TEST_ASSERT_EQUAL_UINT16(0, next.gap);
}

void test_group_after_flush_still_reports_gap() {
  GroupTracker t;
  t.onPacket(7, -60, 1000);
  GroupSummary s{};
  t.flushIfIdle(2500, 1000, s);
  RxResult r = t.onPacket(9, -60, 6000);
  TEST_ASSERT_TRUE(r.newGroup);
  TEST_ASSERT_FALSE(r.prevClosed);  // already closed by the flush
  TEST_ASSERT_EQUAL_UINT16(1, r.gap);
}

void test_idle_flush_survives_millis_wrap() {
  GroupTracker t;
  t.onPacket(1, -60, 0xFFFFFF00u);
  GroupSummary s{};
  TEST_ASSERT_FALSE(t.flushIfIdle(0xFFFFFF10u, 1000, s));
  TEST_ASSERT_TRUE(t.flushIfIdle(0x00000500u, 1000, s));  // clock wrapped, 1.3 s later
}

// ---- EnableWindow ---------------------------------------------------------------------------

void test_trigger_raises_then_times_out() {
  EnableWindow w(45000, 90000, 500, true);
  TEST_ASSERT_FALSE(w.enabled());
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::Raised), static_cast<int>(w.onTrigger(1000)));
  TEST_ASSERT_TRUE(w.enabled());
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(45999)));
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::DroppedTimeout), static_cast<int>(w.tick(46000)));
  TEST_ASSERT_FALSE(w.enabled());
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(70000)));
}

void test_second_trigger_extends_but_is_capped() {
  EnableWindow w(45000, 60000, 500, true);
  w.onTrigger(0);
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::Extended), static_cast<int>(w.onTrigger(30000)));  // deadline 75000, capped to 60000
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(59999)));
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::DroppedTimeout), static_cast<int>(w.tick(60000)));
}

void test_trigger_that_adds_nothing_is_not_an_extension() {
  EnableWindow w(45000, 60000, 500, true);
  w.onTrigger(0);
  w.onTrigger(30000);                         // deadline now at the 60000 cap
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.onTrigger(40000)));
}

void test_done_cuts_after_the_grace_period() {
  EnableWindow w(45000, 90000, 500, true);
  w.onTrigger(0);
  w.onDone(10000);
  TEST_ASSERT_TRUE(w.enabled());
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(10499)));
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::DroppedDone), static_cast<int>(w.tick(10500)));
  TEST_ASSERT_FALSE(w.enabled());
}

void test_done_is_ignored_when_cut_on_done_is_off() {
  EnableWindow w(45000, 90000, 500, false);
  w.onTrigger(0);
  w.onDone(10000);
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(11000)));
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::DroppedTimeout), static_cast<int>(w.tick(45000)));
}

void test_new_trigger_cancels_a_pending_cut() {
  EnableWindow w(45000, 90000, 500, true);
  w.onTrigger(0);
  w.onDone(10000);
  w.onTrigger(10100);  // another runner arrives before the cut
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(10600)));
  TEST_ASSERT_TRUE(w.enabled());
}

void test_done_while_off_does_nothing() {
  EnableWindow w(45000, 90000, 500, true);
  w.onDone(100);
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(1000)));
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::Raised), static_cast<int>(w.onTrigger(2000)));
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(2600)));  // stale done must not cut
}

void test_window_works_across_millis_wrap() {
  EnableWindow w(45000, 90000, 500, true);
  const uint32_t start = 0xFFFFF000u;
  w.onTrigger(start);
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::None), static_cast<int>(w.tick(start + 44000)));
  TEST_ASSERT_EQUAL(static_cast<int>(EnEvent::DroppedTimeout), static_cast<int>(w.tick(start + 45000)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_packet_opens_a_group);
  RUN_TEST(test_repeats_in_a_group_are_counted_not_new);
  RUN_TEST(test_gap_counts_missed_groups);
  RUN_TEST(test_gap_survives_sequence_wrap);
  RUN_TEST(test_backwards_or_far_jump_is_a_sender_reset);
  RUN_TEST(test_idle_flush_closes_the_group_once);
  RUN_TEST(test_late_duplicate_after_flush_is_ignored);
  RUN_TEST(test_group_after_flush_still_reports_gap);
  RUN_TEST(test_idle_flush_survives_millis_wrap);
  RUN_TEST(test_trigger_raises_then_times_out);
  RUN_TEST(test_second_trigger_extends_but_is_capped);
  RUN_TEST(test_trigger_that_adds_nothing_is_not_an_extension);
  RUN_TEST(test_done_cuts_after_the_grace_period);
  RUN_TEST(test_done_is_ignored_when_cut_on_done_is_off);
  RUN_TEST(test_new_trigger_cancels_a_pending_cut);
  RUN_TEST(test_done_while_off_does_nothing);
  RUN_TEST(test_window_works_across_millis_wrap);
  return UNITY_END();
}

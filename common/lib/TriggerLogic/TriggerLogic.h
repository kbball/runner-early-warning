// Pure logic for the trigger board, kept free of Arduino and Nordic headers so it is unit tested on the host.
//
//   GroupTracker  - collapses a burst of identical-seq packets into one group, counts the packets heard in it,
//                   and counts groups missed from gaps in the sequence numbers.
//   EnableWindow  - decides when the camera's boost enable is high: raised on a trigger, extended by further
//                   triggers up to a cap, dropped on timeout or shortly after the camera reports it is done.
//
// All times are millis() values; comparisons use signed differences so they survive the 49-day wrap.
#pragma once

#include <stdint.h>

namespace ew {

inline bool timeReached(uint32_t now, uint32_t target) { return static_cast<int32_t>(now - target) >= 0; }

// ----------------------------------------------------------------------------------------------
struct GroupSummary {
  uint16_t seq;
  uint16_t count;      // packets heard in the group
  int8_t bestRssi;     // strongest RSSI in the group
};

struct RxResult {
  bool newGroup;         // first packet of a new group (log it, act on it)
  uint16_t gap;          // groups missed since the previous group (0 when none or unknown)
  bool senderReset;      // the sequence jumped backwards or far: the sender probably rebooted
  bool prevClosed;       // a previous group was open and is now complete
  GroupSummary prev;     // valid when prevClosed
};

class GroupTracker {
 public:
  // A forward jump larger than this is treated as a sender reboot, not as that many missed groups.
  static constexpr uint16_t kResetThreshold = 1000;

  RxResult onPacket(uint16_t seq, int8_t rssi, uint32_t nowMs) {
    RxResult r{false, 0, false, false, {0, 0, 0}};
    lastRxMs_ = nowMs;
    if (open_ && seq == cur_.seq) {
      cur_.count++;
      if (rssi > cur_.bestRssi) cur_.bestRssi = rssi;
      return r;
    }
    if (!open_ && haveLast_ && seq == lastSeq_) return r;  // late duplicate of a group already closed: ignore it
    r.newGroup = true;
    if (open_) {
      r.prevClosed = true;
      r.prev = cur_;
    }
    if (haveLast_) {
      const uint16_t delta = static_cast<uint16_t>(seq - lastSeq_);  // 1 means consecutive
      if (delta == 0 || delta > kResetThreshold) r.senderReset = true;
      else r.gap = static_cast<uint16_t>(delta - 1);
    }
    cur_ = GroupSummary{seq, 1, rssi};
    open_ = true;
    lastSeq_ = seq;
    haveLast_ = true;
    return r;
  }

  // Closes the open group once nothing has been heard for idleMs. True and fills out when it closed one.
  bool flushIfIdle(uint32_t nowMs, uint32_t idleMs, GroupSummary& out) {
    if (!open_ || !timeReached(nowMs, lastRxMs_ + idleMs)) return false;
    out = cur_;
    open_ = false;
    return true;
  }

  bool open() const { return open_; }

 private:
  bool open_ = false;
  bool haveLast_ = false;
  GroupSummary cur_{0, 0, 0};
  uint16_t lastSeq_ = 0;
  uint32_t lastRxMs_ = 0;
};

// ----------------------------------------------------------------------------------------------
enum class EnEvent : uint8_t { None, Raised, Extended, DroppedTimeout, DroppedDone };

class EnableWindow {
 public:
  EnableWindow(uint32_t windowMs, uint32_t maxMs, uint32_t doneGraceMs, bool cutOnDone)
      : windowMs_(windowMs), maxMs_(maxMs), graceMs_(doneGraceMs), cutOnDone_(cutOnDone) {}

  bool enabled() const { return on_; }

  EnEvent onTrigger(uint32_t now) {
    cutPending_ = false;  // a new trigger cancels a pending early cut
    if (!on_) {
      on_ = true;
      startMs_ = now;
      deadlineMs_ = now + windowMs_;
      return EnEvent::Raised;
    }
    uint32_t wanted = now + windowMs_;
    const uint32_t cap = startMs_ + maxMs_;
    if (static_cast<int32_t>(wanted - cap) > 0) wanted = cap;  // never run past the cap
    if (static_cast<int32_t>(wanted - deadlineMs_) > 0) {
      deadlineMs_ = wanted;
      return EnEvent::Extended;
    }
    return EnEvent::None;
  }

  // The camera said it is finished. With cutOnDone the enable drops after the grace period.
  void onDone(uint32_t now) {
    if (on_ && cutOnDone_) {
      cutPending_ = true;
      cutAtMs_ = now + graceMs_;
    }
  }

  // Call regularly. Returns the event when the enable drops.
  EnEvent tick(uint32_t now) {
    if (!on_) return EnEvent::None;
    if (cutPending_ && timeReached(now, cutAtMs_)) {
      on_ = false;
      cutPending_ = false;
      return EnEvent::DroppedDone;
    }
    if (timeReached(now, deadlineMs_)) {
      on_ = false;
      cutPending_ = false;
      return EnEvent::DroppedTimeout;
    }
    return EnEvent::None;
  }

 private:
  uint32_t windowMs_, maxMs_, graceMs_;
  bool cutOnDone_;
  bool on_ = false;
  bool cutPending_ = false;
  uint32_t startMs_ = 0, deadlineMs_ = 0, cutAtMs_ = 0;
};

}  // namespace ew

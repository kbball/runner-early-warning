// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Field-tunable settings and the log of recent sessions, both kept in the board's flash (NVS) so they survive
// power loss and can be changed from the setup page without reflashing. Compile-time values are the defaults.
#pragma once

#include <stdint.h>

namespace ew {

struct Settings {
  uint8_t threshold;      // detector score 0..100 that counts as a person
  uint8_t confirmFrames;  // consecutive frames at or above the threshold before PERSON
  uint16_t sessionSec;    // how long the node grabs frames after boot
  uint8_t showMin;        // setup page: lowest score drawn (not used by the node)
};

Settings defaultSettings();
Settings loadSettings();                // saved values, or the defaults
bool saveSettings(const Settings& s);   // clamps to sane ranges first; returns false if flash write fails
bool clampSettings(Settings* s);        // true if anything had to change

// One real node session, as the trigger board's window saw it.
struct SessionRecord {
  uint32_t seq;           // 1, 2, 3 ... since the log was last cleared
  uint32_t firstFrameMs;  // boot to first frame
  uint16_t frames;
  uint8_t best;           // best score 0..100
  uint8_t person;         // 1 if the verdict was PERSON
};

constexpr int kSessionLog = 16;
void appendSession(const SessionRecord& r);
int readSessions(SessionRecord* out, int max);  // newest first; returns how many
void clearSessions();

}  // namespace ew

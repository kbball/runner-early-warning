// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
#include "Settings.h"

#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>

// Compile-time defaults (override with -D in platformio.ini).
#ifndef PERSON_THRESHOLD
#define PERSON_THRESHOLD 60
#endif
#ifndef PERSON_CONFIRM_FRAMES
#define PERSON_CONFIRM_FRAMES 2
#endif
#ifndef SESSION_MS
#define SESSION_MS 30000
#endif

namespace ew {

namespace {

constexpr char kNs[] = "ew";
constexpr char kSettingsKey[] = "settings";
constexpr char kLogKey[] = "sessions";

struct LogBlob {
  uint32_t next;  // sequence number of the next record
  SessionRecord recs[kSessionLog];
};

bool ensureNvs() {
  static bool done = false;
  if (done) return true;
  esp_err_t e = nvs_flash_init();
  if (e == ESP_ERR_NVS_NO_FREE_PAGES || e == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    e = nvs_flash_init();
  }
  done = (e == ESP_OK);
  return done;
}

bool readBlob(const char* key, void* out, size_t size) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = size;
  const bool ok = nvs_get_blob(h, key, out, &len) == ESP_OK && len == size;
  nvs_close(h);
  return ok;
}

bool writeBlob(const char* key, const void* data, size_t size) {
  nvs_handle_t h;
  if (nvs_open(kNs, NVS_READWRITE, &h) != ESP_OK) return false;
  const bool ok = nvs_set_blob(h, key, data, size) == ESP_OK && nvs_commit(h) == ESP_OK;
  nvs_close(h);
  return ok;
}

}  // namespace

Settings defaultSettings() {
  return {PERSON_THRESHOLD, PERSON_CONFIRM_FRAMES, static_cast<uint16_t>(SESSION_MS / 1000), 20};
}

bool clampSettings(Settings* s) {
  const Settings before = *s;
  if (s->threshold < 1) s->threshold = 1;
  if (s->threshold > 100) s->threshold = 100;
  if (s->confirmFrames < 1) s->confirmFrames = 1;
  if (s->confirmFrames > 20) s->confirmFrames = 20;
  if (s->sessionSec < 5) s->sessionSec = 5;
  if (s->sessionSec > 300) s->sessionSec = 300;
  if (s->showMin > 100) s->showMin = 100;
  return memcmp(&before, s, sizeof before) != 0;
}

Settings loadSettings() {
  Settings s = defaultSettings();
  if (ensureNvs() && readBlob(kSettingsKey, &s, sizeof s)) {
    clampSettings(&s);
    return s;
  }
  return defaultSettings();
}

bool saveSettings(const Settings& in) {
  Settings s = in;
  clampSettings(&s);
  return ensureNvs() && writeBlob(kSettingsKey, &s, sizeof s);
}

void appendSession(const SessionRecord& in) {
  if (!ensureNvs()) return;
  LogBlob b = {};
  if (!readBlob(kLogKey, &b, sizeof b)) b = {};
  SessionRecord r = in;
  r.seq = b.next + 1;
  b.recs[b.next % kSessionLog] = r;
  b.next = r.seq;
  writeBlob(kLogKey, &b, sizeof b);
}

int readSessions(SessionRecord* out, int max) {
  LogBlob b = {};
  if (!ensureNvs() || !readBlob(kLogKey, &b, sizeof b)) return 0;
  int n = 0;
  for (uint32_t seq = b.next; seq > 0 && n < max && n < kSessionLog; seq--) {
    out[n++] = b.recs[(seq - 1) % kSessionLog];
  }
  return n;
}

void clearSessions() {
  if (!ensureNvs()) return;
  LogBlob b = {};
  writeBlob(kLogKey, &b, sizeof b);
}

}  // namespace ew

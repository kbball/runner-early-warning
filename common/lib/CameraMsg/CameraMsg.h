// Lines the camera (ESP32-S3) prints on its UART for the trigger board to read.
//
// The ESP32-S3 ROM also prints boot noise on the same UART at power-up, so every real message
// starts with a fixed prefix and anything else is ignored.
//
//   EW,BOOT,<ms>                      app is running; <ms> is the camera's millis()
//   EW,FRAME,<ms>,<n>                 a frame was captured (n counts from 1)
//   EW,VERDICT,<ms>,PERSON|NONE,<s>   a detection result, s = score 0..100
//   EW,DONE,<ms>                      detection window over, the camera can be powered off
//
// Host-testable: no Arduino headers.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ew {

enum class CameraMsgType : uint8_t { Boot, Frame, Verdict, Done };

struct CameraMsg {
  CameraMsgType type;
  uint32_t camMs;   // camera millis()
  uint32_t frame;   // Frame: index
  bool person;      // Verdict: true for PERSON
  uint32_t score;   // Verdict: 0..100
};

namespace detail {

// Reads a decimal number from s up to the next ',' or end. Advances s past the comma. False if empty or not digits.
inline bool takeNumber(const char*& s, uint32_t& out) {
  if (*s < '0' || *s > '9') return false;
  uint64_t v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + static_cast<uint64_t>(*s - '0');
    if (v > 0xFFFFFFFFull) return false;
    s++;
  }
  if (*s == ',') s++;
  else if (*s != '\0') return false;
  out = static_cast<uint32_t>(v);
  return true;
}

// True if s starts with word followed by ',' or end; advances s past word and the comma.
inline bool takeWord(const char*& s, const char* word) {
  const char* p = s;
  while (*word) {
    if (*p != *word) return false;
    p++;
    word++;
  }
  if (*p == ',') p++;
  else if (*p != '\0') return false;
  s = p;
  return true;
}

}  // namespace detail

// Parses one line (no trailing newline). False for noise or a malformed message.
inline bool parseCameraLine(const char* line, CameraMsg& out) {
  const char* s = line;
  if (!detail::takeWord(s, "EW")) return false;
  CameraMsg m{CameraMsgType::Boot, 0, 0, false, 0};
  if (detail::takeWord(s, "BOOT")) {
    m.type = CameraMsgType::Boot;
    if (!detail::takeNumber(s, m.camMs) || *s != '\0') return false;
  } else if (detail::takeWord(s, "FRAME")) {
    m.type = CameraMsgType::Frame;
    if (!detail::takeNumber(s, m.camMs) || !detail::takeNumber(s, m.frame) || *s != '\0') return false;
  } else if (detail::takeWord(s, "VERDICT")) {
    m.type = CameraMsgType::Verdict;
    if (!detail::takeNumber(s, m.camMs)) return false;
    if (detail::takeWord(s, "PERSON")) m.person = true;
    else if (detail::takeWord(s, "NONE")) m.person = false;
    else return false;
    if (!detail::takeNumber(s, m.score) || *s != '\0' || m.score > 100) return false;
  } else if (detail::takeWord(s, "DONE")) {
    m.type = CameraMsgType::Done;
    if (!detail::takeNumber(s, m.camMs) || *s != '\0') return false;
  } else {
    return false;
  }
  out = m;
  return true;
}

}  // namespace ew

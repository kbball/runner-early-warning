// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Builds the MeshCore companion-protocol frames the camera sends to the MeshCore nRF over UART, so a
// detected person becomes a channel message. Host-testable: no Arduino headers.
//
// Wire format (docs/companion_protocol.md and ArduinoSerialInterface.cpp in the MeshCore repo):
//   frame   = '<' + payload length (2 bytes, little-endian) + payload
//   App Start            payload = 01 00 00 00 00 00 00 00 + app name (UTF-8)
//   Send Channel Message payload = 03 00 <channel index> <timestamp, 4 bytes LE> <text, UTF-8>
//
// The MeshCore firmware must be built with its hardware-serial interface enabled (see the two-node notes,
// section 12). Replies from the device (frames starting '>') are ignored by the camera.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace ew {

constexpr size_t kMeshFrameHeader = 3;    // '<' + 2 length bytes
constexpr size_t kMeshMaxText = 128;      // conservative cap on message text; MeshCore's own limit is larger

// Wraps a payload in a '<' frame. Returns the frame length, or 0 if it does not fit.
inline size_t meshFrame(const uint8_t* payload, size_t len, uint8_t* out, size_t cap) {
  if (len > 0xFFFF || cap < kMeshFrameHeader + len) return 0;
  out[0] = '<';
  out[1] = static_cast<uint8_t>(len & 0xFF);
  out[2] = static_cast<uint8_t>(len >> 8);
  memcpy(out + kMeshFrameHeader, payload, len);
  return kMeshFrameHeader + len;
}

// App Start frame. Must be sent first after the link comes up.
inline size_t meshAppStart(const char* appName, uint8_t* out, size_t cap) {
  uint8_t payload[8 + 32];
  const size_t nameLen = strnlen(appName, 32);
  payload[0] = 0x01;
  memset(payload + 1, 0, 7);
  memcpy(payload + 8, appName, nameLen);
  return meshFrame(payload, 8 + nameLen, out, cap);
}

// Send Channel Message frame. Text longer than kMeshMaxText is rejected (returns 0).
inline size_t meshSendChannelText(uint8_t channelIdx, uint32_t timestamp, const char* text, uint8_t* out,
                                  size_t cap) {
  const size_t textLen = strlen(text);
  if (textLen == 0 || textLen > kMeshMaxText) return 0;
  uint8_t payload[7 + kMeshMaxText];
  payload[0] = 0x03;
  payload[1] = 0x00;
  payload[2] = channelIdx;
  for (int i = 0; i < 4; i++) payload[3 + i] = static_cast<uint8_t>(timestamp >> (8 * i));
  memcpy(payload + 7, text, textLen);
  return meshFrame(payload, 7 + textLen, out, cap);
}

}  // namespace ew

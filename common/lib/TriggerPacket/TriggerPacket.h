// Trigger packet shared by the PIR node (sender) and the camera node's trigger board (receiver).
//
// Carried as BLE manufacturer-specific advertising data. No Arduino or Nordic headers here, so it
// builds and is unit tested on the host (see ../../test).
//
// Layout of the manufacturer data (little-endian, 12 bytes):
//   0..1   company id (kCompanyId)
//   2      version (kVersion)
//   3      type    (PacketType)
//   4..5   seq     per-sender counter, one number per packet group (a burst repeats the same seq)
//   6..9   uptime  sender millis() when the group was built
//   10..11 vbat    battery millivolts, 0 if unknown
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ew {

constexpr uint16_t kCompanyId = 0xFFFF;  // 0xFFFF is reserved for testing; fine for the rig
constexpr uint8_t kVersion = 1;
constexpr size_t kManufDataSize = 12;

enum class PacketType : uint8_t {
  Heartbeat = 1,  // periodic beacon, no motion; measures the link and shows the node is alive
  Trigger = 2,    // PIR fired
};

struct Packet {
  uint8_t version;
  PacketType type;
  uint16_t seq;
  uint32_t uptimeMs;
  uint16_t vbatMv;
};

// Writes the manufacturer data into out. Returns the number of bytes written, or 0 if cap is too small.
inline size_t encode(const Packet& p, uint8_t* out, size_t cap) {
  if (cap < kManufDataSize) return 0;
  out[0] = static_cast<uint8_t>(kCompanyId & 0xFF);
  out[1] = static_cast<uint8_t>(kCompanyId >> 8);
  out[2] = p.version;
  out[3] = static_cast<uint8_t>(p.type);
  out[4] = static_cast<uint8_t>(p.seq & 0xFF);
  out[5] = static_cast<uint8_t>(p.seq >> 8);
  for (int i = 0; i < 4; i++) out[6 + i] = static_cast<uint8_t>(p.uptimeMs >> (8 * i));
  out[10] = static_cast<uint8_t>(p.vbatMv & 0xFF);
  out[11] = static_cast<uint8_t>(p.vbatMv >> 8);
  return kManufDataSize;
}

// Parses manufacturer data. Returns false for a wrong size, company id, version or type.
inline bool decode(const uint8_t* data, size_t len, Packet& out) {
  if (len != kManufDataSize) return false;
  const uint16_t company = static_cast<uint16_t>(data[0] | (data[1] << 8));
  if (company != kCompanyId || data[2] != kVersion) return false;
  if (data[3] != static_cast<uint8_t>(PacketType::Heartbeat) &&
      data[3] != static_cast<uint8_t>(PacketType::Trigger)) {
    return false;
  }
  out.version = data[2];
  out.type = static_cast<PacketType>(data[3]);
  out.seq = static_cast<uint16_t>(data[4] | (data[5] << 8));
  out.uptimeMs = 0;
  for (int i = 0; i < 4; i++) out.uptimeMs |= static_cast<uint32_t>(data[6 + i]) << (8 * i);
  out.vbatMv = static_cast<uint16_t>(data[10] | (data[11] << 8));
  return true;
}

}  // namespace ew

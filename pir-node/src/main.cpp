// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// PIR node (XIAO nRF52840).
//
// Watches a PIR on D0. On motion it advertises a short burst of Trigger packets; every
// HEARTBEAT_MS it advertises a short burst of Heartbeat packets, so the link can be measured
// without anyone walking past. Packets are BLE manufacturer data (see TriggerPacket.h). The camera
// node's trigger board scans for them.
//
// Between events the CPU sleeps in the RTOS idle task, waking on the PIR edge or the heartbeat timer.

#include <Arduino.h>
#include <TriggerPacket.h>
#include <bluefruit.h>
#include <semphr.h>

// ---- configuration (override with -D in platformio.ini) --------------------------------------
#ifndef PIR_PIN
#define PIR_PIN D0
#endif
#ifndef HEARTBEAT_MS
#define HEARTBEAT_MS 5000  // time between heartbeat groups
#endif
#ifndef TRIGGER_BURST_MS
#define TRIGGER_BURST_MS 1500  // how long a trigger group is advertised
#endif
#ifndef HEARTBEAT_BURST_MS
#define HEARTBEAT_BURST_MS 250  // how long a heartbeat group is advertised
#endif
#ifndef PIR_WARMUP_MS
#define PIR_WARMUP_MS 10000  // ignore the PIR this long after boot (AM312 settling; unverified)
#endif
#ifndef PIR_COOLDOWN_MS
#define PIR_COOLDOWN_MS 1000  // ignore new edges for this long after a trigger group ends
#endif
#ifndef ADV_INTERVAL_MS
#define ADV_INTERVAL_MS 20  // advertising interval inside a burst
#endif
#ifndef TX_POWER_DBM
#define TX_POWER_DBM 4  // -40, -20, -16, -12, -8, -4, 0, 2, 3, 4, 5, 6, 7, 8
#endif
#ifndef LOG_SERIAL
#define LOG_SERIAL 1
#endif
#ifndef LED_ON_TRIGGER
#define LED_ON_TRIGGER 1
#endif
#ifndef VBAT_DIVIDER_X1000
#define VBAT_DIVIDER_X1000 2953  // (1M + 512k) / 512k, from the XIAO battery divider; calibrate with a meter
#endif

#if LOG_SERIAL
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...) \
  do {           \
  } while (0)
#endif

// ---- state -----------------------------------------------------------------------------------
static SemaphoreHandle_t g_pirSem;
static uint16_t g_seq = 0;
static uint32_t g_nextHeartbeatMs = 0;
static uint32_t g_lastTriggerEndMs = 0;
static bool g_haveTrigger = false;

static void pirIsr() {
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(g_pirSem, &woken);
  portYIELD_FROM_ISR(woken);
}

// Battery voltage in millivolts, or 0 if this board has no battery sense pin.
static uint16_t readVbatMv() {
#if defined(PIN_VBAT) && defined(VBAT_ENABLE)
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW);  // connect the divider
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  delay(2);
  const uint32_t raw = analogRead(PIN_VBAT);
  digitalWrite(VBAT_ENABLE, HIGH);  // disconnect the divider again (it draws current)
  return static_cast<uint16_t>(raw * 3000UL * VBAT_DIVIDER_X1000 / (4095UL * 1000UL));
#else
  return 0;
#endif
}

static void pulseLed() {
#if LED_ON_TRIGGER
  digitalWrite(LED_RED, LOW);  // active low
  delay(20);
  digitalWrite(LED_RED, HIGH);
#endif
}

// Advertise one packet group for burstMs. Every packet in the group carries the same sequence number.
static void sendGroup(ew::PacketType type, uint32_t burstMs) {
  const uint32_t now = millis();
  const ew::Packet pkt{ew::kVersion, type, g_seq++, now, readVbatMv()};
  uint8_t data[ew::kManufDataSize];
  ew::encode(pkt, data, sizeof data);

  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addManufacturerData(data, sizeof data);
  Bluefruit.Advertising.start(0);  // 0 = no timeout; stopped below
  LOG("%lu,%s,%u,%u\n", static_cast<unsigned long>(now), type == ew::PacketType::Trigger ? "TRIGGER" : "HEARTBEAT",
      static_cast<unsigned>(pkt.seq), static_cast<unsigned>(pkt.vbatMv));
  if (type == ew::PacketType::Trigger) pulseLed();
  delay(burstMs);
  Bluefruit.Advertising.stop();
}

static void handlePirEdge() {
  const uint32_t now = millis();
  if (now < PIR_WARMUP_MS) {
    LOG("%lu,IGNORED_WARMUP,,\n", static_cast<unsigned long>(now));
    return;
  }
  if (g_haveTrigger && static_cast<int32_t>(now - g_lastTriggerEndMs) < static_cast<int32_t>(PIR_COOLDOWN_MS)) {
    LOG("%lu,IGNORED_COOLDOWN,,\n", static_cast<unsigned long>(now));
    return;
  }
  sendGroup(ew::PacketType::Trigger, TRIGGER_BURST_MS);
  g_lastTriggerEndMs = millis();
  g_haveTrigger = true;
  while (xSemaphoreTake(g_pirSem, 0) == pdTRUE) {
  }  // drop edges that arrived while the group was being sent
}

void setup() {
#if LOG_SERIAL
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 1500) delay(10);  // short wait so boot lines show, never blocks without USB
  Serial.println("# ew-pir-node v0  columns: ms,event,seq,vbat_mv");
  Serial.printf("# heartbeat=%dms trigger_burst=%dms warmup=%dms cooldown=%dms tx=%ddBm\n", HEARTBEAT_MS,
                TRIGGER_BURST_MS, PIR_WARMUP_MS, PIR_COOLDOWN_MS, TX_POWER_DBM);
#endif
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, HIGH);

  Bluefruit.begin();
  Bluefruit.autoConnLed(false);
  Bluefruit.setTxPower(TX_POWER_DBM);
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
  const uint16_t interval = (ADV_INTERVAL_MS * 1000UL) / 625UL;  // units of 0.625 ms
  Bluefruit.Advertising.setInterval(interval, interval);
  Bluefruit.Advertising.setFastTimeout(0);

  g_pirSem = xSemaphoreCreateBinary();
  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), pirIsr, RISING);

  g_nextHeartbeatMs = millis() + HEARTBEAT_MS;
}

void loop() {
  const int32_t untilHeartbeat = static_cast<int32_t>(g_nextHeartbeatMs - millis());
  const TickType_t wait = untilHeartbeat > 0 ? pdMS_TO_TICKS(untilHeartbeat) : 0;

  if (xSemaphoreTake(g_pirSem, wait) == pdTRUE) {
    handlePirEdge();
    return;
  }
  sendGroup(ew::PacketType::Heartbeat, HEARTBEAT_BURST_MS);
  g_nextHeartbeatMs += HEARTBEAT_MS;
  if (static_cast<int32_t>(g_nextHeartbeatMs - millis()) < 0) g_nextHeartbeatMs = millis() + HEARTBEAT_MS;
}

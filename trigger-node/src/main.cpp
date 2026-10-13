// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Camera node trigger board (XIAO nRF52840).
//
// Always on. Scans for the PIR node's BLE advertising packets (see TriggerPacket.h). When a new
// Trigger group arrives it raises the camera's boost enable (EN_PIN), then reads the camera's UART
// lines (see CameraMsg.h) and drops the enable when the window ends or the camera reports it is done.
// Everything is logged to the USB serial port as one CSV line per event.
//
// The packet-grouping and enable-window rules live in TriggerLogic.h and are unit tested on the host.

#include <Arduino.h>
#include <CameraMsg.h>
#include <TriggerLogic.h>
#include <TriggerPacket.h>
#include <bluefruit.h>
#include <queue.h>

// ---- configuration (override with -D in platformio.ini) --------------------------------------
#ifndef EN_PIN
#define EN_PIN D6  // MiniBoost EN
#endif
#ifndef UART_RX_PIN
#define UART_RX_PIN D7  // camera TX
#endif
#ifndef UART_DUMMY_TX_PIN
#define UART_DUMMY_TX_PIN D5  // Serial1 wants a TX pin; D6 is EN, so park TX on a spare pin and leave it unwired
#endif
#ifndef SCAN_INTERVAL_MS
#define SCAN_INTERVAL_MS 100
#endif
#ifndef SCAN_WINDOW_MS
#define SCAN_WINDOW_MS 50
#endif
#ifndef CAMERA_WINDOW_MS
#define CAMERA_WINDOW_MS 45000  // how long the camera stays powered after a trigger (30 s - 1 min per the notes)
#endif
#ifndef CAMERA_MAX_MS
#define CAMERA_MAX_MS 90000  // cap when repeated triggers keep extending the window
#endif
#ifndef CUT_ON_DONE
#define CUT_ON_DONE 1  // drop EN shortly after the camera prints EW,DONE
#endif
#ifndef DONE_GRACE_MS
#define DONE_GRACE_MS 500
#endif
#ifndef GROUP_IDLE_MS
#define GROUP_IDLE_MS 1000  // a packet group is closed after this long with nothing heard
#endif
#ifndef UART_POLL_MS
#define UART_POLL_MS 5  // how often the UART is read while the camera is powered
#endif
#ifndef IDLE_TICK_MS
#define IDLE_TICK_MS 250  // wake-up period while idle, to close packet groups
#endif
#ifndef LOG_SERIAL
#define LOG_SERIAL 1
#endif
#ifndef LOG_ALL_PACKETS
#define LOG_ALL_PACKETS 0  // 1 logs every packet of a burst, not just the first of each group
#endif
#ifndef LOG_UART_NOISE
#define LOG_UART_NOISE 0  // 1 logs UART lines that are not EW messages (ESP32 boot text)
#endif
#ifndef LED_STATUS
#define LED_STATUS 1  // blue LED on while the camera is powered
#endif

#if LOG_SERIAL
#define LOG(...) Serial.printf(__VA_ARGS__)
#else
#define LOG(...) \
  do {           \
  } while (0)
#endif

// ---- state -----------------------------------------------------------------------------------
struct RxEvent {
  ew::Packet pkt;
  int8_t rssi;
  uint32_t ms;
};

static QueueHandle_t g_rxQueue;
static uint32_t g_queueDrops = 0;
static ew::GroupTracker g_tracker;
static ew::EnableWindow g_window(CAMERA_WINDOW_MS, CAMERA_MAX_MS, DONE_GRACE_MS, CUT_ON_DONE != 0);

static char g_line[96];
static size_t g_lineLen = 0;
static bool g_lineOverflow = false;

static const char* typeName(ew::PacketType t) { return t == ew::PacketType::Trigger ? "TRIGGER" : "HEARTBEAT"; }

// ---- BLE scan --------------------------------------------------------------------------------
// Runs in the Bluefruit callback task. Only decodes and queues; the work happens in loop().
static void onScan(ble_gap_evt_adv_report_t* report) {
  uint8_t buf[ew::kManufDataSize + 4];
  const uint8_t len =
      Bluefruit.Scanner.parseReportByType(report, BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, buf, sizeof buf);
  ew::Packet pkt{};
  if (len == ew::kManufDataSize && ew::decode(buf, len, pkt)) {
    const RxEvent ev{pkt, report->rssi, millis()};
    if (xQueueSend(g_rxQueue, &ev, 0) != pdTRUE) g_queueDrops++;
  }
  Bluefruit.Scanner.resume();
}

// ---- enable window ---------------------------------------------------------------------------
static void applyEn(ew::EnEvent e) {
  const unsigned long now = millis();
  switch (e) {
    case ew::EnEvent::Raised:
      digitalWrite(EN_PIN, HIGH);
#if LED_STATUS
      digitalWrite(LED_BLUE, LOW);  // active low
#endif
      LOG("%lu,EN,HIGH,trigger\n", now);
      break;
    case ew::EnEvent::Extended:
      LOG("%lu,EN,EXTEND,trigger\n", now);
      break;
    case ew::EnEvent::DroppedTimeout:
    case ew::EnEvent::DroppedDone:
      digitalWrite(EN_PIN, LOW);
#if LED_STATUS
      digitalWrite(LED_BLUE, HIGH);
#endif
      LOG("%lu,EN,LOW,%s\n", now, e == ew::EnEvent::DroppedDone ? "done" : "timeout");
      break;
    case ew::EnEvent::None:
      break;
  }
}

static void logGroup(const ew::GroupSummary& g) {
  LOG("%lu,GROUP,%u,%u,%d\n", static_cast<unsigned long>(millis()), static_cast<unsigned>(g.seq),
      static_cast<unsigned>(g.count), static_cast<int>(g.bestRssi));
}

static void handleRx(const RxEvent& ev) {
  const ew::RxResult r = g_tracker.onPacket(ev.pkt.seq, ev.rssi, ev.ms);
  // Raise EN before anything is logged, so the log costs no latency.
  if (r.newGroup && ev.pkt.type == ew::PacketType::Trigger) applyEn(g_window.onTrigger(millis()));
  if (r.prevClosed) logGroup(r.prev);
  if (r.newGroup || LOG_ALL_PACKETS) {
    // ms,RX,type,seq,rssi,sender_uptime_ms,vbat_mv,gap,reset
    LOG("%lu,RX,%s,%u,%d,%lu,%u,%u,%d\n", static_cast<unsigned long>(ev.ms), typeName(ev.pkt.type),
        static_cast<unsigned>(ev.pkt.seq), static_cast<int>(ev.rssi), static_cast<unsigned long>(ev.pkt.uptimeMs),
        static_cast<unsigned>(ev.pkt.vbatMv), static_cast<unsigned>(r.gap), r.senderReset ? 1 : 0);
  }
}

// ---- camera UART -----------------------------------------------------------------------------
static void handleCameraLine(const char* line) {
  const unsigned long now = millis();
  ew::CameraMsg m{};
  if (!ew::parseCameraLine(line, m)) {
#if LOG_UART_NOISE
    LOG("%lu,CAM_NOISE,%s\n", now, line);
#endif
    return;
  }
  LOG("%lu,CAM,%s\n", now, line);
  if (m.type == ew::CameraMsgType::Done) g_window.onDone(now);
}

static void pollUart() {
  while (Serial1.available()) {
    const int c = Serial1.read();
    if (c < 0 || c == '\r') continue;
    if (c == '\n') {
      if (!g_lineOverflow && g_lineLen > 0) {
        g_line[g_lineLen] = '\0';
        handleCameraLine(g_line);
      }
      g_lineLen = 0;
      g_lineOverflow = false;
      continue;
    }
    if (g_lineLen < sizeof g_line - 1) g_line[g_lineLen++] = static_cast<char>(c);
    else g_lineOverflow = true;
  }
}

// ---- setup / loop ----------------------------------------------------------------------------
void setup() {
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, LOW);  // camera off. Before this line the pin floats: give EN a pulldown if the module needs one.
#if LED_STATUS
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, HIGH);
#endif

#if LOG_SERIAL
  Serial.begin(115200);
  const uint32_t start = millis();
  while (!Serial && millis() - start < 1500) delay(10);  // short wait so boot lines show, never blocks without USB
  Serial.println("# ew-trigger-node v0  RX: ms,RX,type,seq,rssi,sender_uptime_ms,vbat_mv,gap,reset");
  Serial.println("#   GROUP: ms,GROUP,seq,packets_heard,best_rssi   EN: ms,EN,HIGH|LOW|EXTEND,reason   CAM: ms,CAM,<camera line>");
  Serial.printf("# scan=%d/%dms window=%dms max=%dms cut_on_done=%d\n", SCAN_WINDOW_MS, SCAN_INTERVAL_MS,
                CAMERA_WINDOW_MS, CAMERA_MAX_MS, CUT_ON_DONE);
#endif

  Serial1.setPins(UART_RX_PIN, UART_DUMMY_TX_PIN);
  Serial1.begin(115200);

  g_rxQueue = xQueueCreate(16, sizeof(RxEvent));
  Bluefruit.begin(0, 1);
  Bluefruit.autoConnLed(false);
  Bluefruit.Scanner.setRxCallback(onScan);
  Bluefruit.Scanner.setInterval((SCAN_INTERVAL_MS * 1000UL) / 625UL, (SCAN_WINDOW_MS * 1000UL) / 625UL);
  Bluefruit.Scanner.useActiveScan(false);
  Bluefruit.Scanner.start(0);  // 0 = scan forever
  LOG("%lu,EN,LOW,boot\n", static_cast<unsigned long>(millis()));
}

void loop() {
  // Block until a packet arrives, or for a short tick. While the camera is powered the tick is short, to read its UART.
  const TickType_t wait = pdMS_TO_TICKS(g_window.enabled() ? UART_POLL_MS : IDLE_TICK_MS);
  RxEvent ev;
  if (xQueueReceive(g_rxQueue, &ev, wait) == pdTRUE) {
    do {
      handleRx(ev);
    } while (xQueueReceive(g_rxQueue, &ev, 0) == pdTRUE);
  }

  pollUart();

  const uint32_t now = millis();
  ew::GroupSummary s{};
  if (g_tracker.flushIfIdle(now, GROUP_IDLE_MS, s)) logGroup(s);
  applyEn(g_window.tick(now));

  if (g_queueDrops) {
    LOG("%lu,WARN,rx_queue_full,%lu\n", static_cast<unsigned long>(now), static_cast<unsigned long>(g_queueDrops));
    g_queueDrops = 0;
  }
}

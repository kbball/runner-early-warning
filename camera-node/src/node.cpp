// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Camera node camera (XIAO ESP32-S3 Sense).
//
// Powered only while the trigger board holds the boost EN high. On boot it reports itself on its UART
// (D6 = TX, GPIO43), grabs frames for a session, reports a verdict, then sleeps. The trigger board reads the
// UART lines (see CameraMsg.h) and drops EN when it sees DONE. Debug text goes to the USB serial port.
//
// UART lines:   EW,BOOT,<ms>   EW,FRAME,<ms>,<n>   EW,VERDICT,<ms>,PERSON|NONE,<score>   EW,DONE,<ms>
//
// Person detection is Espressif's ESP-DL pedestrian model (Detector.h). Build with -D STUB_FORCE_PERSON=1 to skip
// the model and report a person after a few frames, to test the rest of the chain without anyone in front of it.

#include <CameraMsg.h>
#include <MeshCoreFrames.h>
#include <driver/uart.h>
#include <esp_camera.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>

#include "CameraConfig.h"
#include "Detector.h"
#include "Settings.h"

// ---- configuration (override with -D in platformio.ini) --------------------------------------
// The person threshold, frames-in-a-row and session length are settings kept in flash (Settings.h), changed from
// the setup page; the compile-time PERSON_THRESHOLD, PERSON_CONFIRM_FRAMES and SESSION_MS are only their defaults.
#ifndef FRAME_INTERVAL_MS
#define FRAME_INTERVAL_MS 0  // extra pause between frames; the model takes ~130 ms a frame already
#endif
#ifndef FRAME_LOG_FIRST
#define FRAME_LOG_FIRST 5  // send an EW,FRAME line for each of the first frames...
#endif
#ifndef FRAME_LOG_EVERY
#define FRAME_LOG_EVERY 10  // ...then for every Nth
#endif
#ifndef POST_DONE_SLEEP_MS
#define POST_DONE_SLEEP_MS 3000  // after DONE, wait this long for the boost to drop, then deep sleep
#endif
#ifndef EW_UART_BAUD
#define EW_UART_BAUD 115200
#endif
#ifndef STUB_FORCE_PERSON
#define STUB_FORCE_PERSON 0
#endif
#ifndef STUB_PERSON_AFTER_FRAMES
#define STUB_PERSON_AFTER_FRAMES 3
#endif
#ifndef MESHCORE_FRAMES
#define MESHCORE_FRAMES 0  // 1 also sends MeshCore companion frames on a confirmed person (needs the MeshCore side)
#endif
#ifndef MESH_CHANNEL_IDX
#define MESH_CHANNEL_IDX 1
#endif
#ifndef MESH_TEXT
#define MESH_TEXT "Runner inbound"
#endif
#ifndef MESH_EPOCH_BASE
#define MESH_EPOCH_BASE 1791763200UL  // 2026-10-12 00:00 UTC; the camera has no clock, so time = this + uptime
#endif

#define DBG(...) printf(__VA_ARGS__)

static constexpr uart_port_t kUart = UART_NUM_0;  // D6 = TX GPIO43, D7 = RX GPIO44
static constexpr int kUartTx = 43;
static constexpr int kUartRx = 44;

static uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }
static void delayMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

// ---- UART to the trigger board ---------------------------------------------------------------
static void uartWrite(const void* data, size_t n) { uart_write_bytes(kUart, data, n); }

static void sendLine(const ew::CameraMsg& m) {
  char line[64];
  const size_t n = ew::formatCameraLine(m, line, sizeof line);
  if (n == 0) return;
  uartWrite(line, n);
  uartWrite("\n", 1);
}

static void sendBoot() { sendLine({ew::CameraMsgType::Boot, nowMs(), 0, false, 0}); }
static void sendFrame(uint32_t n) { sendLine({ew::CameraMsgType::Frame, nowMs(), n, false, 0}); }
static void sendVerdict(bool person, uint32_t score) {
  sendLine({ew::CameraMsgType::Verdict, nowMs(), 0, person, score});
}
static void sendDone() { sendLine({ew::CameraMsgType::Done, nowMs(), 0, false, 0}); }

#if MESHCORE_FRAMES
// Tells the MeshCore nRF to post a channel message. The replies on its TX are not read.
static void sendMeshCoreAlert() {
  uint8_t buf[ew::kMeshFrameHeader + 8 + 32];
  size_t n = ew::meshAppStart("ew-camera", buf, sizeof buf);
  if (n) uartWrite(buf, n);
  delayMs(100);
  uint8_t msg[ew::kMeshFrameHeader + 7 + ew::kMeshMaxText];
  const uint32_t ts = static_cast<uint32_t>(MESH_EPOCH_BASE) + nowMs() / 1000;
  n = ew::meshSendChannelText(MESH_CHANNEL_IDX, ts, MESH_TEXT, msg, sizeof msg);
  if (n) uartWrite(msg, n);
}
#endif

static void initUart() {
  const uart_config_t cfg = {.baud_rate = EW_UART_BAUD,
                             .data_bits = UART_DATA_8_BITS,
                             .parity = UART_PARITY_DISABLE,
                             .stop_bits = UART_STOP_BITS_1,
                             .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                             .rx_flow_ctrl_thresh = 0,
                             .source_clk = UART_SCLK_DEFAULT,
                             .flags = {}};
  uart_driver_install(kUart, 256, 0, 0, nullptr, 0);
  uart_param_config(kUart, &cfg);
  uart_set_pin(kUart, kUartTx, kUartRx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
}

// ---- detector --------------------------------------------------------------------------------
// Returns a person score 0..100 for a frame.
static uint8_t scoreFrame(ew::Detector& detector, const camera_fb_t* fb, uint32_t frameIndex) {
#if STUB_FORCE_PERSON
  (void)detector;
  (void)fb;
  return frameIndex >= STUB_PERSON_AFTER_FRAMES ? 90 : 5;
#else
  (void)frameIndex;
  ew::Detection best;
  return detector.detect(fb, &best, 1) > 0 ? static_cast<uint8_t>(best.score) : 0;
#endif
}

// ---- session ---------------------------------------------------------------------------------
static void runSession(ew::Detector& detector, const ew::Settings& set) {
  const uint32_t start = nowMs();
  uint32_t firstFrameMs = 0;
  uint32_t n = 0;
  uint8_t best = 0;
  uint8_t streak = 0;
  bool person = false;

  while (nowMs() - start < static_cast<uint32_t>(set.sessionSec) * 1000) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      DBG("# frame grab failed\n");
      delayMs(50);
      continue;
    }
    n++;
    if (n == 1) {
      firstFrameMs = nowMs();
      DBG("# first frame at %lu ms (boot-relative)\n", static_cast<unsigned long>(firstFrameMs));
    }
    if (n <= FRAME_LOG_FIRST || n % FRAME_LOG_EVERY == 0) sendFrame(n);

    const uint32_t t0 = nowMs();
    const uint8_t score = scoreFrame(detector, fb, n);
    DBG("# frame %lu: score %u (%lu ms)\n", static_cast<unsigned long>(n), static_cast<unsigned>(score),
        static_cast<unsigned long>(nowMs() - t0));
    esp_camera_fb_return(fb);

    if (score > best) best = score;
    streak = score >= set.threshold ? streak + 1 : 0;
    if (streak >= set.confirmFrames) {
      person = true;
      break;
    }
    if (FRAME_INTERVAL_MS) delayMs(FRAME_INTERVAL_MS);
  }

  sendVerdict(person, best);
#if MESHCORE_FRAMES
  if (person) sendMeshCoreAlert();
#endif
  sendDone();
  ew::appendSession({0, firstFrameMs, static_cast<uint16_t>(n > 65535 ? 65535 : n), best, static_cast<uint8_t>(person)});
  DBG("# session over: %lu frames, best score %u, person=%d\n", static_cast<unsigned long>(n),
      static_cast<unsigned>(best), person ? 1 : 0);
}

void runNode() {
  initUart();
  sendBoot();
  const ew::Settings settings = ew::loadSettings();  // first thing, so the trigger board sees the camera is up

  DBG("# ew-camera-node v0 (esp-dl pedestrian detector) threshold %u, %u frames, %u s\n",
      static_cast<unsigned>(settings.threshold), static_cast<unsigned>(settings.confirmFrames),
      static_cast<unsigned>(settings.sessionSec));

  const uint32_t t0 = nowMs();
  camera_config_t cfg = cameraConfig();
  const esp_err_t err = esp_camera_init(&cfg);
  DBG("# camera init %s (0x%x) in %lu ms\n", err == ESP_OK ? "ok" : "FAILED", static_cast<unsigned>(err),
      static_cast<unsigned long>(nowMs() - t0));

  ew::Detector detector;
#if !STUB_FORCE_PERSON
  const uint32_t m0 = nowMs();
  if (err == ESP_OK) {
    detector.begin();
    DBG("# model loaded in %lu ms\n", static_cast<unsigned long>(nowMs() - m0));
  }
#endif

  if (err == ESP_OK) {
    runSession(detector, settings);
  } else {
    sendVerdict(false, 0);
    sendDone();
  }

  uart_wait_tx_done(kUart, pdMS_TO_TICKS(200));
  delayMs(POST_DONE_SLEEP_MS);
  esp_deep_sleep_start();  // no wake source: stays asleep until power returns
}

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
// The detector is a STUB: there is no person-detection model yet, so the verdict is NONE unless
// STUB_FORCE_PERSON=1. That is enough to measure boot and frame timing and to test the whole chain.

#include <Arduino.h>
#include <CameraMsg.h>
#include <MeshCoreFrames.h>
#include <Preferences.h>
#include <SD.h>
#include <SPI.h>
#include <CameraConfig.h>
#include <esp_camera.h>
#include <esp_sleep.h>

// ---- configuration (override with -D in platformio.ini) --------------------------------------
#ifndef SESSION_MS
#define SESSION_MS 30000  // how long frames are grabbed after boot (the trigger board's window is longer)
#endif
#ifndef FRAME_INTERVAL_MS
#define FRAME_INTERVAL_MS 200  // pause between frames
#endif
#ifndef PERSON_THRESHOLD
#define PERSON_THRESHOLD 60  // detector score (0..100) that counts as a person
#endif
#ifndef PERSON_CONFIRM_FRAMES
#define PERSON_CONFIRM_FRAMES 2  // consecutive frames over the threshold before a PERSON verdict
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
#ifndef SAVE_FRAMES
#define SAVE_FRAMES 0  // 1 saves JPEGs to the microSD card under /ew
#endif
#ifndef SAVE_MAX_FRAMES
#define SAVE_MAX_FRAMES 60  // per session
#endif
#ifndef SD_CS_PIN
#define SD_CS_PIN 21  // Sense expansion board; the notes flag this pin as unverified (GPIO3 vs GPIO21)
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

#define DBG(...) Serial.printf(__VA_ARGS__)

// ---- UART to the trigger board ---------------------------------------------------------------
static void sendLine(const ew::CameraMsg& m) {
  char line[64];
  if (ew::formatCameraLine(m, line, sizeof line) == 0) return;
  Serial0.print(line);
  Serial0.print('\n');
}

static void sendBoot() { sendLine({ew::CameraMsgType::Boot, millis(), 0, false, 0}); }
static void sendFrame(uint32_t n) { sendLine({ew::CameraMsgType::Frame, millis(), n, false, 0}); }
static void sendVerdict(bool person, uint32_t score) {
  sendLine({ew::CameraMsgType::Verdict, millis(), 0, person, score});
}
static void sendDone() { sendLine({ew::CameraMsgType::Done, millis(), 0, false, 0}); }

#if MESHCORE_FRAMES
// Tells the MeshCore nRF to post a channel message. The replies on its TX are not read.
static void sendMeshCoreAlert() {
  uint8_t buf[ew::kMeshFrameHeader + 8 + 32];
  size_t n = ew::meshAppStart("ew-camera", buf, sizeof buf);
  if (n) Serial0.write(buf, n);
  delay(100);
  uint8_t msg[ew::kMeshFrameHeader + 7 + ew::kMeshMaxText];
  const uint32_t ts = static_cast<uint32_t>(MESH_EPOCH_BASE) + millis() / 1000;
  n = ew::meshSendChannelText(MESH_CHANNEL_IDX, ts, MESH_TEXT, msg, sizeof msg);
  if (n) Serial0.write(msg, n);
}
#endif

// ---- detector (stub) -------------------------------------------------------------------------
// Returns a person score 0..100 for a frame. Replace with a real model (ESP-DL or TFLite Micro).
static uint8_t detectPerson(const camera_fb_t* fb, uint32_t frameIndex) {
  (void)fb;
  (void)frameIndex;
#if STUB_FORCE_PERSON
  return frameIndex >= STUB_PERSON_AFTER_FRAMES ? 90 : 5;
#else
  return 0;
#endif
}

// ---- optional frame storage ------------------------------------------------------------------
static bool g_sdOk = false;
static uint32_t g_bootId = 0;

static void initStorage() {
  Preferences prefs;
  prefs.begin("ew", false);
  g_bootId = prefs.getUInt("boot", 0) + 1;
  prefs.putUInt("boot", g_bootId);
  prefs.end();
#if SAVE_FRAMES
  g_sdOk = SD.begin(SD_CS_PIN);
  if (g_sdOk) SD.mkdir("/ew");
  DBG("# sd %s (cs=%d)\n", g_sdOk ? "ok" : "FAILED", SD_CS_PIN);
#endif
}

static void saveFrame(const camera_fb_t* fb, uint32_t n) {
#if SAVE_FRAMES
  if (!g_sdOk || n > SAVE_MAX_FRAMES) return;
  char path[40];
  snprintf(path, sizeof path, "/ew/%lu_%03lu.jpg", static_cast<unsigned long>(g_bootId), static_cast<unsigned long>(n));
  File f = SD.open(path, FILE_WRITE);
  if (!f) return;
  f.write(fb->buf, fb->len);
  f.close();
#else
  (void)fb;
  (void)n;
#endif
}

// ---- session ---------------------------------------------------------------------------------
static void runSession() {
  const uint32_t start = millis();
  uint32_t n = 0;
  uint32_t firstFrameMs = 0;
  uint8_t best = 0;
  uint8_t streak = 0;
  bool person = false;

  while (millis() - start < SESSION_MS) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) {
      DBG("# frame grab failed\n");
      delay(50);
      continue;
    }
    n++;
    if (n == 1) {
      firstFrameMs = millis();
      DBG("# first frame at %lu ms (boot-relative)\n", static_cast<unsigned long>(firstFrameMs));
    }
    if (n <= FRAME_LOG_FIRST || n % FRAME_LOG_EVERY == 0) sendFrame(n);

    const uint8_t score = detectPerson(fb, n);
    saveFrame(fb, n);
    DBG("# frame %lu: %u bytes, score %u\n", static_cast<unsigned long>(n), static_cast<unsigned>(fb->len),
        static_cast<unsigned>(score));
    esp_camera_fb_return(fb);

    if (score > best) best = score;
    streak = score >= PERSON_THRESHOLD ? streak + 1 : 0;
    if (streak >= PERSON_CONFIRM_FRAMES) {
      person = true;
      break;
    }
    delay(FRAME_INTERVAL_MS);
  }

  sendVerdict(person, best);
#if MESHCORE_FRAMES
  if (person) sendMeshCoreAlert();
#endif
  sendDone();
  DBG("# session over: %lu frames, best score %u, person=%d\n", static_cast<unsigned long>(n),
      static_cast<unsigned>(best), person ? 1 : 0);
}

void setup() {
  Serial0.begin(EW_UART_BAUD, SERIAL_8N1, 44, 43);  // RX D7, TX D6
  sendBoot();                                       // first thing, so the trigger board sees the camera is up

  Serial.begin(115200);  // USB debug; never waited for
  DBG("# ew-camera-node v0 (stub detector)\n");

  initStorage();

  const uint32_t t0 = millis();
  camera_config_t cfg = cameraConfig();
  const esp_err_t err = esp_camera_init(&cfg);
  DBG("# camera init %s (0x%x) in %lu ms\n", err == ESP_OK ? "ok" : "FAILED", static_cast<unsigned>(err),
      static_cast<unsigned long>(millis() - t0));

  if (err == ESP_OK) {
    runSession();
  } else {
    sendVerdict(false, 0);
    sendDone();
  }

  delay(POST_DONE_SLEEP_MS);
  esp_deep_sleep_start();  // no wake source: stays asleep until power returns
}

void loop() {}

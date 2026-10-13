// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Camera pins and sensor settings for the XIAO ESP32-S3 Sense, shared by the node firmware and the bench viewer.
#pragma once

#include <esp_camera.h>

#ifndef CAM_FRAME_SIZE
#define CAM_FRAME_SIZE FRAMESIZE_QVGA  // 320x240; the detector scales it to the model's input
#endif

// RGB565 frames: that is what the detector reads, and frame2jpg() turns them into JPEG for the viewer.
static inline camera_config_t cameraConfig() {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = 15;
  c.pin_d1 = 17;
  c.pin_d2 = 18;
  c.pin_d3 = 16;
  c.pin_d4 = 14;
  c.pin_d5 = 12;
  c.pin_d6 = 11;
  c.pin_d7 = 48;
  c.pin_xclk = 10;
  c.pin_pclk = 13;
  c.pin_vsync = 38;
  c.pin_href = 47;
  c.pin_sccb_sda = 40;
  c.pin_sccb_scl = 39;
  c.pin_pwdn = -1;
  c.pin_reset = -1;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_RGB565;
  c.frame_size = CAM_FRAME_SIZE;
  c.jpeg_quality = 12;
  c.fb_count = 2;
  c.fb_location = CAMERA_FB_IN_PSRAM;
  c.grab_mode = CAMERA_GRAB_LATEST;
  c.sccb_i2c_port = -1;
  return c;
}

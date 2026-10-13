// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Person detection on camera frames: Espressif's ESP-DL pedestrian model (PICO_S8_V1, runs on the ESP32-S3).
#pragma once

#include <esp_camera.h>
#include <stdint.h>

namespace ew {

struct Detection {
  int score;   // 0..100
  int box[4];  // left, top, right, bottom, in frame pixels
};

class Detector {
 public:
  Detector();
  ~Detector();
  // Loads the model (from the "pedestrian_det" flash partition). Returns false if it cannot.
  bool begin();
  // Runs the model on an RGB565 frame. Fills `out` (best first) with up to `max` detections at or above
  // `minScore` and returns how many. Returns 0 if there is no person or the model is not loaded.
  int detect(const camera_fb_t* fb, Detection* out, int max, int minScore = 30);

 private:
  struct Impl;
  Impl* impl_;
};

}  // namespace ew

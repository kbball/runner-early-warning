// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
#include "Detector.h"

#include <algorithm>

#include "pedestrian_detect.hpp"

// The camera sends RGB565 high byte first (checked on the bench: little-endian made a window score 83, big-endian 46,
// and the person was boxed either way). Set to 0 only if a different sensor or driver changes the byte order.
#ifndef EW_RGB565_BIG_ENDIAN
#define EW_RGB565_BIG_ENDIAN 1
#endif

namespace ew {

struct Detector::Impl {
  PedestrianDetect* net = nullptr;
};

Detector::Detector() : impl_(new Impl) {}

Detector::~Detector() {
  delete impl_->net;
  delete impl_;
}

bool Detector::begin() {
  if (impl_->net) return true;
  impl_->net = new PedestrianDetect(PedestrianDetect::PICO_S8_V1, /*lazy_load=*/false);
  // Report anything the model finds above this; callers apply their own threshold.
  impl_->net->set_score_thr(0.3f);
  return true;
}

int Detector::detect(const camera_fb_t* fb, Detection* out, int max, int minScore) {
  if (!impl_->net || !fb || fb->format != PIXFORMAT_RGB565) return 0;
  dl::image::img_t img = {};
  img.data = fb->buf;
  img.width = fb->width;
  img.height = fb->height;
  img.pix_type = EW_RGB565_BIG_ENDIAN ? dl::image::DL_IMAGE_PIX_TYPE_RGB565BE : dl::image::DL_IMAGE_PIX_TYPE_RGB565LE;

  int n = 0;
  for (const auto& r : impl_->net->run(img)) {
    const int score = static_cast<int>(r.score * 100.0f + 0.5f);
    if (score < minScore || r.box.size() < 4 || n >= max) continue;
    out[n++] = {score, {r.box[0], r.box[1], r.box[2], r.box[3]}};
  }
  std::sort(out, out + n, [](const Detection& a, const Detection& b) { return a.score > b.score; });
  return n;
}

}  // namespace ew

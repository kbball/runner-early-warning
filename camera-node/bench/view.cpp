// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Bench viewer for the camera node (env camera_view). NOT the node firmware.
//
// The board becomes a Wi-Fi access point (no credentials, no internet). Join it, open http://192.168.4.1/
// and you get a live MJPEG view plus a still at /jpg. Use it to check focus, aim and exposure.
//   SSID: VIEW_SSID (default ew-camera)   password: VIEW_PASS (default ew-camera-view, 8+ chars)

#include <Arduino.h>
#include <CameraConfig.h>
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>

#ifndef VIEW_SSID
#define VIEW_SSID "ew-camera"
#endif
#ifndef VIEW_PASS
#define VIEW_PASS "ew-camera-view"
#endif

static const char kPage[] =
    "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>ew-camera</title><body style='margin:0;background:#111;color:#ddd;font:14px sans-serif'>"
    "<img src=/stream style='width:100%;max-width:960px;display:block;margin:auto'>"
    "<p style='text-align:center'><a style='color:#fb0' href=/jpg>still</a></p>";

static esp_err_t pageHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t jpgHandler(httpd_req_t* req) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return httpd_resp_send_500(req);
  httpd_resp_set_type(req, "image/jpeg");
  const esp_err_t r = httpd_resp_send(req, reinterpret_cast<const char*>(fb->buf), fb->len);
  esp_camera_fb_return(fb);
  return r;
}

static esp_err_t streamHandler(httpd_req_t* req) {
  static const char kBoundary[] = "\r\n--frame\r\n";
  httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  uint32_t frames = 0;
  const uint32_t t0 = millis();
  while (true) {
    camera_fb_t* fb = esp_camera_fb_get();
    if (!fb) return ESP_FAIL;
    char head[64];
    const int n = snprintf(head, sizeof head, "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                           static_cast<unsigned>(fb->len));
    esp_err_t r = httpd_resp_send_chunk(req, kBoundary, sizeof kBoundary - 1);
    if (r == ESP_OK) r = httpd_resp_send_chunk(req, head, n);
    if (r == ESP_OK) r = httpd_resp_send_chunk(req, reinterpret_cast<const char*>(fb->buf), fb->len);
    esp_camera_fb_return(fb);
    if (r != ESP_OK) break;  // viewer went away
    if (++frames % 30 == 0) Serial.printf("# stream: %.1f fps\n", frames * 1000.0f / (millis() - t0));
  }
  return ESP_OK;
}

void setup() {
  Serial.begin(115200);
  camera_config_t cfg = cameraConfig();
  const esp_err_t err = esp_camera_init(&cfg);
  Serial.printf("# camera init %s (0x%x)\n", err == ESP_OK ? "ok" : "FAILED", err);
  if (err != ESP_OK) return;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(VIEW_SSID, VIEW_PASS);

  httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t srv = nullptr;
  if (httpd_start(&srv, &hc) == ESP_OK) {
    const httpd_uri_t routes[] = {{"/", HTTP_GET, pageHandler, nullptr},
                                  {"/jpg", HTTP_GET, jpgHandler, nullptr},
                                  {"/stream", HTTP_GET, streamHandler, nullptr}};
    for (const auto& u : routes) httpd_register_uri_handler(srv, &u);
  }
  Serial.printf("# join Wi-Fi '%s' (password '%s'), open http://%s/\n", VIEW_SSID, VIEW_PASS,
                WiFi.softAPIP().toString().c_str());
}

void loop() { delay(1000); }

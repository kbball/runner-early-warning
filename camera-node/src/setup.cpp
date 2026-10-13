// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Setup mode: the board becomes a Wi-Fi access point (no credentials, no internet). Join it and open
// http://192.168.4.1/ for a live view with the person detections drawn on it, a form for the settings the node
// reads at boot (saved in flash, no reflash needed), and the log of recent real sessions.
//   SSID: VIEW_SSID (default ew-camera)   password: VIEW_PASS (default ew-camera-view, 8+ chars)
//
// Entered when the SETUP switch holds D1 high at power-up (see main.cpp), or always in the camera_view bench env.
// A switch-entered setup ends after SETUP_MINUTES and the board deep-sleeps, so a forgotten switch cannot keep the
// radio on.

#include <driver/gpio.h>
#include <esp_camera.h>
#include <esp_event.h>
#include <esp_http_server.h>
#include <esp_netif.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "CameraConfig.h"
#include "Detector.h"
#include "Settings.h"

#ifndef VIEW_SSID
#define VIEW_SSID "ew-camera"
#endif
#ifndef VIEW_PASS
#define VIEW_PASS "ew-camera-view"
#endif
#ifndef SETUP_MINUTES
#define SETUP_MINUTES 15
#endif

static constexpr gpio_num_t kLed = GPIO_NUM_21;  // yellow user LED, active low

static ew::Detector g_detector;
static ew::Settings g_settings;

static const char kPage[] = R"HTML(<!doctype html>
<meta name=viewport content='width=device-width,initial-scale=1'>
<title>ew-camera setup</title>
<style>
body{margin:0;background:#111;color:#ddd;font:15px sans-serif}
main{max-width:960px;margin:auto;padding:0 12px 24px}
canvas{width:100%;display:block;background:#000}
#s{text-align:center;padding:10px;font-size:22px}
h2{font-size:15px;color:#fb0;margin:18px 0 6px;text-transform:uppercase;letter-spacing:.05em}
label{display:flex;justify-content:space-between;align-items:center;padding:6px 0;border-bottom:1px solid #222}
input{width:90px;font-size:16px;padding:4px;background:#222;color:#fff;border:1px solid #444;border-radius:4px}
button{font-size:16px;padding:8px 16px;margin:10px 8px 0 0;background:#fb0;border:0;border-radius:4px}
button.g{background:#333;color:#ddd}
table{width:100%;border-collapse:collapse}td,th{padding:4px 6px;border-bottom:1px solid #222;text-align:right}
th:first-child,td:first-child{text-align:left}
small{color:#888}
</style>
<main>
<canvas id=c></canvas>
<div id=s>starting...</div>
<h2>Settings <small>(the node reads these at boot)</small></h2>
<label>Person score threshold (1-100)<input id=threshold type=number min=1 max=100></label>
<label>Frames in a row needed (1-20)<input id=confirm type=number min=1 max=20></label>
<label>Session length, seconds (5-300)<input id=session type=number min=5 max=300></label>
<label>Lowest score drawn here (0-100)<input id=showmin type=number min=0 max=100></label>
<button onclick=save()>Save</button><button class=g onclick=defaults()>Defaults</button><span id=m></span>
<h2>Recent real sessions <small>(newest first)</small></h2>
<table id=log><tr><th>#<th>first frame<th>frames<th>best<th>verdict</table>
<button class=g onclick=clearLog()>Clear log</button>
</main>
<script>
const c=document.getElementById('c'),x=c.getContext('2d'),s=document.getElementById('s');
let th=60,showMin=20;
const $=id=>document.getElementById(id);
async function tick(){
  try{
    const r=await fetch('/frame',{cache:'no-store'});
    const b=await createImageBitmap(await r.blob());
    c.width=b.width;c.height=b.height;x.drawImage(b,0,0);
    const d=(r.headers.get('X-Det')||'').split(';').filter(Boolean).map(t=>t.split(',').map(Number)).filter(v=>v[0]>=showMin);
    x.lineWidth=3;x.font='16px sans-serif';
    for(const [sc,l,t,rr,bb] of d){
      x.strokeStyle=sc>=th?'#fb0':'#8cf';x.strokeRect(l,t,rr-l,bb-t);
      x.fillStyle=x.strokeStyle;x.fillText(sc,l+3,Math.max(t-4,14));
    }
    const best=d.length?Math.max(...d.map(v=>v[0])):0;
    s.textContent=(best>=th?'PERSON ':'no person ')+best+'  ('+r.headers.get('X-Ms')+' ms)';
    s.style.color=best>=th?'#fb0':'#ddd';
  }catch(e){s.textContent='waiting for the board...'}
  setTimeout(tick,50);
}
function fill(v){th=v.threshold;showMin=v.showmin;$('threshold').value=v.threshold;$('confirm').value=v.confirm;
  $('session').value=v.session;$('showmin').value=v.showmin}
async function load(){fill(await (await fetch('/api/settings')).json())}
async function save(){
  const body=new URLSearchParams({threshold:$('threshold').value,confirm:$('confirm').value,
    session:$('session').value,showmin:$('showmin').value});
  const r=await fetch('/api/settings',{method:'POST',body});
  fill(await r.json());$('m').textContent='saved';setTimeout(()=>$('m').textContent='',2000);
}
async function defaults(){const r=await fetch('/api/settings',{method:'POST',body:'reset=1'});fill(await r.json())}
async function logs(){
  const l=await (await fetch('/api/log')).json();
  $('log').innerHTML='<tr><th>#<th>first frame<th>frames<th>best<th>verdict'+l.map(e=>
    `<tr><td>${e.seq}<td>${e.first} ms<td>${e.frames}<td>${e.best}<td>${e.person?'PERSON':'none'}`).join('');
}
async function clearLog(){await fetch('/api/log',{method:'POST'});logs()}
load();logs();setInterval(logs,5000);tick();
</script>)HTML";

static esp_err_t pageHandler(httpd_req_t* req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t sendSettings(httpd_req_t* req) {
  char j[128];
  snprintf(j, sizeof j, "{\"threshold\":%u,\"confirm\":%u,\"session\":%u,\"showmin\":%u}",
           static_cast<unsigned>(g_settings.threshold), static_cast<unsigned>(g_settings.confirmFrames),
           static_cast<unsigned>(g_settings.sessionSec), static_cast<unsigned>(g_settings.showMin));
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, j, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t settingsGet(httpd_req_t* req) { return sendSettings(req); }

static esp_err_t settingsPost(httpd_req_t* req) {
  char body[160] = "";
  const int want = req->content_len < sizeof body - 1 ? req->content_len : sizeof body - 1;
  const int got = want > 0 ? httpd_req_recv(req, body, want) : 0;
  body[got > 0 ? got : 0] = '\0';

  char v[16];
  if (httpd_query_key_value(body, "reset", v, sizeof v) == ESP_OK) {
    g_settings = ew::defaultSettings();
  } else {
    if (httpd_query_key_value(body, "threshold", v, sizeof v) == ESP_OK) g_settings.threshold = atoi(v);
    if (httpd_query_key_value(body, "confirm", v, sizeof v) == ESP_OK) g_settings.confirmFrames = atoi(v);
    if (httpd_query_key_value(body, "session", v, sizeof v) == ESP_OK) g_settings.sessionSec = atoi(v);
    if (httpd_query_key_value(body, "showmin", v, sizeof v) == ESP_OK) g_settings.showMin = atoi(v);
  }
  ew::clampSettings(&g_settings);
  if (!ew::saveSettings(g_settings)) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
  return sendSettings(req);
}

static esp_err_t logGet(httpd_req_t* req) {
  ew::SessionRecord recs[ew::kSessionLog];
  const int n = ew::readSessions(recs, ew::kSessionLog);
  char out[ew::kSessionLog * 80 + 8];
  size_t len = 0;
  out[len++] = '[';
  for (int i = 0; i < n; i++) {
    len += snprintf(out + len, sizeof out - len, "%s{\"seq\":%u,\"first\":%u,\"frames\":%u,\"best\":%u,\"person\":%u}",
                    i ? "," : "", static_cast<unsigned>(recs[i].seq), static_cast<unsigned>(recs[i].firstFrameMs),
                    static_cast<unsigned>(recs[i].frames), static_cast<unsigned>(recs[i].best),
                    static_cast<unsigned>(recs[i].person));
  }
  out[len++] = ']';
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, out, len);
}

static esp_err_t logPost(httpd_req_t* req) {
  ew::clearSessions();
  return httpd_resp_send(req, "ok", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t frameHandler(httpd_req_t* req) {
  camera_fb_t* fb = esp_camera_fb_get();
  if (!fb) return httpd_resp_send_500(req);

  ew::Detection det[8];
  const int64_t t0 = esp_timer_get_time();
  const int n = g_detector.detect(fb, det, 8, g_settings.showMin);
  char ms[24];
  snprintf(ms, sizeof ms, "%lld", (esp_timer_get_time() - t0) / 1000);

  char hdr[8 * 40] = "";
  for (int i = 0; i < n; i++) {
    char one[40];
    snprintf(one, sizeof one, "%d,%d,%d,%d,%d;", det[i].score, det[i].box[0], det[i].box[1], det[i].box[2],
             det[i].box[3]);
    strlcat(hdr, one, sizeof hdr);
  }

  uint8_t* jpg = nullptr;
  size_t jpgLen = 0;
  const bool ok = frame2jpg(fb, 80, &jpg, &jpgLen);
  esp_camera_fb_return(fb);
  if (!ok) return httpd_resp_send_500(req);

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "X-Det", hdr);
  httpd_resp_set_hdr(req, "X-Ms", ms);
  const esp_err_t r = httpd_resp_send(req, reinterpret_cast<const char*>(jpg), jpgLen);
  free(jpg);
  return r;
}

static void startAccessPoint() {
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_ap();
  wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&init);

  wifi_config_t ap = {};
  strlcpy(reinterpret_cast<char*>(ap.ap.ssid), VIEW_SSID, sizeof ap.ap.ssid);
  strlcpy(reinterpret_cast<char*>(ap.ap.password), VIEW_PASS, sizeof ap.ap.password);
  ap.ap.ssid_len = strlen(VIEW_SSID);
  ap.ap.channel = 1;
  ap.ap.max_connection = 2;
  ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
  esp_wifi_set_mode(WIFI_MODE_AP);
  esp_wifi_set_config(WIFI_IF_AP, &ap);
  esp_wifi_start();
}

// `timed`: end after SETUP_MINUTES and deep sleep (switch-entered setup). Otherwise run until power is removed.
void runSetup(bool timed) {
  g_settings = ew::loadSettings();

  gpio_config_t led = {};
  led.pin_bit_mask = 1ULL << kLed;
  led.mode = GPIO_MODE_OUTPUT;
  gpio_config(&led);

  camera_config_t cfg = cameraConfig();
  const esp_err_t err = esp_camera_init(&cfg);
  printf("# setup mode: camera init %s (0x%x)\n", err == ESP_OK ? "ok" : "FAILED", static_cast<unsigned>(err));
  if (err != ESP_OK) return;

  const int64_t m0 = esp_timer_get_time();
  g_detector.begin();
  printf("# model loaded in %lld ms\n", (esp_timer_get_time() - m0) / 1000);

  startAccessPoint();

  httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
  hc.stack_size = 8192;
  hc.max_uri_handlers = 8;
  httpd_handle_t srv = nullptr;
  if (httpd_start(&srv, &hc) == ESP_OK) {
    const httpd_uri_t routes[] = {{"/", HTTP_GET, pageHandler, nullptr},
                                  {"/frame", HTTP_GET, frameHandler, nullptr},
                                  {"/api/settings", HTTP_GET, settingsGet, nullptr},
                                  {"/api/settings", HTTP_POST, settingsPost, nullptr},
                                  {"/api/log", HTTP_GET, logGet, nullptr},
                                  {"/api/log", HTTP_POST, logPost, nullptr}};
    for (const auto& u : routes) httpd_register_uri_handler(srv, &u);
  }
  printf("# join Wi-Fi '%s' (password '%s'), open http://192.168.4.1/%s\n", VIEW_SSID, VIEW_PASS,
         timed ? " (setup ends in " "15 min)" : "");

  // Blink the yellow LED so a board left in setup is obvious.
  const int64_t end = esp_timer_get_time() + static_cast<int64_t>(SETUP_MINUTES) * 60 * 1000000;
  for (bool on = false;; on = !on) {
    gpio_set_level(kLed, on ? 0 : 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    if (timed && esp_timer_get_time() > end) break;
  }
  printf("# setup time is up, sleeping\n");
  gpio_set_level(kLed, 1);
  esp_deep_sleep_start();
}

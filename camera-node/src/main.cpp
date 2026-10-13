// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Kevin Ball
// Entry point. One firmware, two programs: the camera node (default) and setup mode, chosen at power-up.
//
// Setup mode is entered when D1 (GPIO2) reads high: the carrier's SETUP/RUN switch ties D1 to the trigger board's 3V3
// while it also forces the boost on. D1 has an internal pull-down, so with nothing connected it reads low and the
// board runs as the node. The bench env camera_view (-D EW_VIEWER=1) is always in setup mode.
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

void runNode();
void runSetup(bool timed);

static constexpr gpio_num_t kSetupPin = GPIO_NUM_2;  // D1

static bool setupSwitchOn() {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << kSetupPin;
  io.mode = GPIO_MODE_INPUT;
  io.pull_down_en = GPIO_PULLDOWN_ENABLE;
  gpio_config(&io);
  vTaskDelay(pdMS_TO_TICKS(5));  // let the pull-down and the 10k series resistor settle
  return gpio_get_level(kSetupPin) == 1;
}

extern "C" void app_main(void) {
#ifdef EW_VIEWER
  runSetup(false);
#else
  if (setupSwitchOn()) {
    runSetup(true);
  } else {
    runNode();
  }
#endif
}

#include <esp8266.h>
#include <config.h>
#include <capture.h>
#include <cgivdai.h>

// initialize the custom stuff that goes beyond esp-link
void app_init() {
  // dated-record store for VDAI readouts (must come before the scheduler may store)
  captureInit();
  // arm the automatic daily-readout scheduler
  vdaiSchedInit();
}

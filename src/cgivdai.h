//
// VDAI: device presets + commands ported from the automatenunsinn web-serial tool, plus an
// automatic daily readout that keeps machines from locking out (see capture.h for storage).

#ifndef VDAI_H
#define VDAI_H

#include <httpd/httpd.h>

// command ids (must match FlashConfig.vdai_daily_command / the web UI)
#define VDAI_CMD_NONE     0
#define VDAI_CMD_READOUT  1   // vdai: 0x05 0x1B <code> \n, then read printout w/ SYN handshake
#define VDAI_CMD_EINSAT   2   // adp:  OKOKEINSAT handshake protocol
#define VDAI_CMD_RAMSET   3   // adpalt text commands
#define VDAI_CMD_SERINI   4
#define VDAI_CMD_GIRADA   5
#define VDAI_CMD_RSOK     6
#define VDAI_CMD_MILLION  7

// preset ids (must match FlashConfig.vdai_daily_preset / the web UI)
#define VDAI_PRESET_NONE   0
#define VDAI_PRESET_VDAI   1
#define VDAI_PRESET_ADP    2
#define VDAI_PRESET_ADPALT 3
#define VDAI_PRESET_BALLY  4
#define VDAI_PRESET_BERG   5

// Apply the baud rate for a preset id (returns the baud, or 0 if none).
uint32_t vdaiPresetBaud(uint8_t preset);

// Run a command, collecting the machine's response into out (NUL-terminated, clamped to
// outmax-1). Returns the number of payload bytes collected.
uint16_t vdaiRunCommand(uint8_t command, const char *code, char *out, uint16_t outmax);

// Initialize the daily-readout scheduler (arms a periodic timer). Call from app_init.
void vdaiSchedInit(void);

// HTTP handlers
int vdaiPreset(HttpdConnData *connData);   // GET /vdai/preset?p=<name>
int vdaiReadout(HttpdConnData *connData);  // GET /vdai/readout?code=&save=
int vdaiEinsat(HttpdConnData *connData);   // GET /vdai/einsat
int vdaiCmd(HttpdConnData *connData);      // GET /vdai/cmd?c=<NAME>
int vdaiTime(HttpdConnData *connData);     // GET /vdai/time?t=<HHMMDDMMYY>
int vdaiDaily(HttpdConnData *connData);    // GET/POST /vdai/daily

#endif

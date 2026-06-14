/*
 * VDAI presets + commands, ported from automatenunsinn.github.io/src/vdai.ts.
 *
 * The ESP-01 is left plugged into a gaming/amusement machine. It exposes the machine's
 * read-out commands over the esp-link web UI and, more importantly, performs an automatic
 * daily readout so the machine does not lock out (some lock after 60 days without a
 * readout). Each daily statistic (<2KB) is stored to flash as a dated record (see capture.h)
 * so it can be retrieved later over WiFi.
 *
 * Protocol documentation: http://www.baersch-online.de/pcadapter.htm
 */

#include <esp8266.h>
#include <osapi.h>
#include "sntp.h"
#include "cgi.h"
#include "config.h"
#include "capture.h"
#include "cgivdai.h"
#include "serial/uart.h"
#include "serial/serled.h"

// Read-loop tuning. Polls are short so interrupts (and the watchdog) get serviced between
// them; the loop ends after IDLE_US of silence or once MAX_US total has elapsed.
#define POLL_US   60000      // 60ms per uart0_rx_poll call
#define IDLE_US   1200000    // 1.2s of silence => transfer done
#define MAX_US    6000000    // overall cap to bound how long we block

// feed the hardware watchdog (same register the SDK uart task pokes)
#define WDT_FEED() WRITE_PERI_REG(0x60000914, 0x73)

// Shared command/response buffer. Used by the (synchronous, non-reentrant) readout paths
// and the daily scheduler — these never run concurrently in the cooperative SDK task model.
// Aligned because captureStore writes it straight to flash.
static char vdaiBuf[CAPTURE_MAX_PAYLOAD] __attribute__((aligned(4)));

//===== preset / baud

uint32_t ICACHE_FLASH_ATTR vdaiPresetBaud(uint8_t preset) {
  switch (preset) {
    case VDAI_PRESET_VDAI:   return 9600;
    case VDAI_PRESET_ADP:    return 4800;
    case VDAI_PRESET_ADPALT: return 4800;
    case VDAI_PRESET_BALLY:  return 110;
    case VDAI_PRESET_BERG:   return 110;
    default:                 return 0;
  }
}

static uint8_t ICACHE_FLASH_ATTR presetByName(const char *n) {
  if (os_strcmp(n, "vdai")   == 0) return VDAI_PRESET_VDAI;
  if (os_strcmp(n, "adp")    == 0) return VDAI_PRESET_ADP;
  if (os_strcmp(n, "adpalt") == 0) return VDAI_PRESET_ADPALT;
  if (os_strcmp(n, "bally")  == 0) return VDAI_PRESET_BALLY;
  if (os_strcmp(n, "berg")   == 0) return VDAI_PRESET_BERG;
  return VDAI_PRESET_NONE;
}

static void ICACHE_FLASH_ATTR applyPreset(uint8_t preset) {
  uint32_t baud = vdaiPresetBaud(preset);
  if (baud) uart0_baud(baud);
}

//===== read loops

// Generic collector: read until idle/timeout, append to out (bounded). If ackSyn, echo a
// 0x16 (SYN) back the first time one is seen (VDAI printer handshake). If ackEach, send a
// '@' for every received byte (ADP "Einsatz" handshake).
static uint16_t ICACHE_FLASH_ATTR
vdaiCollect(char *out, uint16_t outmax, bool ackSyn, bool ackEach) {
  uint16_t total = 0;
  bool syn = false;
  char tmp[64];
  uint32_t last = system_get_time();
  uint32_t start = last;

  while (1) {
    uint16_t got = uart0_rx_poll(tmp, sizeof(tmp), POLL_US);
    WDT_FEED();
    system_soft_wdt_feed();
    uint32_t now = system_get_time();
    if (got > 0) {
      last = now;
      for (uint16_t i = 0; i < got; i++) {
        if (total < outmax - 1) out[total++] = tmp[i];
        if (ackEach) uart0_write_char('@');
        if (ackSyn && !syn && tmp[i] == 0x16) { syn = true; uart0_write_char(0x16); }
      }
    } else if (now - last > IDLE_US) {
      break;
    }
    // note: system_get_time() wraps ~every 71min, so only trust the elapsed test when it
    // hasn't wrapped; the idle test above is the primary exit and uses fresh deltas.
    if (now >= start && now - start > MAX_US) break;
  }
  if (ackSyn && !syn) uart0_write_char(0x16); // final ack, mirrors the web tool
  out[total] = 0;
  return total;
}

// Build and send the 17-char VDAI command: 0x05 0x1B <code padded to 17> '\n'.
static void ICACHE_FLASH_ATTR vdaiSendReadout(const char *code) {
  char buf[20];
  buf[0] = 0x05;
  buf[1] = 0x1B;
  int n = 0;
  for (; n < 17 && code && code[n]; n++) buf[2 + n] = code[n];
  for (; n < 17; n++) buf[2 + n] = ' ';   // pad to 17
  buf[19] = '\n';
  uart0_tx_buffer(buf, 20);
}

uint16_t ICACHE_FLASH_ATTR
vdaiRunCommand(uint8_t command, const char *code, char *out, uint16_t outmax) {
  if (outmax == 0) return 0;
  out[0] = 0;
  serledFlash(50);

  switch (command) {
    case VDAI_CMD_READOUT:
      vdaiSendReadout(code);
      for (int d = 0; d < 10; d++) os_delay_us(10000); // ~100ms, like the web tool
      return vdaiCollect(out, outmax, true, false);

    case VDAI_CMD_EINSAT: {
      uart0_tx_buffer("OKOKEINSAT", 10);
      char hs[4];
      uart0_rx_poll(hs, 1, 2000000); // wait up to 2s for the handshake byte
      uart0_write_char('@');
      return vdaiCollect(out, outmax, false, true);
    }

    case VDAI_CMD_RAMSET:
    case VDAI_CMD_SERINI:
    case VDAI_CMD_GIRADA:
    case VDAI_CMD_RSOK:
    case VDAI_CMD_MILLION: {
      static const char *names[] = { "", "", "", "RAMSET", "SERINI", "GIRADA", "RSOK", "MILLIONENSPIEL" };
      uart0_tx_buffer((char *)names[command], os_strlen(names[command]));
      uart0_write_char('\r');
      return vdaiCollect(out, outmax, false, false);
    }

    default:
      return 0;
  }
}

//===== daily scheduler

static ETSTimer vdaiSchedTimer;
static uint32_t vdaiTicks;        // 60s ticks since boot
static uint32_t vdaiLastRunTick;  // tick of last fallback run
static uint32_t vdaiLastRunDay;   // SNTP day-number of last run (ts/86400)

#define SCHED_PERIOD_MS 60000
#define TICKS_PER_DAY   (24 * 60)  // 1440 ticks of 60s

// Convert a (timezone-adjusted) unix timestamp to a YYYYMMDD integer.
static uint32_t ICACHE_FLASH_ATTR vdaiYmd(uint32_t ts) {
  int z = (int)(ts / 86400) + 719468;
  int era = (z >= 0 ? z : z - 146096) / 146097;
  unsigned doe = (unsigned)(z - era * 146097);
  unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  int y = (int)yoe + era * 400;
  unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  unsigned mp = (5 * doy + 2) / 153;
  unsigned d = doy - (153 * mp + 2) / 5 + 1;
  unsigned m = mp < 10 ? mp + 3 : mp - 9;
  y += (m <= 2);
  return (uint32_t)y * 10000 + m * 100 + d;
}

// Perform the configured daily readout and store the result as a dated record.
static void ICACHE_FLASH_ATTR vdaiDoDaily(uint32_t date, uint32_t ts, uint16_t flags) {
  applyPreset(flashConfig.vdai_daily_preset);
  uint16_t len = vdaiRunCommand(flashConfig.vdai_daily_command, flashConfig.vdai_daily_code,
                                vdaiBuf, sizeof(vdaiBuf));
  os_printf("VDAI daily readout: %d bytes, date=%u\n", len, date);
  captureStore(date, ts, flags, vdaiBuf, len);
}

static void ICACHE_FLASH_ATTR vdaiSchedCb(void *arg) {
  vdaiTicks++;
  if (!flashConfig.vdai_daily_enable || flashConfig.vdai_daily_command == VDAI_CMD_NONE)
    return;

  uint32_t ts = sntp_get_current_timestamp(); // local time (tz applied), 0 if not synced
  if (ts > 0) {
    uint32_t day  = ts / 86400;
    uint32_t hour = (ts / 3600) % 24;
    if (hour == flashConfig.vdai_daily_hour && day != vdaiLastRunDay) {
      uint32_t date = vdaiYmd(ts);
      vdaiLastRunDay = day;
      vdaiLastRunTick = vdaiTicks;
      if (date != captureNewestDate()) // avoid a duplicate run after a same-day reboot
        vdaiDoDaily(date, ts, 0);
    }
  } else {
    // no clock: run every 24h since boot (or since the last run)
    if (vdaiTicks - vdaiLastRunTick >= TICKS_PER_DAY) {
      vdaiLastRunTick = vdaiTicks;
      vdaiDoDaily(0, vdaiTicks * (SCHED_PERIOD_MS / 1000), CAPTURE_F_BOOTREL);
    }
  }
}

void ICACHE_FLASH_ATTR vdaiSchedInit(void) {
  vdaiTicks = 0;
  vdaiLastRunTick = 0;
  vdaiLastRunDay = 0;
  os_timer_disarm(&vdaiSchedTimer);
  os_timer_setfn(&vdaiSchedTimer, vdaiSchedCb, NULL);
  os_timer_arm(&vdaiSchedTimer, SCHED_PERIOD_MS, 1); // repeating
}

//===== HTTP handlers

int ICACHE_FLASH_ATTR vdaiPreset(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;
  char arg[16];
  int n = httpdFindArg(connData->getArgs, "p", arg, sizeof(arg));
  uint8_t preset = n > 0 ? presetByName(arg) : VDAI_PRESET_NONE;
  uint32_t baud = vdaiPresetBaud(preset);
  if (baud) uart0_baud(baud);

  char buff[64];
  jsonHeader(connData, baud ? 200 : 400);
  os_sprintf(buff, "{\"baud\": %u}", baud);
  httpdSend(connData, buff, -1);
  return HTTPD_CGI_DONE;
}

// shared: run a command and return its captured output as a plain-text body
static int ICACHE_FLASH_ATTR
runAndRespond(HttpdConnData *connData, uint8_t command, const char *code) {
  uint16_t len = vdaiRunCommand(command, code, vdaiBuf, sizeof(vdaiBuf));

  // optionally store this manual readout as a dated record (save=1)
  char s[4];
  if (httpdFindArg(connData->getArgs, "save", s, sizeof(s)) > 0 && atoi(s)) {
    uint32_t ts = sntp_get_current_timestamp();
    if (ts > 0) captureStore(vdaiYmd(ts), ts, 0, vdaiBuf, len);
    else        captureStore(0, 0, CAPTURE_F_BOOTREL, vdaiBuf, len);
  }

  httpdStartResponse(connData, 200);
  httpdHeader(connData, "Content-Type", "text/plain; charset=iso-8859-1");
  httpdEndHeaders(connData);
  httpdSend(connData, vdaiBuf, len);
  return HTTPD_CGI_DONE;
}

int ICACHE_FLASH_ATTR vdaiReadout(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;
  char code[20];
  int n = httpdFindArg(connData->getArgs, "code", code, sizeof(code));
  return runAndRespond(connData, VDAI_CMD_READOUT, n > 0 ? code : flashConfig.vdai_daily_code);
}

int ICACHE_FLASH_ATTR vdaiEinsat(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;
  return runAndRespond(connData, VDAI_CMD_EINSAT, NULL);
}

int ICACHE_FLASH_ATTR vdaiCmd(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;
  char name[20];
  int n = httpdFindArg(connData->getArgs, "c", name, sizeof(name));
  uint8_t cmd = VDAI_CMD_NONE;
  if (n > 0) {
    if (os_strcmp(name, "RAMSET") == 0)              cmd = VDAI_CMD_RAMSET;
    else if (os_strcmp(name, "SERINI") == 0)         cmd = VDAI_CMD_SERINI;
    else if (os_strcmp(name, "GIRADA") == 0)         cmd = VDAI_CMD_GIRADA;
    else if (os_strcmp(name, "RSOK") == 0)           cmd = VDAI_CMD_RSOK;
    else if (os_strcmp(name, "MILLIONENSPIEL") == 0) cmd = VDAI_CMD_MILLION;
  }
  if (cmd == VDAI_CMD_NONE) {
    errorResponse(connData, 400, "unknown command");
    return HTTPD_CGI_DONE;
  }
  return runAndRespond(connData, cmd, NULL);
}

// GET /vdai/time?t=HHMMDDMMYY -> send **ZEIT*:<t>0000\r (client formats the digits)
int ICACHE_FLASH_ATTR vdaiTime(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;
  char t[16];
  int n = httpdFindArg(connData->getArgs, "t", t, sizeof(t));
  if (n < 10) {
    errorResponse(connData, 400, "need 10 digits HHMMDDMMYY");
    return HTTPD_CGI_DONE;
  }
  char msg[32];
  int len = os_sprintf(msg, "**ZEIT*:%s0000\r", t);
  serledFlash(50);
  uart0_tx_buffer(msg, len);
  jsonHeader(connData, 200);
  httpdSend(connData, "{}", -1);
  return HTTPD_CGI_DONE;
}

// GET/POST /vdai/daily -> read or update the daily-readout schedule config
int ICACHE_FLASH_ATTR vdaiDaily(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;

  // any of these args present => update
  uint8_t got = 0;
  got |= getBoolArg(connData,   "enable",  &flashConfig.vdai_daily_enable) > 0;
  got |= getUInt8Arg(connData,  "preset",  &flashConfig.vdai_daily_preset) > 0;
  got |= getUInt8Arg(connData,  "command", &flashConfig.vdai_daily_command) > 0;
  got |= getUInt8Arg(connData,  "hour",    &flashConfig.vdai_daily_hour) > 0;
  got |= getStringArg(connData, "code", flashConfig.vdai_daily_code,
                      sizeof(flashConfig.vdai_daily_code)) > 0;
  if (got) configSave();

  char buff[256];
  os_sprintf(buff,
    "{\"enable\":%d,\"preset\":%d,\"command\":%d,\"hour\":%d,\"code\":\"%s\","
    "\"newest_date\":%u}",
    flashConfig.vdai_daily_enable, flashConfig.vdai_daily_preset,
    flashConfig.vdai_daily_command, flashConfig.vdai_daily_hour,
    flashConfig.vdai_daily_code, captureNewestDate());
  jsonHeader(connData, 200);
  httpdSend(connData, buff, -1);
  return HTTPD_CGI_DONE;
}

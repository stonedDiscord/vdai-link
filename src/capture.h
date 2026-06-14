// Dated-record store for VDAI daily readouts.
//
// Keeps a ring of fixed-size records (one per 4KB flash sector) in the reserved user-page
// flash region. Each record holds one readout's output (a day's statistic, <2KB) tagged
// with the date/time it was taken so it can be retrieved later over WiFi. The number of
// records retained equals the number of sectors available in the user region (which is
// small: ~1 on a 512KB ESP-01, more on larger modules); the oldest is overwritten first.

#ifndef CAPTURE_H
#define CAPTURE_H

#include <httpd/httpd.h>

// Max payload bytes kept per record (a day's statistic is well under this).
#define CAPTURE_MAX_PAYLOAD 2048

// flags bits
#define CAPTURE_F_BOOTREL 0x0001  // ts is boot-relative seconds, date is not a real calendar date

// Scan the flash region and build the in-memory record index. Call once at boot.
void captureInit(void);

// Store one record. date is YYYYMMDD (0 if unknown), ts is unix time or boot-relative
// seconds, flags as above. len is clamped to CAPTURE_MAX_PAYLOAD. Returns true on success.
// NOTE: buf must be 4-byte aligned (it is written straight to flash); use an aligned buffer.
bool captureStore(uint32_t date, uint32_t ts, uint16_t flags, const char *buf, uint16_t len);

// Date (YYYYMMDD) of the most recently stored record, or 0 if none / unknown.
uint32_t captureNewestDate(void);

// HTTP handlers (registered in main.c builtInUrls)
int ajaxRecordList(HttpdConnData *connData);   // GET  /vdai/records       -> JSON index
int ajaxRecordGet(HttpdConnData *connData);    // GET  /vdai/records/get?i=N -> payload text
int ajaxRecordClear(HttpdConnData *connData);  // POST /vdai/records/clear  -> erase all

#endif

// Dated-record store for VDAI daily readouts. See capture.h for the rationale.
//
// Layout: the reserved user-page flash region (config.c getUserPageSectionStart/End) is
// treated as a ring of 4KB sectors, one record per sector (erase granularity is a sector,
// so one-per-sector keeps writes simple and robust). Each sector starts with a RecHdr
// followed by up to CAPTURE_MAX_PAYLOAD bytes. New records take the oldest sector (lowest
// seq, empties first); seq is a monotically increasing counter so "oldest" and ordering are
// unambiguous.

#include <esp8266.h>
#include <osapi.h>
#include "cgi.h"
#include "config.h"
#include "capture.h"

#define CAP_SECT     4096
#define CAP_MAGIC    0x52454331   // 'REC1'

typedef struct {
  uint32_t magic;
  uint32_t seq;     // 0 == empty/never written
  uint32_t date;    // YYYYMMDD, 0 if unknown
  uint32_t ts;      // unix time, or boot-relative seconds if CAPTURE_F_BOOTREL
  uint16_t len;     // payload length in bytes
  uint16_t flags;
  uint32_t pad;     // keep header 4-byte aligned and a round 24 bytes
} RecHdr;

static uint32_t cap_base;          // flash byte address of region start
static uint16_t cap_nsect;         // number of record slots (sectors)
static uint32_t cap_maxseq;        // highest seq seen
static uint32_t cap_newest_date;   // date of the record with the highest seq
static bool     cap_ok;            // region valid / persistence available

static uint32_t ICACHE_FLASH_ATTR sectAddr(uint16_t idx) {
  return cap_base + (uint32_t)idx * CAP_SECT;
}

static bool ICACHE_FLASH_ATTR readHdr(uint16_t idx, RecHdr *hdr) {
  return spi_flash_read(sectAddr(idx), (uint32_t*)hdr, sizeof(RecHdr)) == SPI_FLASH_RESULT_OK
      && hdr->magic == CAP_MAGIC && hdr->seq != 0 && hdr->len <= CAPTURE_MAX_PAYLOAD;
}

void ICACHE_FLASH_ATTR captureInit(void) {
  cap_ok = false; cap_nsect = 0; cap_maxseq = 0; cap_newest_date = 0;

  uint32_t start = getUserPageSectionStart();
  uint32_t end   = getUserPageSectionEnd();
  if (start == 0xFFFFFFFF || end == 0xFFFFFFFF || end <= start) {
    os_printf("VDAI capture: no usable flash region\n");
    return;
  }
  cap_base  = start;
  cap_nsect = (end - start) / CAP_SECT;
  if (cap_nsect == 0) return;
  cap_ok = true;

  // scan existing records to find the highest seq (newest) and its date
  for (uint16_t i = 0; i < cap_nsect; i++) {
    RecHdr hdr;
    if (readHdr(i, &hdr) && hdr.seq > cap_maxseq) {
      cap_maxseq = hdr.seq;
      cap_newest_date = hdr.date;
    }
  }
  os_printf("VDAI capture: %d record slot(s), newest seq=%u date=%u\n",
            cap_nsect, cap_maxseq, cap_newest_date);
}

uint32_t ICACHE_FLASH_ATTR captureNewestDate(void) {
  return cap_newest_date;
}

bool ICACHE_FLASH_ATTR
captureStore(uint32_t date, uint32_t ts, uint16_t flags, const char *buf, uint16_t len) {
  if (!cap_ok) return false;
  if (len > CAPTURE_MAX_PAYLOAD) len = CAPTURE_MAX_PAYLOAD;

  // pick the oldest slot: an empty one if any, else the lowest seq
  uint16_t target = 0;
  uint32_t bestseq = 0xFFFFFFFF;
  for (uint16_t i = 0; i < cap_nsect; i++) {
    RecHdr hdr;
    uint32_t s = readHdr(i, &hdr) ? hdr.seq : 0; // empty == 0 == oldest
    if (s < bestseq) { bestseq = s; target = i; if (s == 0) break; }
  }

  uint32_t addr = sectAddr(target);
  if (spi_flash_erase_sector(addr / CAP_SECT) != SPI_FLASH_RESULT_OK) {
    os_printf("VDAI capture: erase failed\n");
    return false;
  }

  RecHdr hdr;
  os_memset(&hdr, 0, sizeof(hdr));
  hdr.magic = CAP_MAGIC;
  hdr.seq   = ++cap_maxseq;
  hdr.date  = date;
  hdr.ts    = ts;
  hdr.len   = len;
  hdr.flags = flags;
  if (spi_flash_write(addr, (uint32_t*)&hdr, sizeof(hdr)) != SPI_FLASH_RESULT_OK)
    return false;

  if (len > 0) {
    // spi_flash_write needs a word-aligned source and a multiple-of-4 length. buf is
    // required to be 4-byte aligned (callers use an aligned static buffer); write the whole
    // words directly from it, then the trailing 1-3 bytes via a small zero-padded word.
    uint16_t whole = len & ~3;
    if (whole &&
        spi_flash_write(addr + sizeof(hdr), (uint32_t*)buf, whole) != SPI_FLASH_RESULT_OK)
      return false;
    uint16_t rem = len - whole;
    if (rem) {
      uint32_t tail = 0;
      os_memcpy(&tail, buf + whole, rem);
      if (spi_flash_write(addr + sizeof(hdr) + whole, &tail, 4) != SPI_FLASH_RESULT_OK)
        return false;
    }
  }

  cap_newest_date = date;
  return true;
}

//===== HTTP handlers

// GET /vdai/records -> {"slots":N, "records":[{"i":idx,"seq":..,"date":..,"ts":..,"len":..,"bootrel":0|1},...]}
int ICACHE_FLASH_ATTR ajaxRecordList(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;
  char buff[1024];
  int len = os_sprintf(buff, "{\"slots\": %d, \"records\": [", cap_nsect);
  bool first = true;
  for (uint16_t i = 0; i < cap_nsect && len < (int)sizeof(buff) - 128; i++) {
    RecHdr hdr;
    if (!readHdr(i, &hdr)) continue;
    len += os_sprintf(buff + len,
        "%s{\"i\":%d,\"seq\":%u,\"date\":%u,\"ts\":%u,\"len\":%d,\"bootrel\":%d}",
        first ? "" : ",", i, hdr.seq, hdr.date, hdr.ts, hdr.len,
        (hdr.flags & CAPTURE_F_BOOTREL) ? 1 : 0);
    first = false;
  }
  len += os_sprintf(buff + len, "]}");
  jsonHeader(connData, 200);
  httpdSend(connData, buff, len);
  return HTTPD_CGI_DONE;
}

// GET /vdai/records/get?i=N -> the record payload as a downloadable text file
int ICACHE_FLASH_ATTR ajaxRecordGet(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;
  char arg[8];
  int n = httpdFindArg(connData->getArgs, "i", arg, sizeof(arg));
  int idx = n > 0 ? atoi(arg) : -1;

  RecHdr hdr;
  if (idx < 0 || idx >= cap_nsect || !readHdr(idx, &hdr)) {
    errorResponse(connData, 404, "no such record");
    return HTTPD_CGI_DONE;
  }

  static char pbuf[CAPTURE_MAX_PAYLOAD] __attribute__((aligned(4)));
  uint16_t rlen = (hdr.len + 3) & ~3;
  if (rlen > 0) spi_flash_read(sectAddr(idx) + sizeof(hdr), (uint32_t*)pbuf, rlen);

  char cd[64];
  os_sprintf(cd, "attachment; filename=\"vdai_%u.txt\"", hdr.date ? hdr.date : hdr.seq);
  httpdStartResponse(connData, 200);
  httpdHeader(connData, "Content-Type", "text/plain; charset=iso-8859-1");
  httpdHeader(connData, "Content-Disposition", cd);
  httpdEndHeaders(connData);
  httpdSend(connData, pbuf, hdr.len);
  return HTTPD_CGI_DONE;
}

// POST /vdai/records/clear -> erase the whole region
int ICACHE_FLASH_ATTR ajaxRecordClear(HttpdConnData *connData) {
  if (connData->conn == NULL) return HTTPD_CGI_DONE;
  if (cap_ok) {
    for (uint16_t i = 0; i < cap_nsect; i++)
      spi_flash_erase_sector(sectAddr(i) / CAP_SECT);
    cap_maxseq = 0;
    cap_newest_date = 0;
  }
  jsonHeader(connData, 200);
  httpdSend(connData, "{}", -1);
  return HTTPD_CGI_DONE;
}

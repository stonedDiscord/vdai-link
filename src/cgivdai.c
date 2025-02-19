/*
 *
 * Documentation about the protocol used : see http://www.baersch-online.de/pcadapter.htm
 *
 *
 */

#include <esp8266.h>
#include <osapi.h>
#include "cgi.h"
#include "config.h"
#include "serial/uart.h"
#include "serial/serbridge.h"
#include "serial/serled.h"

#define INIT_DELAY     150   // wait this many millisecs before sending anything
#define BAUD_INTERVAL  600   // interval after which we change baud rate

static uint32_t baudRate;    // baud rate at which we're programming

static void initBaud(void);

int ICACHE_FLASH_ATTR vdaiC(HttpdConnData *connData) {

}

int ICACHE_FLASH_ATTR vdaiE(HttpdConnData *connData) {
  
}

int ICACHE_FLASH_ATTR vdaiG(HttpdConnData *connData) {
  
}

int ICACHE_FLASH_ATTR vdaiL(HttpdConnData *connData) {
  
}

int ICACHE_FLASH_ATTR vdaiK(HttpdConnData *connData) {
  
}

int ICACHE_FLASH_ATTR vdaiS(HttpdConnData *connData) {
  
}

static void ICACHE_FLASH_ATTR setBaud() {
  baudRate = baudRates[(baudCnt++) % 4];
  uart0_baud(baudRate);
  //DBG("OB changing to %ld baud\n", baudRate);
}

static void ICACHE_FLASH_ATTR initBaud() {
  baudRates[0] = flashConfig.baud_rate;
  setBaud();
}
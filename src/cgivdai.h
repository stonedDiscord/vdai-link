//

#ifndef VDAI_H
#define VDAI_H

#include <httpd/httpd.h>

int ICACHE_FLASH_ATTR vdaiC(HttpdConnData *connData);
int ICACHE_FLASH_ATTR vdaiE(HttpdConnData *connData);
int ICACHE_FLASH_ATTR vdaiG(HttpdConnData *connData);
int ICACHE_FLASH_ATTR vdaiL(HttpdConnData *connData);
int ICACHE_FLASH_ATTR vdaiK(HttpdConnData *connData);
int ICACHE_FLASH_ATTR vdaiS(HttpdConnData *connData);

#endif

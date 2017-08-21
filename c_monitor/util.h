#ifndef _H_UTIL_
#define _H_UTIL_

#include <stdint.h>
#include <unistd.h>

uint64_t util_CRC64(uint8_t * buf, size_t len);
uint64_t util_CRC64Rev(uint8_t * buf, size_t len);

#endif

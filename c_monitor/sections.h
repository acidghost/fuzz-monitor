#ifndef _H_SECTIONS_
#define _H_SECTIONS_

#include <inttypes.h>

int64_t section_find(const char *filename, const char *sec_name,
                     uint64_t *sec_start, uint64_t *sec_end);

#endif

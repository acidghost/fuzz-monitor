#ifndef _H_SECTIONS_
#define _H_SECTIONS_

#include <inttypes.h>

typedef struct section_bounds {
    uint64_t sec_start;
    uint64_t sec_end;
} section_bounds_t;

int64_t section_find(const char *filename, const char *sec_name, section_bounds_t *bounds);

#endif

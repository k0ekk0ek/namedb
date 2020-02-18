#ifndef NSD_DNAME_H
#define NSD_DNAME_H

#include <stdint.h>

int dname_parse_wire(uint8_t *wirefmt, const char *name);

#endif /* NSD_DNAME_H */

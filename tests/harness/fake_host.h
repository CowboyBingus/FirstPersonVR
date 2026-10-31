/* The tests' FpvHost: reads are plain copies (guarded by a list of readable ranges), the log is kept for CHECK_LOG,
 * and hooks are recorded rather than placed. */
#ifndef FPV_FAKE_HOST_H
#define FPV_FAKE_HOST_H

#include <stddef.h>
#include <stdint.h>
#include "fpv.h"

/* A fresh host: no readable memory, an empty log, exe_base 0. */
void fake_setup(void);
/* Lets the mod read [address, address + size). */
void fake_readable(const void* address, size_t size);
int fake_log_has(const char* text);
const char* fake_log_text(void);

#endif

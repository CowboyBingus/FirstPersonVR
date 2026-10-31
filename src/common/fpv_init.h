/* The entry files' view of the host seam: fpv_host.c fills FpvHost from HellForge's HfApi. */
#ifndef FPV_INIT_H
#define FPV_INIT_H

#include "fpv.h"
#include "hellforge_mod.h"

/* Checks the SDK (13 or newer, with every member the mods use) and the game build, logging why when it refuses, and
 * fills FpvHost. 1 ready, 0 not (the mod should then return 1 from hellforge_mod_init). */
int fpv_host_init(const HfApi* api, HfMod* mod);
const HfApi* fpv_api(void);
HfMod* fpv_mod(void);

#endif

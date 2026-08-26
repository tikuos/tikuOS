#ifndef TIKU_HOST_NVM_REGION_STUB_H_
#define TIKU_HOST_NVM_REGION_STUB_H_
/* The host has no carved NVM region, which selects the byte-writable
 * persist buffers -- the path the config's own comment promises lands
 * "in plain .bss (volatile test harness)". */
#define TIKU_NVM_HAS_REGION 0
#endif

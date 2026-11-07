#include <stddef.h>
#include <psp2/kernel/rng.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/processmgr.h>
#include <mbedtls/platform_time.h>
#include "entropy_poll.h"
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)(sceKernelGetProcessTimeWide() / 1000);
}
int mbedtls_hardware_poll(void *context, unsigned char *output, size_t len, size_t *olen) {
    (void)context;
    *olen = 0;
    while (*olen < len) {
        size_t chunk = len - *olen;
        if (chunk > 64) chunk = 64;
        if (sceKernelGetRandomNumber(output + *olen, chunk) < 0) return -1;
        *olen += chunk;
    }
    return 0;
}

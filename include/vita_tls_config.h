#pragma once
#define MBEDTLS_NO_PLATFORM_ENTROPY
#define MBEDTLS_ENTROPY_HARDWARE_ALT
#define MBEDTLS_PLATFORM_MS_TIME_ALT
// Networking belongs to curl. The SDK's BSD sockets do not provide every
// ioctl used by Mbed TLS's optional example socket layer.
#undef MBEDTLS_NET_C
#undef MBEDTLS_TIMING_C

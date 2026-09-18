#ifndef OPLUS_FASTBOOT_H
#define OPLUS_FASTBOOT_H
#include <stdint.h>
#include <stdbool.h>
typedef enum {
    FASTBOOT_PATCH_ERROR = -1,
    FASTBOOT_PATCH_NOT_FOUND = 0,
    FASTBOOT_PATCH_APPLIED = 1,
    FASTBOOT_PATCH_ALREADY_APPLIED = 2
} FastbootPatchResult;

FastbootPatchResult patch_fastboot(char* buffer, int32_t size);
#endif /* OPLUS_FASTBOOT_H */

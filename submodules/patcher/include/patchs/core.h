#ifndef PATCHS_CORE_H
#define PATCHS_CORE_H
#include <stdint.h>
#include <stdbool.h>
/* 双模式接口移植自 ditvelo/gbl_root_canoe，来源及许可见 THIRD_PARTY_NOTICES.md。 */
typedef enum {
    PATCH_MODE_NORMAL = 0,
    PATCH_MODE_FAKE_LOCKED = 1
} PatchMode;

/* 失败时保持输入缓冲区不变；normal 必须从未做假回锁的 ABL 生成。 */
bool PatchBufferEx(char* data, int32_t size, PatchMode mode);
bool PatchBuffer(char* data, int32_t size);
#endif /* PATCHS_CORE_H */

#ifndef PATCHS_CORE_H
#define PATCHS_CORE_H
#include <stdint.h>
#include <stdbool.h>
/* normal 模式参考 ditvelo/gbl_root_canoe 的双模式实现，适用仓库根 LICENSE。 */
typedef enum {
    PATCH_MODE_NORMAL = 0,
    PATCH_MODE_FAKE_LOCKED = 1
} PatchMode;

/* 两个模式均从原始 loader 生成，失败时输入缓冲区保持不变。 */
bool PatchBufferWithMode(char* data, int32_t size, PatchMode mode, bool fix_fastboot);
bool PatchBuffer(char* data, int32_t size);
/* BDS 运行时控制官方 Fastboot 时，生成阶段保留原始验证分支。 */
bool PatchBufferEx(char* data, int32_t size, bool fix_fastboot);
#endif /* PATCHS_CORE_H */

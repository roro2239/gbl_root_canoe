/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __SUPER_FB_BOOT_OPTIONS_H__
#define __SUPER_FB_BOOT_OPTIONS_H__

#include <Uefi.h>

typedef struct {
  BOOLEAN BootWarning;
  BOOLEAN FixFastboot;
  BOOLEAN ShowUiHints; /* 控制启动进度、启动页 BL 详情及子菜单过渡提示。 */
} SFB_BOOT_OPTIONS;

EFI_STATUS
SfbLoadBootOptions (OUT SFB_BOOT_OPTIONS *Options);

EFI_STATUS
SfbSaveBootOptions (IN CONST SFB_BOOT_OPTIONS *Options);

/* 读取持久化的真实解锁状态，不使用被假回锁修改的状态变量。 */
EFI_STATUS
SfbReadBlState (OUT BOOLEAN *Unlocked);

/* 调用方先验证原始镜像；只修改本次启动的内存副本。识别失败时不修改。 */
EFI_STATUS
SfbPatchBootImage (IN OUT VOID *Buffer, IN UINTN Size,
                   IN CONST SFB_BOOT_OPTIONS *Options);

#endif

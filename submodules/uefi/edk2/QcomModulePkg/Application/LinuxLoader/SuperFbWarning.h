/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __SUPER_FB_WARNING_H__
#define __SUPER_FB_WARNING_H__

#include <Uefi.h>

/* 仅恢复已知去黄字补丁的显示路径；识别失败时不修改缓冲区。 */
EFI_STATUS
SfbEnableBootWarning (IN OUT VOID *Buffer, IN UINTN Size);

#endif

/** @file
 *  ArbTools - a standalone UEFI tool launched from the super-fastboot boot
 *  menu. Reads each AVB rollback-index slot from the DeviceInfo blob (via the
 *  Verified Boot protocol, ported from the r32 tree) and offers a reset that
 *  zeroes every slot and writes it back.
 *
 *  Copyright (c) 2026, contributors to the canoe ABL tree.
 *  SPDX-License-Identifier: BSD-3-Clause
 */

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include "AtDevInfo.h"
#include "AndroidToolsUi.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

/* ---- ported r32 DeviceInfo / rollback helpers --------------------------- */

EFI_STATUS
AtDevInfoRead (
  OUT DeviceInfo *DevInfo
  )
{
  QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;
  EFI_STATUS                 Status;

  if (DevInfo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->LocateProtocol (&gEfiQcomVerifiedBootProtocolGuid, NULL,
                                (VOID **)&VbIntf);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AT: VB protocol not found: %r\n", Status));
    return Status;
  }

  return VbIntf->VBRwDeviceState (VbIntf, READ_CONFIG, (UINT8 *)DevInfo,
                                  (UINT32)sizeof (DeviceInfo));
}

EFI_STATUS
AtDevInfoWrite (
  IN CONST DeviceInfo *DevInfo
  )
{
  QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf;
  EFI_STATUS                 Status;

  if (DevInfo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Status = gBS->LocateProtocol (&gEfiQcomVerifiedBootProtocolGuid, NULL,
                                (VOID **)&VbIntf);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return VbIntf->VBRwDeviceState (VbIntf, WRITE_CONFIG, (UINT8 *)DevInfo,
                                  (UINT32)sizeof (DeviceInfo));
}

EFI_STATUS
AtReadRollbackIndex (
  IN  CONST DeviceInfo *DevInfo,
  IN  UINT32            Loc,
  OUT UINT64            *RollbackIndex
  )
{
  if (DevInfo == NULL || RollbackIndex == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  if (Loc >= MAX_VB_PARTITIONS) {
    return EFI_INVALID_PARAMETER;
  }
  *RollbackIndex = DevInfo->rollback_index[Loc];
  return EFI_SUCCESS;
}

VOID
AtClearRollbackIndex (
  IN OUT DeviceInfo *DevInfo
  )
{
  if (DevInfo == NULL) {
    return;
  }
  SetMem (DevInfo->rollback_index, sizeof (DevInfo->rollback_index), 0);
}

/* ---- screens ------------------------------------------------------------ */

/**
  Show every rollback-index slot whose value is non-zero, in a scrollable list.
  Power returns. Slots that are still zero are skipped, so only the ARB indexes
  that are actually in use are shown.
**/
STATIC
VOID
AtShowArbValues (
  VOID
  )
{
  DeviceInfo  Info;
  EFI_STATUS  Status;
  CHAR16    **Lines = NULL;
  UINTN       Used;
  UINTN       Index;
  UINT64      Val;

  Status = AtDevInfoRead (&Info);
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"读取 DeviceInfo", Status);
    return;
  }

  /* Refuse to touch a blob whose magic does not match: the persist partition
   * would be uninitialized or corrupt, so the "existing" fields are garbage
   * and writing them back could clobber unrelated areas (unlock state, keys). */
  if (CompareMem (Info.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE) != 0) {
    AtUiShowMessage (L"DeviceInfo 尚未初始化");
    AtUiWaitForKey (0);
    return;
  }

  Lines = AllocateZeroPool (MAX_VB_PARTITIONS * sizeof (CHAR16 *));
  if (Lines == NULL) {
    AtUiReportStatus (L"分配内存", EFI_OUT_OF_RESOURCES);
    goto Out;
  }

  /* Walk every possible slot, keep only the non-zero ones. */
  Used = 0;
  for (Index = 0; Index < MAX_VB_PARTITIONS; Index++) {
    AtReadRollbackIndex (&Info, (UINT32)Index, &Val);
    if (Val == 0) {
      continue;
    }
    Lines[Used] = AllocatePool (40 * sizeof (CHAR16));
    if (Lines[Used] == NULL) {
      AtUiReportStatus (L"分配内存", EFI_OUT_OF_RESOURCES);
      goto Out;
    }
    UnicodeSPrint (Lines[Used], 40 * sizeof (CHAR16),
                   L"索引 %2u：0x%016lx", (UINT32)Index, Val);
    Used++;
  }

  if (Used == 0) {
    AtUiShowMessage (L"所有 ARB 索引均为零");
    AtUiWaitForKey (0);
    goto Out;
  }

  /* Selection is ignored - power just returns to the main menu. */
  Index = 0;
  AtUiRunMenu (L"ARB 索引（非零）", (CONST CHAR16 **)Lines, Used,
               &Index, L"音量键滚动，电源键返回");

Out:
  for (Index = 0; Index < MAX_VB_PARTITIONS; Index++) {
    if (Lines != NULL && Lines[Index] != NULL) {
      FreePool (Lines[Index]);
    }
  }
  if (Lines != NULL) {
    FreePool (Lines);
  }
}

/**
  Require five separate confirmations, at least one second apart, before the
  destructive write is allowed. The reset writes the DeviceInfo blob back
  through the Verified Boot protocol, which is a TEE write that can lose keys,
  so each confirm must be a deliberate action: a 1s stall (and input flush)
  precedes every prompt, enforcing the interval and dropping any key held over
  from the previous step.

  Returns TRUE only if all five confirmations were given.
**/
STATIC
BOOLEAN
AtConfirmReset5x (
  VOID
  )
{
  UINTN  Step;
  AT_KEY Key;
  CHAR16 Progress[24];

  for (Step = 1; Step <= 5; Step++) {
    /* Enforce >=1s since the previous confirmation and drop any key held over
     * from it, so each confirm is a separate deliberate action. */
    gBS->Stall (1000000);  /* 1 second */
    gST->ConIn->Reset (gST->ConIn, FALSE);

    AtUiBeginScreen (L"重置 ARB 索引", NULL);
    AtUiDrawNote (L"警告：将写入 TEE，可能丢失密钥。");
    UnicodeSPrint (Progress, sizeof (Progress), L"确认 %u/5", (UINT32)Step);
    AtUiDrawNote (Progress);
    AtUiEndScreen (L"电源键确认，音量键取消");

    Key = AtUiWaitForKey (0);
    if (Key != AtKeySelect) {
      AtUiShowMessage (L"已取消重置");
      gBS->Stall (1000000);
      return FALSE;
    }
  }

  /* Final 1s interval before the destructive write begins. */
  gBS->Stall (1000000);
  return TRUE;
}

/**
  Zero every rollback-index slot after requiring five confirmations.
**/
STATIC
VOID
AtResetArbValues (
  VOID
  )
{
  DeviceInfo  Info;
  EFI_STATUS  Status;

  if (!AtConfirmReset5x ()) {
    return;
  }

  AtUiShowMessage (L"正在重置 ARB 索引...");

  Status = AtDevInfoRead (&Info);
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"读取 DeviceInfo", Status);
    return;
  }

  /* Only write back when the existing blob is valid, so the read-modify-write
   * preserves the real on-device fields. A bad magic means the persist data is
   * uninitialized/corrupt; zeroing rollback_index and persisting the rest would
   * clobber unrelated areas, so abort instead. */
  if (CompareMem (Info.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE) != 0) {
    AtUiReportStatus (L"DeviceInfo 标识", EFI_VOLUME_CORRUPTED);
    return;
  }

  AtClearRollbackIndex (&Info);

  Status = AtDevInfoWrite (&Info);
  if (EFI_ERROR (Status)) {
    AtUiReportStatus (L"写入 DeviceInfo", Status);
    return;
  }

  AtUiShowMessage (L"ARB 索引重置完成");
  AtUiWaitForKey (0);
}

/* ---- entry point -------------------------------------------------------- */

EFI_STATUS
EFIAPI
ArbToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  STATIC CONST CHAR16 *Items[] = {
    L"读取 ARB 值",
    L"重置 ARB 值",
    L"返回",
  };
  UINTN      Sel;
  EFI_STATUS Status;

  /*
   * The power press that selected us in the super-fastboot menu is often still
   * held when we start; drain it (after a release delay) so it cannot confirm
   * the first entry the instant the menu appears.
   */
  Status = AtUiEnterMenu (L"ARB 工具");
  if (EFI_ERROR (Status)) {
    return Status;
  }

  while (TRUE) {
    Status = AtUiRunMenu (L"ARB 工具", Items, ARRAY_SIZE (Items), &Sel,
                          L"音量键移动，电源键确认");
    if (EFI_ERROR (Status)) {
      return Status;
    }

    switch (Sel) {
    case 0:
      AtShowArbValues ();
      break;
    case 1:
      AtResetArbValues ();
      break;
    case 2:
      /* Exit the app so control returns to the boot menu. */
      return EFI_SUCCESS;
    default:
      break;
    }
  }

  return EFI_SUCCESS;
}

/** @file
 *  RebootTools - a standalone UEFI tool launched from the super-fastboot boot
 *  menu. Offers four reboot targets (Fastbootd, Bootloader, Recovery, System).
 *
 *  RebootDevice and the misc-partition BCB write are ported from the r32 tree
 *  (ShutdownServices.c / Recovery.c) but trimmed to the self-contained paths:
 *  the reset is a single gRT->ResetSystem call, and the misc partition is found
 *  by its type GUID via LocateHandleBuffer rather than the full GPT enumerator.
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
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/BlockIo.h>

#include "AtReboot.h"
#include "AndroidToolsUi.h"
#include "MenuScreen.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

/* ---- ported r32 reboot/recovery helpers --------------------------------- */

VOID
AtRebootDevice (
  IN UINT8 Reason
  )
{
  AT_RESET_DATA ResetData;
  EFI_STATUS    Status;

  Status = (Reason == NORMAL_MODE) ? EFI_SUCCESS : EFI_INVALID_PARAMETER;

  StrnCpyS (ResetData.DataBuffer, ARRAY_SIZE (ResetData.DataBuffer),
            (CONST CHAR16 *)AT_RESET_PARAM, ARRAY_SIZE (AT_RESET_PARAM) - 1);
  ResetData.Bdata = Reason;

  gRT->ResetSystem (EfiResetCold, Status, sizeof (ResetData), &ResetData);
}

EFI_STATUS
AtWriteRecoveryMessage (
  IN CHAR8 *Command
  )
{
  EFI_STATUS             Status;
  EFI_HANDLE            *Handles = NULL;
  UINTN                  Count = 0;
  EFI_BLOCK_IO_PROTOCOL *BlkIo = NULL;
  UINT32                 BlkSize;
  UINTN                  Size;
  VOID                  *Buf = NULL;
  struct RecoveryMessage *Msg;

  if (Command == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  /* The platform partition driver installs the partition type GUID as a
   * protocol on each partition handle, so misc is found directly. */
  Status = gBS->LocateHandleBuffer (ByProtocol, &gEfiMiscPartitionGuid, NULL,
                                    &Count, &Handles);
  if (EFI_ERROR (Status) || Count == 0) {
    DEBUG ((DEBUG_ERROR, "AT: misc partition not found: %r\n", Status));
    return (EFI_ERROR (Status)) ? Status : EFI_NOT_FOUND;
  }

  Status = gBS->HandleProtocol (Handles[0], &gEfiBlockIoProtocolGuid,
                                (VOID **)&BlkIo);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AT: misc BlockIo open failed: %r\n", Status));
    FreePool (Handles);
    return Status;
  }

  BlkSize = (UINT32)BlkIo->Media->BlockSize;
  if (BlkSize == 0) {
    BlkSize = 4096;
  }
  /* Read/write two blocks, mirroring r32's two-page window. */
  Size = (UINTN)BlkSize * 2;

  Buf = AllocateZeroPool (Size);
  if (Buf == NULL) {
    FreePool (Handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = BlkIo->ReadBlocks (BlkIo, BlkIo->Media->MediaId, 0, Size, Buf);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AT: misc read failed: %r\n", Status));
    goto Out;
  }

  /* UFS / non-NAND: the bootloader message lives at offset 0. */
  Msg = (struct RecoveryMessage *)Buf;
  AsciiStrnCpyS (Msg->Command, sizeof (Msg->Command), Command,
                 AsciiStrLen (Command));

  Status = BlkIo->WriteBlocks (BlkIo, BlkIo->Media->MediaId, 0, Size, Buf);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "AT: misc write failed: %r\n", Status));
  }

  /* Best effort: flush so the BCB is durable before the reset. */
  if (BlkIo->FlushBlocks != NULL) {
    BlkIo->FlushBlocks (BlkIo);
  }

Out:
  FreePool (Buf);
  FreePool (Handles);
  return Status;
}

/* ---- menu --------------------------------------------------------------- */

STATIC CONST struct {
  CONST CHAR16   *Label;
  CHAR8          *BootCommand;
  UINT8          ResetReason;
} mTargets[] = {
  { L"进入 Fastbootd", AT_RECOVERY_BOOT_FASTBOOT, NORMAL_MODE },
  { L"进入 Bootloader", NULL, FASTBOOT_MODE },
  { L"进入 Recovery", AT_RECOVERY_BOOT_RECOVERY, NORMAL_MODE },
  { L"重启系统", NULL, NORMAL_MODE }
};

#define BACK_ITEM      ARRAY_SIZE (mTargets)
#define CHOICE_COUNT   (BACK_ITEM + 1)
#define REBOOT_LINES   (CHOICE_COUNT + 3)

STATIC EFI_STATUS
DrawChoice (
  IN MENU_SCREEN   *Screen,
  IN UINTN         Item,
  IN UINTN         Cursor
  )
{
  CONST CHAR16 *Label = (Item == BACK_ITEM) ? L"返回" : mTargets[Item].Label;

  return MenuScreenLine (Screen, 2 + Item, (Item == Cursor) ? MenuLineSelected : MenuLineNormal,
                         Label, MAX_UINTN);
}

STATIC EFI_STATUS
DrawMenu (
  IN MENU_SCREEN   *Screen,
  IN UINTN         Cursor,
  IN CONST CHAR16  *Footer
  )
{
  EFI_STATUS Status;
  UINTN      Item;

  Status = MenuScreenClear (Screen);
  if (!EFI_ERROR (Status)) {
    Status = MenuScreenLine (Screen, 0, MenuLineTitle, L"重启工具", MAX_UINTN);
  }
  for (Item = 0; Item < CHOICE_COUNT && !EFI_ERROR (Status); Item++) {
    Status = DrawChoice (Screen, Item, Cursor);
  }
  if (!EFI_ERROR (Status)) {
    Status = MenuScreenLine (Screen, REBOOT_LINES - 1, MenuLineHint, Footer, MAX_UINTN);
  }
  return Status;
}

EFI_STATUS
EFIAPI
RebootToolsEntry (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  MENU_SCREEN   Screen;
  EFI_INPUT_KEY Key;
  EFI_STATUS    Status;
  UINTN         Cursor = 0;
  UINTN         Previous;
  UINTN         EventIndex;
  CHAR16        Notice[64];

  Status = MenuScreenOpen (&Screen, REBOOT_LINES, REBOOT_LINES);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RebootTools: opening menu failed: %r\n", Status));
    return Status;
  }

  Status = DrawMenu (&Screen, Cursor, L"音量键移动，电源键确认");
  if (!EFI_ERROR (Status)) {
    // Discard the power press that launched the application before accepting a choice.
    Status = gBS->Stall (2 * 1000 * 1000);
    if (!EFI_ERROR (Status)) {
      Status = gST->ConIn->Reset (gST->ConIn, FALSE);
    }
  }

  while (!EFI_ERROR (Status)) {
    Status = gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &EventIndex);
    if (EFI_ERROR (Status)) {
      break;
    }
    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (Status == EFI_NOT_READY) {
      Status = EFI_SUCCESS;
      continue;
    }
    if (EFI_ERROR (Status)) {
      break;
    }
    if (Key.ScanCode == SCAN_UP || Key.ScanCode == SCAN_DOWN) {
      Previous = Cursor;
      AtUiMoveCursor (&Cursor, CHOICE_COUNT, (Key.ScanCode == SCAN_UP) ? AtKeyUp : AtKeyDown);
      Status = DrawChoice (&Screen, Previous, Cursor);
      if (!EFI_ERROR (Status)) {
        Status = DrawChoice (&Screen, Cursor, Cursor);
      }
      continue;
    }
    if (Key.ScanCode != SCAN_NULL || Key.UnicodeChar != CHAR_CARRIAGE_RETURN) {
      continue;
    }
    if (Cursor == BACK_ITEM) {
      break;
    }

    Status = MenuScreenClear (&Screen);
    if (!EFI_ERROR (Status)) {
      Status = MenuScreenLine (&Screen, 3, MenuLineTitle, L"正在重启", MAX_UINTN);
    }
    if (!EFI_ERROR (Status)) {
      Status = MenuScreenLine (&Screen, 4, MenuLineNormal, mTargets[Cursor].Label, MAX_UINTN);
    }
    if (EFI_ERROR (Status)) {
      break;
    }
    if (mTargets[Cursor].BootCommand != NULL) {
      Status = AtWriteRecoveryMessage (mTargets[Cursor].BootCommand);
    }
    if (!EFI_ERROR (Status)) {
      AtRebootDevice (mTargets[Cursor].ResetReason);
      Status = EFI_DEVICE_ERROR; // A successful ResetSystem never returns.
    }
    DEBUG ((DEBUG_ERROR, "RebootTools: menu item %u failed: %r\n", Cursor, Status));
    UnicodeSPrint (Notice, sizeof (Notice), L"重启失败 0x%lx", (UINT64)Status);
    Status = DrawMenu (&Screen, Cursor, Notice);
  }

  if (!EFI_ERROR (Status)) {
    Status = MenuScreenClear (&Screen);
  }
  MenuScreenClose (&Screen);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "RebootTools: menu failed: %r\n", Status));
  }
  return Status;
}

/** @file
 *  Menu UI shared by the boot loader and its standalone tools.
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
#include <Protocol/SimpleTextIn.h>
#include <Protocol/SimpleTextOut.h>

#include "AndroidToolsUi.h"
#include "MenuScreen.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof (a) / sizeof ((a)[0]))
#endif

#define AT_SCREEN_LINES  (AT_VISIBLE_ROWS + 6)

STATIC MENU_SCREEN mScreen;
STATIC EFI_STATUS  mDrawStatus;
STATIC UINTN       mNextRow;
STATIC UINTN       mVisibleRows;

RETURN_STATUS
EFIAPI
AtUiReleaseScreen (VOID)
{
  if (mScreen.Lines != 0) {
    MenuScreenClose (&mScreen);
  }
  return EFI_SUCCESS;
}

STATIC VOID
DrawLine (IN UINTN Row, IN MENU_LINE_STYLE Style, IN CONST CHAR16 *Text, IN UINTN Length)
{
  if (!EFI_ERROR (mDrawStatus)) {
    mDrawStatus = MenuScreenLine (&mScreen, Row, Style, Text, Length);
    if (EFI_ERROR (mDrawStatus)) {
      DEBUG ((DEBUG_ERROR, "Menu: drawing row %u failed: %r\n", (UINT32)Row, mDrawStatus));
    }
  }
}

EFI_STATUS
AtUiScreenStatus (VOID)
{
  return mDrawStatus;
}

STATIC VOID
ShowBanner (IN CONST CHAR16 *Text, IN BOOLEAN ClearScreen, IN BOOLEAN TopAligned)
{
  UINTN Top;

  mDrawStatus = EFI_SUCCESS;
  /* 返回菜单时，刚运行的固件程序可能已经切换了显示模式。 */
  if (mScreen.Graphics != NULL && mScreen.Graphics->Mode->Mode != mScreen.GraphicsMode) {
    MenuScreenClose (&mScreen);
  }
  if (mScreen.Lines == 0) {
    mDrawStatus = MenuScreenOpen (&mScreen, 7, AT_SCREEN_LINES);
  }
  if (!EFI_ERROR (mDrawStatus) && ClearScreen) {
    mDrawStatus = MenuScreenClear (&mScreen);
  }
  if (EFI_ERROR (mDrawStatus)) {
    DEBUG ((DEBUG_ERROR, "Menu: opening screen failed: %r\n", mDrawStatus));
    return;
  }
  Top = mScreen.Top;
  if (TopAligned) {
    /* 启动提示使用顶部留白，避免菜单标题的位置遮挡原厂开机标志。 */
    mScreen.Top = (mScreen.Graphics != NULL) ?
                  mScreen.Graphics->Mode->Info->VerticalResolution / 16 : 1;
  }
  DrawLine (0, MenuLineTitle, Text, MAX_UINTN);
  mScreen.Top = Top;
}

VOID
AtUiShowBanner (IN CONST CHAR16 *Text, IN BOOLEAN ClearScreen)
{
  ShowBanner (Text, ClearScreen, TRUE);
}

/* Seconds to wait for the key that launched us to be released before the
 * input queue is drained. Mirrors SFB_ENTER_MENU_DELAY_S in SuperFbMenu.c:
 * without it a power press held through LoadImage/StartImage is read back at
 * once and confirms the first menu entry. */
#define AT_ENTER_MENU_DELAY_S  2

AT_KEY
AtUiWaitForKey (
  IN UINT32 TimeoutMs
  )
{
  EFI_STATUS     Status;
  EFI_EVENT      TimerEvent = NULL;
  EFI_EVENT      WaitList[2];
  UINTN          WaitCount;
  UINTN          EventIndex;
  EFI_INPUT_KEY  Key;
  AT_KEY         Result = AtKeyTimeout;

  if (EFI_ERROR (mDrawStatus)) {
    return AtKeyTimeout;
  }
  if (TimeoutMs != 0) {
    Status = gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &TimerEvent);
    if (EFI_ERROR (Status)) {
      TimerEvent = NULL;
    } else {
      /* Boot services timers count in 100ns units. */
      Status = gBS->SetTimer (TimerEvent, TimerRelative,
                              (UINT64)TimeoutMs * 10000);
      if (EFI_ERROR (Status)) {
        gBS->CloseEvent (TimerEvent);
        TimerEvent = NULL;
      }
    }
  }

  WaitList[0] = gST->ConIn->WaitForKey;
  WaitCount = 1;
  if (TimerEvent != NULL) {
    WaitList[1] = TimerEvent;
    WaitCount = 2;
  }

  while (TRUE) {
    Status = gBS->WaitForEvent (WaitCount, WaitList, &EventIndex);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "AT: WaitForEvent failed: %r\n", Status));
      break;
    }

    if (EventIndex == 1) {
      break;
    }

    Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
    if (EFI_ERROR (Status)) {
      continue;
    }

    /*
     * On the handset the Qualcomm keypad driver reports the volume keys as
     * SCAN_UP and SCAN_DOWN, and power arrives as a carriage return. Anything
     * left over counts as confirm.
     */
    if (Key.ScanCode == SCAN_UP) {
      Result = AtKeyUp;
    } else if (Key.ScanCode == SCAN_DOWN) {
      Result = AtKeyDown;
    } else {
      Result = AtKeySelect;
    }
    break;
  }

  if (TimerEvent != NULL) {
    gBS->CloseEvent (TimerEvent);
  }

  return Result;
}

/*
 * Announce the menu, then wait for the key that launched us to be released
 * before draining the input queue. Without this, a power press held through
 * the LoadImage/StartImage transition would be read back at once and confirm
 * the first menu entry. Mirrors SfbShowEnteringMenu in SuperFbMenu.c.
 */
EFI_STATUS
AtUiEnterMenu (
  IN CONST CHAR16 *Title
  )
{
  ShowBanner ((Title != NULL) ? Title : L"菜单", TRUE, FALSE);
  if (EFI_ERROR (mDrawStatus)) {
    return mDrawStatus;
  }

  /* Wait for the launching key to be released... */
  gBS->Stall (AT_ENTER_MENU_DELAY_S * 1000 * 1000);

  /* ...then drop anything typed or held during the wait so it does not leak
   * into the menu as a spurious confirm. */
  return gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* ---- drawing ------------------------------------------------------------ */

VOID
AtUiBeginScreen (
  IN CONST CHAR16 *Title,
  IN CONST CHAR16 *Subtitle
  )
{
  ShowBanner (Title, TRUE, FALSE);
  mNextRow = 1;
  mVisibleRows = 0;
  if (EFI_ERROR (mDrawStatus)) {
    return;
  }
  if (Subtitle != NULL) {
    DrawLine (mNextRow++, MenuLineHint, Subtitle, MAX_UINTN);
  }
  /* 最后两行提示和一行页脚不计入列表窗口。 */
  mVisibleRows = MIN (AT_VISIBLE_ROWS, mScreen.Lines - mNextRow - 3);
}

VOID
AtUiEndScreen (
  IN CONST CHAR16 *Footer
  )
{
  if (Footer != NULL) {
    DrawLine (mScreen.Lines - 1, MenuLineHint, Footer, MAX_UINTN);
  }
}

VOID
AtUiDrawRow (
  IN BOOLEAN       Selected,
  IN CONST CHAR16 *Marker,
  IN CONST CHAR16 *Text
  )
{
  CHAR16 Line[512];

  if (Text == NULL) {
    Text = L"";
  }
  if (Marker != NULL && Marker[0] != L'\0' && StrCmp (Marker, L" ") != 0) {
    UnicodeSPrint (Line, sizeof (Line), L"%s %s", Marker, Text);
    Text = Line;
  }
  DrawLine (mNextRow++, Selected ? MenuLineSelected : MenuLineNormal, Text, MAX_UINTN);
}

UINTN
AtUiVisibleRows (VOID)
{
  return mVisibleRows;
}

VOID
AtUiDrawNote (IN CONST CHAR16 *Text)
{
  UINTN Count;

  while (!EFI_ERROR (mDrawStatus) && *Text != 0) {
    if (mNextRow >= mScreen.Lines - 1) {
      mDrawStatus = EFI_BAD_BUFFER_SIZE;
      DEBUG ((DEBUG_ERROR, "Menu: no room for message\n"));
      return;
    }
    Count = (mNextRow == mScreen.Lines - 2) ? StrLen (Text) : MenuScreenFit (&mScreen, Text);
    DrawLine (mNextRow++, MenuLineNormal, Text, Count);
    Text += Count;
  }
}

UINTN
AtUiWindowStart (
  IN UINTN Cursor,
  IN UINTN Count,
  IN UINTN Rows
  )
{
  if (Count <= Rows) {
    return 0;
  }
  if (Cursor < Rows / 2) {
    return 0;
  }
  if (Cursor > Count - 1 - (Rows - Rows / 2 - 1)) {
    return Count - Rows;
  }
  return Cursor - Rows / 2;
}

VOID
AtUiMoveCursor (
  IN OUT UINTN *Cursor,
  IN UINTN     Count,
  IN AT_KEY    Key
  )
{
  if (Count == 0) {
    *Cursor = 0;
    return;
  }
  if (Key == AtKeyUp) {
    *Cursor = (*Cursor == 0) ? Count - 1 : *Cursor - 1;
  } else if (Key == AtKeyDown) {
    *Cursor = (*Cursor + 1 >= Count) ? 0 : *Cursor + 1;
  }
}

VOID
AtUiShowMessage (
  IN CONST CHAR16 *Text
  )
{
  ShowBanner ((Text != NULL) ? Text : L"", TRUE, FALSE);
}

VOID
AtUiReportStatus (
  IN CONST CHAR16 *What,
  IN EFI_STATUS    Status
  )
{
  CHAR16 Code[40];

  AtUiBeginScreen ((What != NULL) ? What : L"操作结果", NULL);
  UnicodeSPrint (Code, sizeof (Code), L"状态码 0x%lx", (UINT64)Status);
  AtUiDrawNote (Code);
  AtUiEndScreen (L"按电源键继续");
  AtUiWaitForKey (0);
}

EFI_STATUS
AtUiRunMenu (
  IN  CONST CHAR16  *Title,
  IN  CONST CHAR16  **Items,
  IN  UINTN          Count,
  OUT UINTN         *Selected,
  IN  CONST CHAR16  *Footer
  )
{
  UINTN   Cursor = 0;
  UINTN   Start;
  UINTN   Index;
  UINTN   Visible;
  AT_KEY  Key;

  if (Items == NULL || Selected == NULL || Count == 0) {
    return EFI_INVALID_PARAMETER;
  }

  /* Drop anything held since launch so it does not move the cursor at once. */
  gST->ConIn->Reset (gST->ConIn, FALSE);

  while (TRUE) {
    AtUiBeginScreen (Title, NULL);
    if (EFI_ERROR (mDrawStatus)) {
      return mDrawStatus;
    }
    Visible = MIN (Count, AtUiVisibleRows ());

    Start = AtUiWindowStart (Cursor, Count, Visible);
    for (Index = Start; Index < Start + Visible && Index < Count; Index++) {
      AtUiDrawRow ((BOOLEAN)(Index == Cursor), L" ", Items[Index]);
    }

    AtUiEndScreen (Footer);
    if (EFI_ERROR (mDrawStatus)) {
      return mDrawStatus;
    }

    Key = AtUiWaitForKey (0);
    if (Key == AtKeySelect) {
      *Selected = Cursor;
      return EFI_SUCCESS;
    } else if (Key == AtKeyUp || Key == AtKeyDown) {
      AtUiMoveCursor (&Cursor, Count, Key);
    }
  }
}

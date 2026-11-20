/*
 * UI for the super-fastboot boot menu.
 *
 * Three keys drive everything: volume up and volume down move the cursor, and
 * power confirms.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMenu.h"
#include "SuperFbBootOptions.h"

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/SfbSecurity.h>
#include <Library/ShutdownServices.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbMenuModuleTag = "SuperFbMenu";

#define SFB_ENTER_MENU_DELAY_S       3
#define SFB_ENTERING_SCREEN_DELAY_S  1

STATIC BOOLEAN mSubmenuInputPending;

SFB_KEY
SfbWaitForKey (IN UINT32 TimeoutMs)
{
  EFI_STATUS     Status;
  EFI_EVENT      TimerEvent = NULL;
  EFI_EVENT      WaitList[2];
  UINTN          WaitCount;
  UINTN          EventIndex;
  EFI_INPUT_KEY  Key;
  SFB_KEY        Result = SfbKeyTimeout;

  if (EFI_ERROR (AtUiScreenStatus ())) {
    return SfbKeyDisplayError;
  }
  if (mSubmenuInputPending) {
    mSubmenuInputPending = FALSE;
    Status = gBS->Stall (SFB_ENTERING_SCREEN_DELAY_S * 1000 * 1000);
    if (!EFI_ERROR (Status)) {
      Status = gST->ConIn->Reset (gST->ConIn, FALSE);
    }
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "SFB: preparing submenu input failed: %r\n", Status));
      return SfbKeyDisplayError;
    }
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
      DEBUG ((EFI_D_ERROR, "SFB: WaitForEvent failed: %r\n", Status));
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
     * SCAN_UP and SCAN_DOWN, and power arrives as a carriage return.
     *
     * Anything left over counts as confirm: on a three-key handset there is
     * nothing else it can be, so the menu stays usable even if a platform
     * reports power differently from what is expected here.
     */
    if (Key.ScanCode == SCAN_UP) {
      Result = SfbKeyUp;
    } else if (Key.ScanCode == SCAN_DOWN) {
      Result = SfbKeyDown;
    } else {
      DEBUG ((EFI_D_VERBOSE, "SFB: confirm key scan=0x%x char=0x%x\n",
              Key.ScanCode, Key.UnicodeChar));
      Result = SfbKeySelect;
    }
    break;
  }

  if (TimerEvent != NULL) {
    gBS->CloseEvent (TimerEvent);
  }

  return Result;
}

/* ---- drawing ------------------------------------------------------------ */

STATIC
CONST CHAR16 *
SfbBlStateText (IN BOOLEAN ForMenu)
{
  STATIC CHAR16 ErrorText[80];
  EFI_STATUS Status;
  BOOLEAN Unlocked;

  Status = SfbReadBlState (&Unlocked);
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (ErrorText, sizeof (ErrorText), L"BL 真实状态：读取失败（0x%lx）", (UINT64)Status);
    return ErrorText;
  }
  if (ForMenu) {
    return Unlocked ? L"已解锁" : L"未解锁";
  }
  return Unlocked ? L"BL 真实状态：已解锁" : L"BL 真实状态：已锁定";
}

VOID
SfbBeginScreen (IN CONST CHAR16 *Title OPTIONAL, IN CONST CHAR16 *Subtitle OPTIONAL)
{
  CHAR16 Text[SFB_NAME_CHARS + 80];

  UnicodeSPrint (Text, sizeof (Text), L"%s-%s",
                  (Title != NULL) ? Title : gSfbRootTitle, SfbBlStateText (TRUE));
  AtUiSetStatusLine (NULL);
  AtUiBeginScreen (Text, Subtitle);
}

VOID
SfbEndScreen (IN CONST CHAR16 *Footer)
{
  AtUiEndScreen (Footer);
}

VOID
SfbDrawRow (IN BOOLEAN Selected, IN CONST CHAR16 *Marker, IN CONST CHAR16 *Text)
{
  AtUiDrawRow (Selected, Marker, Text);
}

VOID
SfbDrawMore (IN UINTN Remaining)
{
  CHAR16 Text[32];

  UnicodeSPrint (Text, sizeof (Text), L"还有 %u 项", (UINT32)Remaining);
  AtUiDrawNote (Text);
}

/*
 * First row of the visible window, keeping the cursor inside it. Lists longer
 * than the window scroll rather than overflow the console.
 */
UINTN
SfbWindowStart (IN UINTN Cursor, IN UINTN Count, IN UINTN Rows)
{
  if (Rows == 0 || Count <= Rows) {
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
SfbMoveCursor (IN OUT UINTN *Cursor, IN UINTN Count, IN SFB_KEY Key)
{
  if (Count == 0) {
    *Cursor = 0;
    return;
  }

  if (Key == SfbKeyUp) {
    *Cursor = (*Cursor == 0) ? Count - 1 : *Cursor - 1;
  } else if (Key == SfbKeyDown) {
    *Cursor = (*Cursor + 1 >= Count) ? 0 : *Cursor + 1;
  }
}

/* Report a failure and hold the screen until the user acknowledges it. */
VOID
SfbReportStatus (IN CONST CHAR16 *What, IN EFI_STATUS Status)
{
  AtUiSetStatusLine (SfbBlStateText (FALSE));
  AtUiReportStatus (What, Status);
}

/*
 * Hand the screen over to fastboot. The menu is the last thing that draws
 * before control leaves for the fastboot loop, which prints nothing of its own
 * until a host connects, so without this the user would be staring at a boot
 * menu that no longer responds to anything.
 */
VOID
SfbShowFastbootMode (VOID)
{
  AtUiSetStatusLine (SfbBlStateText (FALSE));
  AtUiShowMessage (L"SuperFastboot 模式");
}

VOID
SfbShowBootingScreen (IN CONST CHAR16 *Name, IN BOOLEAN ClearScreen)
{
  STATIC CHAR16 ErrorText[80];
  CHAR16 Text[SFB_NAME_CHARS + 8];
  CONST CHAR16 *Title = (Name != NULL) ? Name : L"";
  CONST CHAR16 *Footer = NULL;
  SFB_BOOT_OPTIONS Options;
  EFI_STATUS Status;

  if (StrnCmp (Title, L"启动 ", 3) == 0) {
    Title += 3;
  }
  Status = SfbLoadBootOptions (&Options);
  if (EFI_ERROR (Status)) {
    UnicodeSPrint (ErrorText, sizeof (ErrorText), L"BL 真实状态：设置读取失败（0x%lx）", (UINT64)Status);
    Footer = ErrorText;
  } else if (!Options.ShowUiHints) {
    AtUiSetStatusLine (NULL);
    if (ClearScreen) {
      AtUiShowBanner (L"", TRUE);
    }
    return;
  } else {
    UnicodeSPrint (Text, sizeof (Text), L"正在启动 %s", Title);
    Title = Text;
    Footer = SfbBlStateText (FALSE);
  }
  AtUiSetStatusLine (Footer);
  AtUiShowBanner (Title, ClearScreen);
}

/*
 * Announce a power action (Power Off / Restart) and leave the message on
 * screen while the reset takes effect. Neither action returns, so the screen is
 * the last thing the user sees.
 */
VOID
SfbShowActionScreen (IN CONST CHAR16 *Text)
{
  AtUiShowMessage (Text);
}

/* ---- shared screens ------------------------------------------------------- */

BOOLEAN
SfbConfirmBootMode (IN CONST SFB_BOOT_ENTRY *Entry)
{
  UINTN Step;
  EFI_STATUS Status;
  SFB_KEY Key;
  CHAR16 Progress[24];

  for (Step = 1; Step <= 3; Step++) {
    SfbBeginScreen (L"确认启动方式",
                    SfbEntryDisplayName (&Entry->Spec, Entry->Spec.Name));
    SfbDrawRow (FALSE, L" ", L"警告：切换启动状态可能导致");
    SfbDrawRow (FALSE, L" ", L"数据无法解密，需要格式化");
    SfbDrawRow (FALSE, L" ", L"并清除数据。请先备份重要数据。");
    UnicodeSPrint (Progress, sizeof (Progress), L"确认 %u/3", (UINT32)Step);
    AtUiDrawNote (Progress);
    SfbEndScreen (L"电源键确认，音量键取消");

    /* 每步显示后等待一秒，再丢弃遗留按键，防止选择键穿透。 */
    Status = gBS->Stall (1000000);
    if (!EFI_ERROR (Status)) {
      Status = gST->ConIn->Reset (gST->ConIn, FALSE);
    }
    if (EFI_ERROR (Status)) {
      SfbReportStatus (L"准备确认按键失败", Status);
      return FALSE;
    }
    Key = SfbWaitForKey (0);
    if (Key != SfbKeySelect) {
      if (Key == SfbKeyTimeout) {
        SfbReportStatus (L"读取确认按键失败", EFI_DEVICE_ERROR);
      } else if (Key == SfbKeyUp || Key == SfbKeyDown) {
        SfbDebounceMenuExit ();
      }
      return FALSE;
    }
  }
  return TRUE;
}

VOID
SfbShowEnteringScreen (IN CONST CHAR16 *What)
{
  CHAR16 Text[SFB_NAME_CHARS + 8];
  SFB_BOOT_OPTIONS Options;
  EFI_STATUS Status;

  Status = SfbLoadBootOptions (&Options);
  if (EFI_ERROR (Status)) {
    SfbReportStatus (L"启动选项读取失败", Status);
  } else if (Options.ShowUiHints) {
    UnicodeSPrint (Text, sizeof (Text), L"正在进入%s", (What != NULL) ? What : L"菜单");
    AtUiShowMessage (Text);
  }

  /* 完整子菜单绘制后、第一次接收按键前再等待释放按键。 */
  mSubmenuInputPending = TRUE;
}

/*
 * Debounce a menu exit: the key that confirmed Back would otherwise leak into
 * the parent menu (or its trailing repeats would) and act there immediately.
 * Hold for the entering-screen delay, then drop everything queued.
 */
VOID
SfbDebounceMenuExit (VOID)
{
  gBS->Stall (SFB_ENTERING_SCREEN_DELAY_S * 1000 * 1000);
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* ---- boot menu ---------------------------------------------------------- */

STATIC
VOID
SfbRunBootOptions (VOID)
{
  SFB_BOOT_OPTIONS Options;
  SFB_BOOT_OPTIONS Next;
  EFI_STATUS Status;
  UINTN Cursor = 0;
  SFB_KEY Key;

  Status = SfbLoadBootOptions (&Options);
  if (EFI_ERROR (Status)) {
    SfbReportStatus (L"启动选项读取失败", Status);
    return;
  }
  SfbShowEnteringScreen (L"启动选项");
  while (TRUE) {
    SfbBeginScreen (L"启动选项", L"设置自动保存");
    SfbDrawRow ((BOOLEAN)(Cursor == 0), Options.BootWarning ? L"✓" : L" ",
                Options.BootWarning ? L"开机提示：开" : L"开机提示：关");
    SfbDrawRow ((BOOLEAN)(Cursor == 1), Options.FixFastboot ? L"✓" : L" ",
                Options.FixFastboot ? L"修复官方 FB：开" : L"修复官方 FB：关");
    SfbDrawRow ((BOOLEAN)(Cursor == 2), Options.ShowUiHints ? L"✓" : L" ",
                Options.ShowUiHints ? L"启动 / 菜单提示：开" : L"启动 / 菜单提示：关");
    SfbDrawRow ((BOOLEAN)(Cursor == 3), L" ", L"返回");
    SfbEndScreen (L"音量键移动，电源键切换或返回");
    Key = SfbWaitForKey (0);
    if (Key == SfbKeyDisplayError) return;
    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, 4, Key);
      continue;
    }
    if (Key != SfbKeySelect) continue;
    if (Cursor == 3) {
      SfbDebounceMenuExit ();
      return;
    }
    Next = Options;
    if (Cursor == 0) Next.BootWarning = !Options.BootWarning;
    else if (Cursor == 1) Next.FixFastboot = !Options.FixFastboot;
    else Next.ShowUiHints = !Options.ShowUiHints;
    Status = SfbSaveBootOptions (&Next);
    if (EFI_ERROR (Status)) {
      SfbReportStatus (L"启动选项保存失败", Status);
      Status = SfbLoadBootOptions (&Options);
      if (EFI_ERROR (Status)) {
        SfbReportStatus (L"启动选项读取失败", Status);
        return;
      }
    } else {
      Options = Next;
    }
    gBS->Stall (250000);
    gST->ConIn->Reset (gST->ConIn, FALSE);
  }
}

typedef enum {
  SfbRowEntry = 0,   /* launches List->Entry[Entry] */
  SfbRowFolder,      /* opens the next level of the name hierarchy */
  SfbRowBack,
  SfbRowFastboot,
  SfbRowSelector,
  SfbRowBootOptions,
  SfbRowPowerOff,
  SfbRowRestart
} SFB_ROW_KIND;

typedef struct {
  SFB_ROW_KIND  Kind;
  UINTN         Entry;
  CHAR16        Text[SFB_NAME_CHARS];
  CONST CHAR16  *FolderCaption;
} SFB_MENU_ROW;

#define SFB_MAX_MENU_ROWS  (SFB_MAX_ENTRIES + 5)

STATIC
VOID
SfbAddRow (IN OUT SFB_MENU_ROW  *Rows,
           IN OUT UINTN         *Count,
           IN SFB_ROW_KIND      Kind,
           IN UINTN             Entry,
           IN CONST CHAR16      *Text)
{
  if (*Count >= SFB_MAX_MENU_ROWS) {
    return;
  }
  Rows[*Count].Kind = Kind;
  Rows[*Count].Entry = Entry;
  Rows[*Count].FolderCaption = NULL;
  StrnCpyS (Rows[*Count].Text, SFB_NAME_CHARS, Text, SFB_NAME_CHARS - 1);
  (*Count)++;
}

/*
 * The rows of one menu level: the entries directly under Prefix, in list
 * order, with every deeper group folded into one folder row where its first
 * entry sits. The root ends with the built-ins; other levels with Back.
 */
STATIC
UINTN
SfbBuildRows (IN CONST SFB_ENTRY_LIST *List,
              IN CONST CHAR16         *Prefix,
              OUT SFB_MENU_ROW        *Rows)
{
  UINTN    Count = 0;
  UINTN    Index;
  UINTN    Row;
  CHAR16   Child[SFB_NAME_CHARS];
  BOOLEAN  IsFolder;
  BOOLEAN  Seen;
  CONST CHAR16 *MenuName;

  for (Index = 0; Index < List->Count; Index++) {
    /* Hidden entries are still normal boot entries in the boot order; only
     * the boot menu leaves them out (and a folder holding just hidden
     * descendants disappears with them). */
    if (SfbEntryNameHidden (List->Entry[Index].Spec.Name)) {
      continue;
    }
    MenuName = SfbEntryMenuName (&List->Entry[Index].Spec);
    if (!SfbEntryNameChild (MenuName, Prefix, Child,
                            ARRAY_SIZE (Child), &IsFolder)) {
      continue;
    }
    if (!IsFolder) {
      SfbAddRow (Rows, &Count, SfbRowEntry, Index,
                 SfbEntryDisplayName (&List->Entry[Index].Spec, Child));
      continue;
    }
    Seen = FALSE;
    for (Row = 0; Row < Count; Row++) {
      if (Rows[Row].Kind == SfbRowFolder && StrCmp (Rows[Row].Text, Child) == 0) {
        Seen = TRUE;
        break;
      }
    }
    if (!Seen) {
      SfbAddRow (Rows, &Count, SfbRowFolder, SFB_NO_INDEX, Child);
    }
    if (Prefix[0] == L'\0' && Row < Count &&
        SfbEntryDisplayName (&List->Entry[Index].Spec, NULL) != NULL) {
      if (StrCmp (Child, L"Boot Modes") == 0) {
        Rows[Row].FolderCaption = L"启动方式";
      } else if (StrCmp (Child, L"Android Tools") == 0) {
        Rows[Row].FolderCaption = L"Android 工具";
      }
    }
  }

  if (Prefix[0] == L'\0') {
    SfbAddRow (Rows, &Count, SfbRowFastboot, SFB_NO_INDEX, L"进入 SuperFastboot");
    SfbAddRow (Rows, &Count, SfbRowSelector, SFB_NO_INDEX, L"EFI 程序选择器");
    SfbAddRow (Rows, &Count, SfbRowBootOptions, SFB_NO_INDEX, L"启动选项");
    SfbAddRow (Rows, &Count, SfbRowPowerOff, SFB_NO_INDEX, L"关机");
    SfbAddRow (Rows, &Count, SfbRowRestart, SFB_NO_INDEX, L"重启");
  } else {
    SfbAddRow (Rows, &Count, SfbRowBack, SFB_NO_INDEX, L"返回");
  }
  return Count;
}

STATIC
VOID
SfbDrawMenu (IN CONST SFB_MENU_ROW *Rows,
             IN UINTN              Count,
             IN UINTN              Cursor,
             IN UINTN              DefaultBootMode,
             IN CONST CHAR16       *Title OPTIONAL)
{
  UINTN   Start;
  UINTN   Index;
  UINTN   Last;
  CHAR16  Text[SFB_NAME_CHARS + 4];

  SfbBeginScreen (Title, NULL);

  Start = SfbWindowStart (Cursor, Count, AtUiVisibleRows ());
  Last = Start + AtUiVisibleRows ();
  if (Last > Count) {
    Last = Count;
  }

  for (Index = Start; Index < Last; Index++) {
    /* Folder rows get a trailing '>' so it is obvious they open another list
     * rather than launch an image. */
    if (Rows[Index].Kind == SfbRowFolder) {
      UnicodeSPrint (Text, sizeof (Text), L"%s >",
                     (Rows[Index].FolderCaption != NULL) ? Rows[Index].FolderCaption : Rows[Index].Text);
      SfbDrawRow ((BOOLEAN)(Index == Cursor), L" ", Text);
    } else {
      SfbDrawRow ((BOOLEAN)(Index == Cursor),
                  (Rows[Index].Kind == SfbRowEntry && Rows[Index].Entry == DefaultBootMode)
                    ? L"✓" : L" ", Rows[Index].Text);
    }
  }

  if (Last < Count) {
    SfbDrawMore (Count - Last);
  }

  SfbEndScreen (L"音量键移动，电源键确认");
}

VOID
SfbShowEnteringMenu (VOID)
{
  SFB_ENTRY_LIST *List = SfbEntries ();
  SFB_MENU_ROW   *Rows;
  UINTN          Count;

  if (List == NULL) {
    return;
  }
  Rows = AllocateZeroPool (SFB_MAX_MENU_ROWS * sizeof (*Rows));
  if (Rows == NULL) {
    DEBUG ((EFI_D_ERROR, "SFB: cannot allocate initial menu rows\n"));
    return;
  }
  /* 启动流程已加载启动项，先显示完整缓存菜单，再等待按键释放和卷刷新。 */
  Count = SfbBuildRows (List, L"", Rows);
  SfbDrawMenu (Rows, Count, 0, SfbDefaultBootModeIndex (List), NULL);
  FreePool (Rows);
  if (EFI_ERROR (AtUiScreenStatus ())) {
    return;
  }
  gBS->Stall (SFB_ENTER_MENU_DELAY_S * 1000 * 1000);
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* Refresh the shared list from the volumes and store the result. */
STATIC
VOID
SfbRefreshEntries (VOID)
{
  SFB_ENTRY_LIST  *List = SfbEntries ();

  if (List != NULL) {
    (VOID)SfbUpdateEntries (List, NULL);
    (VOID)SfbWriteStoredEntries (List);
  }
}

/*
 * Run one level of the menu, Prefix naming it ("" for the root). Returns TRUE
 * when the user picked "Enter Fastboot"; FALSE when Back left a
 * nested level.
 */
STATIC
BOOLEAN
SfbRunMenuLevel (IN CONST CHAR16 *Prefix, IN UINTN Depth, IN CONST CHAR16 *Title OPTIONAL)
{
  SFB_ENTRY_LIST  *List = SfbEntries ();
  SFB_MENU_ROW    *Rows;
  UINTN           Count = 0;
  UINTN           Cursor = 0;
  BOOLEAN         Rebuild = TRUE;
  BOOLEAN         Fastboot = FALSE;
  SFB_KEY         Key;
  EFI_STATUS      Status;
  CHAR16          *ChildPrefix;

  Rows = AllocateZeroPool (SFB_MAX_MENU_ROWS * sizeof (*Rows));
  ChildPrefix = AllocateZeroPool (SFB_NAME_CHARS * 2 * sizeof (CHAR16));
  if (List == NULL || Rows == NULL || ChildPrefix == NULL) {
    goto Done;
  }

  while (TRUE) {
    if (Rebuild) {
      Count = SfbBuildRows (List, Prefix, Rows);
      if (Cursor >= Count) {
        Cursor = 0;
      }
      Rebuild = FALSE;
    }

    SfbDrawMenu (Rows, Count, Cursor, SfbDefaultBootModeIndex (List), Title);

    /* The menu is purely interactive: it waits for a key indefinitely and
     * never launches anything unattended. */
    Key = SfbWaitForKey (0);
    if (Key == SfbKeyDisplayError) {
      goto Done;
    }
    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, Count, Key);
      continue;
    }
    if (Count == 0) {
      continue;
    }

    switch (Rows[Cursor].Kind) {
    case SfbRowBack:
      /* Returning to the parent: debounce the confirming key first. */
      SfbDebounceMenuExit ();
      goto Done;

    case SfbRowFolder:
      if (Depth >= SFB_MAX_MENU_DEPTH) {
        SfbReportStatus (L"菜单层级过深", EFI_BUFFER_TOO_SMALL);
        break;
      }
      if (Prefix[0] == L'\0') {
        StrCpyS (ChildPrefix, SFB_NAME_CHARS * 2, Rows[Cursor].Text);
      } else {
        UnicodeSPrint (ChildPrefix, SFB_NAME_CHARS * 2 * sizeof (CHAR16),
                       L"%s\\%s", Prefix, Rows[Cursor].Text);
      }
      if (SfbRunMenuLevel (ChildPrefix, Depth + 1, (Rows[Cursor].FolderCaption != NULL) ?
                           Rows[Cursor].FolderCaption : Rows[Cursor].Text)) {
        Fastboot = TRUE;
        goto Done;
      }
      Rebuild = TRUE;
      break;

    case SfbRowFastboot:
      Fastboot = TRUE;
      goto Done;

    case SfbRowSelector:
      if (SfbSecurityLocked ()) {
        SfbReportStatus (L"设备已锁定", EFI_ACCESS_DENIED);
        break;
      }
      SfbRunFileBrowser ();
      Rebuild = TRUE;
      break;

    case SfbRowBootOptions:
      if (SfbSecurityLocked ()) {
        SfbReportStatus (L"设备已锁定", EFI_ACCESS_DENIED);
        break;
      }
      SfbRunBootOptions ();
      Rebuild = TRUE;
      break;

    case SfbRowPowerOff:
      SfbShowActionScreen (L"正在关机");
      ShutdownDevice ();
      break;

    case SfbRowRestart:
      SfbShowActionScreen (L"正在重启");
      RebootDevice (NORMAL_MODE);
      break;

    case SfbRowEntry:
    default:
      Status = SfbLaunchEntry (&List->Entry[Rows[Cursor].Entry], TRUE, NULL);
      if (EFI_ERROR (Status) && Status != EFI_ABORTED) {
        SfbReportStatus (L"启动失败", Status);
      }
      Rebuild = TRUE;
      break;
    }
  }

Done:
  if (Rows != NULL) {
    FreePool (Rows);
  }
  if (ChildPrefix != NULL) {
    FreePool (ChildPrefix);
  }
  return Fastboot;
}

BOOLEAN
SfbRunBootMenu (VOID)
{
  /* The menu always starts from a fresh scan, stored before it is shown. */
  SfbRefreshEntries ();
  return SfbRunMenuLevel (L"", 0, NULL);
}

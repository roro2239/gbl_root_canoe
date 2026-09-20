/*
 * Graphical and console UI for the super-fastboot boot menu.
 *
 * Three keys drive everything: volume up and volume down move the cursor, and
 * power confirms.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbMenu.h"
#include "SuperFbLang.h"
#include "SuperFbGfx.h"
#include <AtDevInfo.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/ShutdownServices.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/SimpleTextIn.h>

/* Keeps the translation unit legal when the feature is compiled out. */
CONST CHAR8 *gSfbMenuModuleTag = "SuperFbMenu";

#define SFB_ATTR_NORMAL    EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
#define SFB_ATTR_SELECTED  EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTGRAY)
#define SFB_ATTR_TITLE     EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK)

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

STATIC CONST CHAR16*
SfbGetFileName (IN CONST CHAR16 *Path)
{
  CONST CHAR16 *FileName = Path;
  while (*Path != L'\0') {
    if (*Path == L'\\') FileName = Path + 1;
    Path++;
  }
  return FileName;
}

STATIC BOOLEAN
SfbStrCaseEqual (IN CONST CHAR16 *Str1, IN CONST CHAR16 *Str2)
{
  while (*Str1 && *Str2) {
    CHAR16 c1 = (*Str1 >= L'a' && *Str1 <= L'z') ? *Str1 - 0x20 : *Str1;
    CHAR16 c2 = (*Str2 >= L'a' && *Str2 <= L'z') ? *Str2 - 0x20 : *Str2;
    if (c1 != c2) return FALSE;
    Str1++;
    Str2++;
  }
  return *Str1 == L'\0' && *Str2 == L'\0';
}

STATIC UINT32 mPanelTop;
STATIC UINT32 mRowsY;
STATIC UINT32 mNotesY;
STATIC UINT32 mFooterY;
STATIC UINTN  mVisibleRows = SFB_VISIBLE_ROWS;
STATIC UINTN  mRowIndex;
STATIC UINTN  mNoteIndex;

STATIC
VOID
SfbDrawCentered (IN CONST CHAR16 *Text, IN UINT32 Y, IN UINT32 Color)
{
  UINT32 W;
  UINT32 TextW;

  SfbGfxGetScreen (&W, NULL);
  TextW = SfbGfxTextWidth (Text);
  SfbGfxDrawText (Text, TextW < W ? (W - TextW) / 2 : 0, Y,
                  Color, SFB_COLOR_PANEL);
}

UINTN
SfbVisibleRows (VOID)
{
  return mVisibleRows;
}

VOID
SfbBeginScreen (IN CONST CHAR16 *Title, IN CONST CHAR16 *Subtitle)
{
  UINT32 W;
  UINT32 H;
  UINT32 PanelH;
  UINT32 Reserved;

  if (SfbGfxInit ()) {
    SfbGfxGetScreen (&W, &H);
    /* 为两条列表提示及页脚预留空间，滚动窗口使用实际可见行数。 */
    Reserved = 72 + 80 + 60 + (Subtitle != NULL ? 44 : 0);
    PanelH = MAX (H / 3, Reserved + 3 * 52);
    PanelH = MIN (PanelH, H - 32);
    mVisibleRows = MIN ((PanelH - Reserved) / 52, SFB_VISIBLE_ROWS);
    mPanelTop = (H - PanelH) / 2;
    mRowsY = mPanelTop + 72;
    mNotesY = mRowsY + (UINT32)mVisibleRows * 52;
    mFooterY = mPanelTop + PanelH - 48;
    mRowIndex = 0;
    mNoteIndex = 0;

    gST->ConOut->EnableCursor (gST->ConOut, FALSE);
    SfbGfxClear (SFB_COLOR_BG);
    SfbGfxFillRect (0, mPanelTop, W, PanelH, SFB_COLOR_PANEL);
    SfbGfxHLine (mPanelTop, 0, W - 1, 2, SFB_COLOR_ACCENT);
    SfbGfxHLine (mPanelTop + PanelH - 2, 0, W - 1, 2, SFB_COLOR_ACCENT);
    SfbDrawCentered (Title, mPanelTop + 20, SFB_COLOR_ACCENT);
    if (Subtitle != NULL) {
      SfbDrawCentered (Subtitle, mFooterY - 44, SFB_COLOR_ACCENT_D);
    }
    return;
  }

  mVisibleRows = SFB_VISIBLE_ROWS;
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  gST->ConOut->ClearScreen (gST->ConOut);
  Print (L"Chinese UI unavailable (graphics initialization failed).\r\n");
  Print (L"%s\r\n", Title);
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  if (Subtitle != NULL) {
    Print (L"%s\r\n", Subtitle);
  }
  Print (L"\r\n");
}

VOID
SfbEndScreen (IN CONST CHAR16 *Footer)
{
  if (SfbGfxActive ()) {
    SfbDrawCentered (Footer, mFooterY, SFB_COLOR_ACCENT_D);
    return;
  }
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  Print (L"\r\n%s\r\n", Footer);
}

VOID
SfbDrawRow (IN BOOLEAN Selected, IN CONST CHAR16 *Marker, IN CONST CHAR16 *Text)
{
  CHAR16 Full[SFB_PATH_CHARS + 8];
  UINT32 W;
  UINT32 Y;

  if (SfbGfxActive ()) {
    if (mRowIndex >= mVisibleRows) {
      return;
    }
    SfbGfxGetScreen (&W, NULL);
    Y = mRowsY + (UINT32)mRowIndex++ * 52;
    UnicodeSPrint (Full, sizeof (Full), L"%s %s %s",
                   Selected ? L">" : L" ", Marker, Text);
    if (Selected) {
      SfbGfxFillRect (0, Y, W, 44, SFB_COLOR_SEL_BG);
    }
    SfbGfxDrawText (Full, 24, Y + 6,
                    Selected ? SFB_COLOR_SEL_FG : SFB_COLOR_TEXT,
                    Selected ? SFB_COLOR_SEL_BG : SFB_COLOR_PANEL);
    return;
  }
  gST->ConOut->SetAttribute (gST->ConOut,
                             Selected ? SFB_ATTR_SELECTED : SFB_ATTR_NORMAL);
  Print (L"%s %s %s", Selected ? L">" : L" ", Marker, Text);
  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
  Print (L"\r\n");
}

VOID
SfbPanelNote (IN CONST CHAR16 *Text)
{
  if (SfbGfxActive ()) {
    if (mNoteIndex < 2) {
      SfbGfxDrawText (Text, 24, mNotesY + (UINT32)mNoteIndex++ * 36,
                      SFB_COLOR_ACCENT_D, SFB_COLOR_PANEL);
    }
    return;
  }
  Print (L"  %s\r\n", Text);
}

VOID
SfbDrawCountNote (IN UINTN StringId, IN UINT32 Count)
{
  CHAR16 Text[128];

  UnicodeSPrint (Text, sizeof (Text), SfbStr (StringId), Count);
  SfbPanelNote (Text);
}

/*
 * First row of the visible window, keeping the cursor inside it. Lists longer
 * than the window scroll rather than overflow the console.
 */
UINTN
SfbWindowStart (IN UINTN Cursor, IN UINTN Count, IN UINTN Rows)
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
  CHAR16 Detail[96];

  UnicodeSPrint (Detail, sizeof (Detail), L"%r (0x%lx)", Status, (UINT64)Status);
  SfbBeginScreen (What, NULL);
  SfbPanelNote (Detail);
  SfbEndScreen (SfbStr (StrPressPower));
  SfbWaitForKey (0);
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
  SfbBeginScreen (SfbStr (StrFastbootMode), NULL);
}

/* 仅映射随包提供的名称与路径组合，自定义名称仍按原文显示。 */
STATIC
CONST CHAR16 *
SfbBootDisplayName (IN CONST CHAR16 *Name, IN CONST CHAR16 *FilePath)
{
  if (Name != NULL && FilePath != NULL) {
    CONST CHAR16 *FileName = SfbGetFileName (FilePath);

    if (StrCmp (Name, L"Boot Modes") == 0 && SfbStrCaseEqual (FileName, L"BOOTMODES")) {
      return SfbStr (StrBootModes);
    }

    if (StrCmp (Name, L"Android") == 0 && SfbStrCaseEqual (FileName, L"boot.efi")) {
      return SfbStr (StrBootAndroidFakeLocked);
    }
    if (StrCmp (Name, L"Android (Real State)") == 0 &&
        SfbStrCaseEqual (FileName, L"boot_normal.efi")) {
      return SfbStr (StrBootAndroidRealState);
    }
    if (StrCmp (Name, L"Android backup") == 0 &&
        SfbStrCaseEqual (FileName, L"boot_backup.efi")) {
      return SfbStr (StrBootAndroidBackup);
    }
    if (StrCmp (Name, L"Reboot Tools") == 0 && SfbStrCaseEqual (FileName, L"RebootTools.efi")) {
      return SfbStr (StrRebootTools);
    }
    if (StrCmp (Name, L"BL Tools") == 0 && SfbStrCaseEqual (FileName, L"BLTools.efi")) {
      return SfbStr (StrBlTools);
    }
    if (StrCmp (Name, L"ARB Tools") == 0 && SfbStrCaseEqual (FileName, L"ArbTools.efi")) {
      return SfbStr (StrArbTools);
    }
    if (StrCmp (Name, L"Android Tools") == 0 &&
        SfbStrCaseEqual (FileName, L"ENTRIES")) {
      return SfbStr (StrAndroidTools);
    }
  }
  return (Name != NULL && Name[0] != L'\0') ? Name : L"...";
}

BOOLEAN
SfbConfirmBootMode (IN CONST SFB_BOOT_ENTRY *Entry)
{
  UINTN Step;
  CHAR16 Progress[40];

  for (Step = 1; Step <= 3; Step++) {
    /* 丢弃进入确认页及上一步遗留的按键，避免一次按键穿透多步。 */
    gBS->Stall (1000000);
    if (EFI_ERROR (gST->ConIn->Reset (gST->ConIn, FALSE))) {
      return FALSE;
    }
    SfbBeginScreen (SfbStr (StrConfirmBootMode),
                    SfbBootDisplayName (Entry->Desc, Entry->Path));
    SfbDrawRow (FALSE, L"", SfbStr (StrBootModeDataWarning));
    SfbDrawRow (FALSE, L"", SfbStr (StrBootModeFormatWarning));
    SfbDrawRow (FALSE, L"", SfbStr (StrBootModeBackupWarning));
    UnicodeSPrint (Progress, sizeof (Progress), SfbStr (StrConfirmThree), (UINT32)Step);
    SfbPanelNote (Progress);
    SfbEndScreen (SfbStr (StrConfirmOrCancel));
    if (SfbWaitForKey (0) != SfbKeySelect) {
      return FALSE;
    }
  }
  return TRUE;
}

/*
 * Clear the menu away and announce the launch. The loaded image prints nothing
 * of its own until it takes over, so without this the boot menu would linger on
 * screen through the load.
 */
VOID
SfbShowBootingScreen (IN CONST CHAR16 *Name,
                      IN CONST CHAR16 *FilePath,
                      IN BOOLEAN       ClearScreen)
{
  CHAR16 Text[SFB_DESC_CHARS + 32];

  if (ClearScreen) {
    UnicodeSPrint (Text, sizeof (Text), SfbStr (StrBooting),
                   SfbBootDisplayName (Name, FilePath));
    SfbBeginScreen (Text, NULL);
    return;
  }

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_TITLE);
  /*
   * An unattended default boot must not blank whatever is already on screen
   * (typically the boot splash): only clear when the launch came from the menu,
   * where the menu itself is what needs clearing away.
   */
  gST->ConOut->EnableCursor (gST->ConOut, FALSE);

  if (FilePath != NULL) {
    CONST CHAR16 *FileName = SfbGetFileName (FilePath);
    if (!SfbStrCaseEqual (FileName, L"boot.efi")) {
      Print (L"Booting %s\r\n", (Name != NULL && Name[0] != L'\0') ? Name : L"...");
    }
  } else {
    Print (L"Booting %s\r\n", (Name != NULL && Name[0] != L'\0') ? Name : L"...");
  }

  gST->ConOut->SetAttribute (gST->ConOut, SFB_ATTR_NORMAL);
}

/*
 * Announce a power action (Power Off / Restart) and leave the message on
 * screen while the reset takes effect. Neither action returns, so the screen is
 * the last thing the user sees.
 */
VOID
SfbShowActionScreen (IN CONST CHAR16 *Text)
{
  SfbBeginScreen (Text, NULL);
}

/*
 * Seconds to hold on the "Entering Boot Menu" screen before the menu starts
 * taking input. Long enough that a volume key held from power-on has been
 * released, so it does not immediately move the menu cursor.
 */
#define SFB_ENTER_MENU_DELAY_S  3

VOID
SfbShowEnteringMenu (VOID)
{
  SfbBeginScreen (SfbStr (StrEnteringBootMenu), NULL);

  /* Wait for the key to be released... */
  gBS->Stall (SFB_ENTER_MENU_DELAY_S * 1000 * 1000);

  /* ...then drop anything typed or held during the wait so it does not leak
   * into the menu as a spurious keypress. */
  gST->ConIn->Reset (gST->ConIn, FALSE);
}

/* ---- boot menu ---------------------------------------------------------- */

STATIC
EFI_STATUS
SfbReadBlState (OUT BOOLEAN *Unlocked)
{
  QCOM_VERIFIEDBOOT_PROTOCOL *VbIntf = NULL;
  DeviceInfo                 Info;
  EFI_STATUS                 Status;

  Status = gBS->LocateProtocol (&gEfiQcomVerifiedBootProtocolGuid, NULL,
                                (VOID **)&VbIntf);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (VbIntf == NULL || VbIntf->VBRwDeviceState == NULL) {
    return EFI_UNSUPPORTED;
  }

  /* 直接读取持久化状态，不使用启动模式或被假回锁修改的内存状态。 */
  SetMem (&Info, sizeof (Info), 0xFF);
  Status = VbIntf->VBRwDeviceState (VbIntf, READ_CONFIG, (UINT8 *)&Info,
                                   (UINT32)sizeof (Info));
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (CompareMem (Info.magic, DEVICE_MAGIC, DEVICE_MAGIC_SIZE) != 0 ||
      Info.is_unlocked > 1 || Info.is_unlock_critical > 1) {
    return EFI_VOLUME_CORRUPTED;
  }

  *Unlocked = Info.is_unlocked;
  return EFI_SUCCESS;
}

STATIC
VOID
SfbDrawMenu (IN CONST SFB_MENU_STATE *Menu,
             IN UINTN                Cursor,
             IN CONST CHAR16         *Title,
             IN CONST CHAR16         *Subtitle)
{
  UINTN  Start;
  UINTN  Index;
  UINTN  Last;

  SfbBeginScreen (Title, Subtitle);

  if (Menu->Count == 0) {
    SfbPanelNote (SfbStr (StrNoEntries));
  }

  Start = SfbWindowStart (Cursor, Menu->Count, SfbVisibleRows ());
  Last = Start + SfbVisibleRows ();
  if (Last > Menu->Count) {
    Last = Menu->Count;
  }

  for (Index = Start; Index < Last; Index++) {
    CONST SFB_BOOT_ENTRY  *Entry = &Menu->Entry[Index];
    CONST CHAR16          *Marker = (Index == Menu->DefaultIndex) ? L"*" : L" ";

    /* Submenu rows get a trailing '>' so it is obvious they open another list
     * rather than launch an image. */
    if (Entry->Kind == SfbEntrySubmenu) {
      CHAR16  Text[SFB_DESC_CHARS + 4];

      UnicodeSPrint (Text, sizeof (Text), L"%s >",
                     SfbBootDisplayName (Entry->Desc, Entry->Path));
      SfbDrawRow ((BOOLEAN)(Index == Cursor), Marker, Text);
    } else {
      CONST CHAR16 *Text = Entry->Desc;
      switch (Entry->Kind) {
      case SfbEntryEfiFile: Text = SfbBootDisplayName (Entry->Desc, Entry->Path); break;
      case SfbEntryFastboot: Text = SfbStr (StrEnterFastboot); break;
      case SfbEntrySelector: Text = SfbStr (StrEfiProgramSelector); break;
      case SfbEntryPowerOff: Text = SfbStr (StrPowerOff); break;
      case SfbEntryRestart:  Text = SfbStr (StrRestart); break;
      case SfbEntryBack:     Text = SfbStr (StrBack); break;
      default: break;
      }
      SfbDrawRow ((BOOLEAN)(Index == Cursor), Marker, Text);
    }
  }

  if (Last < Menu->Count) {
    SfbDrawCountNote (StrMore, (UINT32)(Menu->Count - Last));
  }

  SfbEndScreen (SfbStr (StrKeyNavSelect));
}

/*
 * Run a submenu defined by the ENTRIES file at EntriesPath on Volume. The file
 * is parsed exactly like the root BOOTENTRIES, and may itself contain further
 * '%' submenu rows; Depth bounds the nesting so a chain of files that points at
 * one another cannot recurse without limit. The submenu state is heap-allocated
 * (a single SFB_MENU_STATE is ~17 KB) so deep nesting stays off the call stack.
 *
 * Returns when the user picks the trailing "Back" row, or when the file could
 * not be built at all; the caller then redraws its own menu.
 */
STATIC
VOID
SfbRunSubMenu (IN EFI_HANDLE   Volume,
               IN CONST CHAR16 *EntriesPath,
               IN CONST CHAR16 *Title,
               IN UINTN        Depth)
{
  SFB_MENU_STATE  *Menu = NULL;
  UINTN           Cursor = 0;
  BOOLEAN         Rebuild = TRUE;
  SFB_KEY         Key;
  EFI_STATUS      Status;

  Title = SfbBootDisplayName (Title, EntriesPath);
  Menu = AllocateZeroPool (sizeof (*Menu));
  if (Menu == NULL) {
    return;
  }
  Menu->DefaultIndex = SFB_NO_INDEX;

  while (TRUE) {
    UINTN  Chosen;

    if (Rebuild) {
      SfbFreeMenu (Menu);
      Status = SfbBuildSubMenu (Menu, Volume, EntriesPath);
      if (EFI_ERROR (Status)) {
        SfbReportStatus (Title, Status);
        break;
      }
      Cursor = Menu->DefaultIsPersisted ? Menu->DefaultIndex : 0;
      Rebuild = FALSE;
    }

    SfbDrawMenu (Menu, Cursor, Title, NULL);

    /* Same input model as the root menu: volume keys move, power confirms. */
    Key = SfbWaitForKey (0);

    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, Menu->Count, Key);
      continue;
    }

    if (Menu->Count == 0) {
      continue;
    }

    Chosen = Cursor;
    switch (Menu->Entry[Chosen].Kind) {
    case SfbEntryBack:
      goto done;

    case SfbEntrySubmenu:
      if (Depth >= SFB_MAX_SUBMENU_DEPTH) {
        SfbReportStatus (SfbStr (StrSubmenuTooDeep), EFI_BUFFER_TOO_SMALL);
      } else {
        SfbRunSubMenu (Menu->Entry[Chosen].Volume,
                       Menu->Entry[Chosen].Path,
                       Menu->Entry[Chosen].Desc,
                       Depth + 1);
      }
      /* Media may have changed while the child menu was open. */
      Rebuild = TRUE;
      break;

    case SfbEntryEfiFile:
    default:
      Status = SfbLaunchEntry (&Menu->Entry[Chosen],
                               !SfbIsBootModeEntry (&Menu->Entry[Chosen]), TRUE);
      if (EFI_ERROR (Status) && Status != EFI_ABORTED) {
        SfbReportStatus (SfbStr (StrBootFailed), Status);
      }
      Rebuild = TRUE;
      break;
    }
  }

done:
  SfbFreeMenu (Menu);
  FreePool (Menu);
}

BOOLEAN
SfbRunBootMenu (VOID)
{
  SFB_MENU_STATE  Menu;
  UINTN           Cursor = 0;
  BOOLEAN         Rebuild = TRUE;
  SFB_KEY         Key;
  EFI_STATUS      Status;
  BOOLEAN         Unlocked = FALSE;
  CHAR16          BlState[96];

  ZeroMem (&Menu, sizeof (Menu));
  Menu.DefaultIndex = SFB_NO_INDEX;

  while (TRUE) {
    UINTN  Chosen;

    if (Rebuild) {
      SfbFreeMenu (&Menu);
      SfbBuildMenu (&Menu);
      Status = SfbReadBlState (&Unlocked);
      if (EFI_ERROR (Status)) {
        DEBUG ((EFI_D_ERROR, "SFB: Read BL DeviceInfo failed: %r\n", Status));
        UnicodeSPrint (BlState, sizeof (BlState), SfbStr (StrBlStateUnknown),
                       (UINT64)Status);
      } else {
        UnicodeSPrint (BlState, sizeof (BlState), L"%s",
                       SfbStr (Unlocked ? StrBlStateUnlocked : StrBlStateLocked));
      }
      Cursor = (Menu.DefaultIndex == SFB_NO_INDEX) ? 0 : Menu.DefaultIndex;
      Rebuild = FALSE;
    }

    SfbDrawMenu (&Menu, Cursor, SfbStr (StrBootMenu), BlState);

    /* The menu is purely interactive: it waits for a key indefinitely and
     * never launches anything unattended. */
    Key = SfbWaitForKey (0);

    if (Key == SfbKeyUp || Key == SfbKeyDown) {
      SfbMoveCursor (&Cursor, Menu.Count, Key);
      continue;
    }

    Chosen = Cursor;

    if (Menu.Count == 0) {
      continue;
    }

    switch (Menu.Entry[Chosen].Kind) {
    case SfbEntryFastboot:
      SfbFreeMenu (&Menu);
      return TRUE;

    case SfbEntrySelector:
      SfbRunFileBrowser ();
      /* The browser may have added a custom entry. */
      Rebuild = TRUE;
      break;

    case SfbEntrySubmenu:
      SfbRunSubMenu (Menu.Entry[Chosen].Volume,
                     Menu.Entry[Chosen].Path,
                     Menu.Entry[Chosen].Desc,
                     1);
      /* Media may have changed while the submenu was open. */
      Rebuild = TRUE;
      break;

    case SfbEntryBack:
      /* Only submenus carry a Back row; the root menu never adds one. */
      Rebuild = TRUE;
      break;

    case SfbEntryPowerOff:
      SfbShowActionScreen (SfbStr (StrPoweringOff));
      ShutdownDevice ();
      break;

    case SfbEntryRestart:
      SfbShowActionScreen (SfbStr (StrRestarting));
      RebootDevice (NORMAL_MODE);
      break;

    case SfbEntryEfiFile:
    default:
      Status = SfbLaunchEntry (&Menu.Entry[Chosen], FALSE, TRUE);
      if (EFI_ERROR (Status) && Status != EFI_ABORTED) {
        SfbReportStatus (SfbStr (StrBootFailed), Status);
      }
      /* Media or variables may have changed while the image ran. */
      Rebuild = TRUE;
      break;
    }
  }
}

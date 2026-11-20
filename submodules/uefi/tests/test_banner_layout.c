/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#undef NULL
#include <AndroidToolsUi.h>
#include <MenuScreen.h>

RETURN_STATUS EFIAPI AtUiReleaseScreen (VOID);

static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode = { .Info = &Info };
static EFI_GRAPHICS_OUTPUT_PROTOCOL Graphics = { .Mode = &Mode };
static BOOLEAN UseGraphics;
static MENU_SCREEN *CurrentScreen;
static UINTN DrawTop;
static UINTN ClearCount;
static EFI_STATUS DrawStatus;
static BOOLEAN FooterSeen;
static UINTN StatusRow;
static UINTN StatusTop;
static UINTN FooterRow;
static UINTN MinimumLines;
static CONST CHAR16 StatusText[] = L"BL 真实状态：已解锁";
static CONST CHAR16 FooterText[] = L"音量键移动，电源键确认";
static CONST CHAR16 SubtitleText[] = L"菜单说明";
static UINTN StallCount;
static UINTN ResetCount;
static UINTN ReadCount;
static EFI_STATUS ResetStatus;
static EFI_BOOT_SERVICES BootServices;
static EFI_SYSTEM_TABLE SystemTable;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL Output;
static EFI_SIMPLE_TEXT_OUTPUT_MODE OutputMode;
EFI_BOOT_SERVICES *gBS = &BootServices;
EFI_SYSTEM_TABLE *gST = &SystemTable;
EFI_GUID gEfiGraphicsOutputProtocolGuid;

VOID *EFIAPI AllocatePool (UINTN Size) { return malloc (Size); }
VOID EFIAPI FreePool (VOID *Buffer) { free (Buffer); }
VOID *EFIAPI ZeroMem (VOID *Buffer, UINTN Size) { return memset (Buffer, 0, Size); }

static EFI_STATUS EFIAPI locate_graphics (EFI_GUID *Guid, VOID *Registration, VOID **Interface) {
  (void)Registration;
  assert (Guid == &gEfiGraphicsOutputProtocolGuid);
  if (!UseGraphics) return EFI_NOT_FOUND;
  *Interface = &Graphics;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI handle_graphics (EFI_HANDLE Handle, EFI_GUID *Guid, VOID **Interface) {
  assert (Handle == &Output);
  return locate_graphics (Guid, NULL, Interface);
}

static EFI_STATUS EFIAPI enable_cursor (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible) {
  assert (This == &Output);
  This->Mode->CursorVisible = Visible;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI set_attribute (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute) {
  assert (This == &Output);
  This->Mode->Attribute = (INT32)Attribute;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI query_mode (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Number,
                                    UINTN *Columns, UINTN *Rows) {
  assert (This == &Output && Number == 0);
  *Columns = 80;
  *Rows = 25;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI test_string (EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, CHAR16 *Text) {
  assert (This == &Output && Text != NULL);
  return EFI_SUCCESS;
}

EFI_STATUS __real_MenuScreenOpen (MENU_SCREEN *Screen, UINTN MinLines, UINTN MaxLines);

EFI_STATUS __wrap_MenuScreenOpen (MENU_SCREEN *Screen, UINTN MinLines, UINTN MaxLines) {
  assert (MinLines <= MaxLines);
  MinimumLines = MinLines;
  CurrentScreen = Screen;
  return __real_MenuScreenOpen (Screen, MinLines, MaxLines);
}

EFI_STATUS __wrap_MenuScreenClear (MENU_SCREEN *Screen) {
  assert (Screen == CurrentScreen);
  ClearCount++;
  return EFI_SUCCESS;
}

EFI_STATUS __wrap_MenuScreenLine (MENU_SCREEN *Screen, UINTN Row, MENU_LINE_STYLE Style,
                                 CONST CHAR16 *Text, UINTN Length) {
  assert (Screen == CurrentScreen);
  assert (Text != NULL && Length == MAX_UINTN);
  assert (Row < Screen->Lines);
  if (UseGraphics) {
    assert (Screen->Top + (Row + 1) * 24 * Screen->Scale <= Info.VerticalResolution);
  }
  if (Style == MenuLineTitle) {
    assert (Row == 0);
    DrawTop = Screen->Top;
  } else {
    assert (Style == MenuLineHint);
    if (Text == StatusText) {
      StatusRow = Row;
      StatusTop = Screen->Top;
    } else if (Text == SubtitleText) {
      assert (Row == 1);
    } else {
      assert (Row == Screen->Lines - 1 || Row == Screen->Lines - 3);
      FooterRow = Row;
      FooterSeen = TRUE;
    }
  }
  return DrawStatus;
}

static EFI_STATUS EFIAPI stall_after_frame (UINTN Microseconds) {
  assert (FooterSeen && Microseconds == 2000000);
  StallCount++;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI reset_input (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, BOOLEAN Extended) {
  assert (This == &Input && !Extended);
  ResetCount++;
  return ResetStatus;
}

static EFI_STATUS EFIAPI wait_key (UINTN Count, EFI_EVENT *Events, UINTN *Index) {
  assert (Count == 1 && Events != NULL);
  *Index = 0;
  return EFI_SUCCESS;
}

static EFI_STATUS EFIAPI read_key (EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This, EFI_INPUT_KEY *Key) {
  assert (This == &Input);
  ReadCount++;
  Key->ScanCode = SCAN_NULL;
  Key->UnicodeChar = CHAR_CARRIAGE_RETURN;
  return EFI_SUCCESS;
}

static void check_menu_entry (void) {
  BootServices.Stall = stall_after_frame;
  BootServices.WaitForEvent = wait_key;
  Input.Reset = reset_input;
  Input.ReadKeyStroke = read_key;
  SystemTable.ConIn = &Input;
  DrawStatus = EFI_SUCCESS;
  FooterSeen = FALSE;
  assert (AtUiEnterMenu (L"BL 锁状态工具") == EFI_SUCCESS);
  assert (StallCount == 0 && ResetCount == 0 && ReadCount == 0);
  AtUiBeginScreen (L"BL 锁状态工具", NULL);
  AtUiEndScreen (L"音量键移动，电源键确认");
  assert (StallCount == 0 && FooterSeen);
  assert (AtUiWaitForKey (0) == AtKeySelect);
  assert (StallCount == 1 && ResetCount == 1 && ReadCount == 1);
  assert (AtUiWaitForKey (0) == AtKeySelect);
  assert (StallCount == 1 && ResetCount == 1 && ReadCount == 2);

  ResetStatus = EFI_DEVICE_ERROR;
  FooterSeen = FALSE;
  assert (AtUiEnterMenu (L"ARB 工具") == EFI_SUCCESS);
  AtUiBeginScreen (L"ARB 工具", NULL);
  AtUiEndScreen (L"音量键移动，电源键确认");
  assert (AtUiWaitForKey (0) == AtKeyTimeout);
  assert (AtUiScreenStatus () == EFI_DEVICE_ERROR && ReadCount == 2);
  AtUiReleaseScreen ();
}

static void check_layout (void) {
  UINTN MenuTop;
  UINTN BannerTop;

  ClearCount = 0;
  DrawStatus = EFI_SUCCESS;
  AtUiShowBanner (L"正在启动 Android 系统", FALSE);
  MenuTop = CurrentScreen->Top;
  assert (AtUiScreenStatus () == EFI_SUCCESS);
  assert (ClearCount == 0);
  if (UseGraphics) {
    assert (DrawTop == Info.VerticalResolution / 16);
    assert (MenuTop + CurrentScreen->Lines * 24 * CurrentScreen->Scale <=
            Info.VerticalResolution - Info.VerticalResolution / 32);
  } else {
    assert (DrawTop == 1);
  }
  BannerTop = DrawTop;

  AtUiShowBanner (L"正在启动 Android 系统", TRUE);
  assert (AtUiScreenStatus () == EFI_SUCCESS);
  assert (ClearCount == 1 && DrawTop == BannerTop);
  assert (CurrentScreen->Top == MenuTop);

  AtUiBeginScreen (L"菜单", NULL);
  assert (AtUiScreenStatus () == EFI_SUCCESS);
  assert (ClearCount == 2 && DrawTop == MenuTop);

  DrawStatus = EFI_DEVICE_ERROR;
  AtUiShowBanner (L"正在启动 Android 系统", FALSE);
  assert (AtUiScreenStatus () == EFI_DEVICE_ERROR);
  assert (ClearCount == 2 && CurrentScreen->Top == MenuTop);
  AtUiReleaseScreen ();
}

static void check_status_layout (void) {
  UINTN MenuTop;

  DrawStatus = EFI_SUCCESS;
  StatusRow = FooterRow = MAX_UINTN;
  AtUiSetStatusLine (StatusText);
  AtUiBeginScreen (L"菜单", NULL);
  MenuTop = CurrentScreen->Top;
  AtUiEndScreen (FooterText);
  assert (AtUiScreenStatus () == EFI_SUCCESS && MinimumLines == 9);
  assert (StatusRow == CurrentScreen->Lines - 2 && StatusTop == MenuTop);
  assert (FooterRow < StatusRow && StatusRow + 1 < CurrentScreen->Lines);
  assert (AtUiVisibleRows () > 0);

  StatusRow = MAX_UINTN;
  AtUiShowBanner (L"正在启动 Android", FALSE);
  assert (StatusTop == MenuTop);
  assert (StatusRow == CurrentScreen->Lines - 2);
  assert (CurrentScreen->Top == MenuTop);

  AtUiSetStatusLine (NULL);
  StatusRow = MAX_UINTN;
  AtUiBeginScreen (L"菜单", NULL);
  AtUiEndScreen (FooterText);
  assert (MinimumLines == 7 && StatusRow == MAX_UINTN);
  assert (FooterRow == CurrentScreen->Lines - 1);

  UINTN Rows = AtUiVisibleRows ();
  assert (Rows > 0);
  if (UseGraphics && Info.VerticalResolution >= 1920) {
    assert (Rows > 12);
  }
  for (UINTN Cursor = 0; Cursor < 7; Cursor++) {
    UINTN Start = AtUiWindowStart (Cursor, 7, Rows);
    assert (Start <= Cursor && Cursor < Start + Rows);
    if (Rows >= 7) assert (Start == 0);
  }
  AtUiBeginScreen (L"菜单", SubtitleText);
  assert (AtUiVisibleRows () + 1 == Rows);
  AtUiReleaseScreen ();
}

int main (void) {
  static const UINT32 Resolutions[][2] = {
    { 1440, 3168 }, { 1080, 2400 }, { 1080, 1920 },
    { 2400, 1080 }, { 240, 320 }, { 320, 240 }
  };

  BootServices.HandleProtocol = handle_graphics;
  BootServices.LocateProtocol = locate_graphics;
  Output.Mode = &OutputMode;
  Output.EnableCursor = enable_cursor;
  Output.SetAttribute = set_attribute;
  Output.QueryMode = query_mode;
  Output.TestString = test_string;
  SystemTable.ConOut = &Output;
  SystemTable.ConsoleOutHandle = &Output;

  UseGraphics = TRUE;
  for (UINTN Index = 0; Index < sizeof (Resolutions) / sizeof (Resolutions[0]); Index++) {
    Info.HorizontalResolution = Resolutions[Index][0];
    Info.VerticalResolution = Resolutions[Index][1];
    check_layout ();
    check_status_layout ();
  }
  UseGraphics = FALSE;
  check_layout ();
  check_status_layout ();
  check_menu_entry ();
  puts ("真实屏幕布局、大小屏行数与滚动、启动提示位置和菜单输入测试通过");
  return 0;
}

/* SPDX-License-Identifier: BSD-3-Clause */
#include <assert.h>
#include <stdio.h>
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
static UINTN StallCount;
static UINTN ResetCount;
static UINTN ReadCount;
static EFI_STATUS ResetStatus;
static EFI_BOOT_SERVICES BootServices;
static EFI_SYSTEM_TABLE SystemTable;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL Input;
EFI_BOOT_SERVICES *gBS = &BootServices;
EFI_SYSTEM_TABLE *gST = &SystemTable;

EFI_STATUS MenuScreenOpen (MENU_SCREEN *Screen, UINTN MinLines, UINTN MaxLines) {
  assert (MinLines <= MaxLines);
  MinimumLines = MinLines;
  Screen->Graphics = UseGraphics ? &Graphics : NULL;
  Screen->GraphicsMode = Mode.Mode;
  Screen->Lines = MaxLines;
  Screen->Top = UseGraphics ? Info.VerticalResolution / 4 : 8;
  CurrentScreen = Screen;
  return EFI_SUCCESS;
}

VOID MenuScreenClose (MENU_SCREEN *Screen) {
  memset (Screen, 0, sizeof (*Screen));
}

EFI_STATUS MenuScreenClear (MENU_SCREEN *Screen) {
  assert (Screen == CurrentScreen);
  ClearCount++;
  return EFI_SUCCESS;
}

EFI_STATUS MenuScreenLine (MENU_SCREEN *Screen, UINTN Row, MENU_LINE_STYLE Style,
                          CONST CHAR16 *Text, UINTN Length) {
  assert (Screen == CurrentScreen);
  assert (Text != NULL && Length == MAX_UINTN);
  if (Style == MenuLineTitle) {
    assert (Row == 0);
    DrawTop = Screen->Top;
  } else {
    assert (Style == MenuLineHint);
    if (Text == StatusText) {
      StatusRow = Row;
      StatusTop = Screen->Top;
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
  UINTN MenuTop = UseGraphics ? Info.VerticalResolution / 4 : 8;
  UINTN BannerTop;

  ClearCount = 0;
  DrawStatus = EFI_SUCCESS;
  AtUiShowBanner (L"正在启动 Android 系统", FALSE);
  assert (AtUiScreenStatus () == EFI_SUCCESS);
  assert (ClearCount == 0);
  assert (DrawTop > 0 && DrawTop < MenuTop);
  assert (CurrentScreen->Top == MenuTop);
  if (UseGraphics) {
    assert (DrawTop < Info.VerticalResolution / 8);
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
  UINTN MenuTop = UseGraphics ? Info.VerticalResolution / 4 : 8;

  DrawStatus = EFI_SUCCESS;
  StatusRow = FooterRow = MAX_UINTN;
  AtUiSetStatusLine (StatusText);
  AtUiBeginScreen (L"菜单", NULL);
  AtUiEndScreen (FooterText);
  assert (AtUiScreenStatus () == EFI_SUCCESS && MinimumLines == 9);
  assert (StatusRow == CurrentScreen->Lines - 2 && StatusTop == MenuTop);
  assert (FooterRow < StatusRow && StatusRow + 1 < CurrentScreen->Lines);
  assert (AtUiVisibleRows () > 0);

  StatusRow = MAX_UINTN;
  AtUiShowBanner (L"正在启动 Android", FALSE);
  assert (DrawTop < StatusTop && StatusTop == MenuTop);
  assert (StatusRow == CurrentScreen->Lines - 2);
  assert (CurrentScreen->Top == MenuTop);

  AtUiSetStatusLine (NULL);
  StatusRow = MAX_UINTN;
  AtUiBeginScreen (L"菜单", NULL);
  AtUiEndScreen (FooterText);
  assert (MinimumLines == 7 && StatusRow == MAX_UINTN);
  assert (FooterRow == CurrentScreen->Lines - 1);
  AtUiReleaseScreen ();
}

int main (void) {
  static const UINT32 Heights[] = { 3168, 2400, 1080, 320 };

  UseGraphics = TRUE;
  for (UINTN Index = 0; Index < sizeof (Heights) / sizeof (Heights[0]); Index++) {
    Info.VerticalResolution = Heights[Index];
    check_layout ();
    check_status_layout ();
  }
  UseGraphics = FALSE;
  check_layout ();
  check_status_layout ();
  check_menu_entry ();
  puts ("启动提示、BL 状态栏留白、完整首屏后清理按键及输入失败处理测试通过");
  return 0;
}

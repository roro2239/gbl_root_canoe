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

EFI_STATUS MenuScreenOpen (MENU_SCREEN *Screen, UINTN MinLines, UINTN MaxLines) {
  assert (MinLines <= MaxLines);
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
  assert (Row == 0 && Style == MenuLineTitle);
  assert (Text != NULL && Length == MAX_UINTN);
  DrawTop = Screen->Top;
  return DrawStatus;
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

int main (void) {
  static const UINT32 Heights[] = { 3168, 2400, 1080, 320 };

  UseGraphics = TRUE;
  for (UINTN Index = 0; Index < sizeof (Heights) / sizeof (Heights[0]); Index++) {
    Info.VerticalResolution = Heights[Index];
    check_layout ();
  }
  UseGraphics = FALSE;
  check_layout ();
  puts ("启动提示顶部布局、菜单坐标恢复及绘制失败状态测试通过");
  return 0;
}

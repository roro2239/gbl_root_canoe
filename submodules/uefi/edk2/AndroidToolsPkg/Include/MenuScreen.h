/** @file
  Scalable bitmap output for firmware menus.
  SPDX-License-Identifier: BSD-3-Clause
**/

#ifndef MENU_SCREEN_H_
#define MENU_SCREEN_H_

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

typedef enum {
  MenuLineNormal,
  MenuLineSelected,
  MenuLineTitle,
  MenuLineHint
} MENU_LINE_STYLE;

typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Graphics;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *RowBuffer;
  UINT32                        GraphicsMode;
  UINTN                         Scale;
  UINTN                         Width;
  UINTN                         Lines;
  UINTN                         Left;
  UINTN                         Top;
  UINTN                         OriginalAttribute;
  BOOLEAN                       OriginalCursor;
} MENU_SCREEN;

EFI_STATUS
MenuScreenOpen (OUT MENU_SCREEN *Screen, IN UINTN MinLines, IN UINTN MaxLines);

VOID
MenuScreenClose (IN OUT MENU_SCREEN *Screen);

EFI_STATUS
MenuScreenClear (IN MENU_SCREEN *Screen);

/* 一行在正常字号下能容纳的字符数，供多行提示换行。 */
UINTN
MenuScreenFit (IN CONST MENU_SCREEN *Screen, IN CONST CHAR16 *Text);

EFI_STATUS
MenuScreenLine (IN MENU_SCREEN *Screen, IN UINTN Row, IN MENU_LINE_STYLE Style,
                IN CONST CHAR16 *Text, IN UINTN Length);

#endif

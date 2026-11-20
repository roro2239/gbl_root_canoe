/** @file
  Scalable bitmap output for firmware menus.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include "MenuScreen.h"
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define GLYPH_HEIGHT  16
#define LINE_HEIGHT   24
#define TEXT_INSET    4

typedef struct {
  CHAR16 Code;
  UINT8  Width;
  UINT16 Bits[GLYPH_HEIGHT];
} MENU_GLYPH;

#include "MenuFont.h"

STATIC CONST MENU_GLYPH *
FindGlyph (IN CHAR16 Code)
{
  UINTN Low = 0;
  UINTN High = ARRAY_SIZE (mMenuFont.Glyphs);
  UINTN Middle;

  while (Low < High) {
    Middle = Low + (High - Low) / 2;
    if (mMenuFont.Glyphs[Middle].Code == Code) {
      return &mMenuFont.Glyphs[Middle];
    }
    if (mMenuFont.Glyphs[Middle].Code < Code) {
      Low = Middle + 1;
    } else {
      High = Middle;
    }
  }
  /* 字表末尾固定为 U+FFFD。外部文件名可能含有字表以外的字符。 */
  return &mMenuFont.Glyphs[ARRAY_SIZE (mMenuFont.Glyphs) - 1];
}

EFI_STATUS
MenuScreenOpen (OUT MENU_SCREEN *Screen, IN UINTN MinLines, IN UINTN MaxLines)
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out = gST->ConOut;
  EFI_GRAPHICS_OUTPUT_PROTOCOL    *Graphics;
  EFI_STATUS                      Status;
  UINTN                           Width;
  UINTN                           Height;
  UINTN                           AvailableHeight;
  UINTN                           Scale;

  ZeroMem (Screen, sizeof (*Screen));
  Screen->OriginalAttribute = (UINTN)Out->Mode->Attribute;
  Screen->OriginalCursor = Out->Mode->CursorVisible;

  Status = gBS->HandleProtocol (gST->ConsoleOutHandle, &gEfiGraphicsOutputProtocolGuid,
                                (VOID **)&Graphics);
  if (EFI_ERROR (Status)) {
    Status = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Graphics);
  }
  if (!EFI_ERROR (Status) && Graphics->Mode != NULL && Graphics->Mode->Info != NULL) {
    Width = Graphics->Mode->Info->HorizontalResolution;
    Height = Graphics->Mode->Info->VerticalResolution;
    AvailableHeight = Height - 2 * (Height / 32);
    Scale = MAX (1, MIN (Width, Height) / 240);
    Scale = MIN (Scale, AvailableHeight / (MinLines * LINE_HEIGHT));
    Screen->Left = Width / 24;
    Screen->Width = Width - 2 * Screen->Left;
    if (Scale != 0 && Screen->Width >= (16 + 2 * TEXT_INSET) * Scale) {
      Screen->RowBuffer = AllocatePool (Screen->Width * LINE_HEIGHT * Scale *
                                        sizeof (*Screen->RowBuffer));
      if (Screen->RowBuffer != NULL) {
        Screen->Graphics = Graphics;
        Screen->GraphicsMode = Graphics->Mode->Mode;
        Screen->Scale = Scale;
        if (Height > Width) {
          AvailableHeight = MAX (Height / 2, MinLines * LINE_HEIGHT * Scale);
        }
        Screen->Lines = MIN (MaxLines, AvailableHeight / (LINE_HEIGHT * Scale));
        Screen->Top = (Height - Screen->Lines * LINE_HEIGHT * Scale) / 2;
        Out->EnableCursor (Out, FALSE);
        return EFI_SUCCESS;
      }
      DEBUG ((DEBUG_WARN, "Menu: graphics row allocation failed\n"));
    }
  }

  Status = Out->QueryMode (Out, (UINTN)Out->Mode->Mode, &Width, &Height);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (Height < MinLines || Width < 8 || Out->TestString (Out, L"菜单") != EFI_SUCCESS) {
    DEBUG ((DEBUG_ERROR, "Menu: no usable graphics or Chinese text console\n"));
    return EFI_UNSUPPORTED;
  }
  Screen->Lines = MIN (MaxLines, Height);
  Screen->Top = (Height - Screen->Lines) / 2;
  /* 留出选择指示符和最末一列，避免控制台自动换行。 */
  Screen->Width = MIN (80, Width - 3) * 8;
  Screen->Left = (Width - Screen->Width / 8 - 2) / 2;
  Screen->Scale = 1;
  Out->EnableCursor (Out, FALSE);
  return EFI_SUCCESS;
}

VOID
MenuScreenClose (IN OUT MENU_SCREEN *Screen)
{
  if (Screen->RowBuffer != NULL) {
    FreePool (Screen->RowBuffer);
  }
  gST->ConOut->SetAttribute (gST->ConOut, Screen->OriginalAttribute);
  gST->ConOut->EnableCursor (gST->ConOut, Screen->OriginalCursor);
  ZeroMem (Screen, sizeof (*Screen));
}

EFI_STATUS
MenuScreenClear (IN MENU_SCREEN *Screen)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black = { 0, 0, 0, 0 };
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *Graphics = Screen->Graphics;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out = gST->ConOut;

  if (Graphics == NULL) {
    Out->SetAttribute (Out, EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK));
    return Out->ClearScreen (Out);
  }
  return Graphics->Blt (Graphics, &Black, EfiBltVideoFill, 0, 0, 0, 0,
                        Graphics->Mode->Info->HorizontalResolution,
                        Graphics->Mode->Info->VerticalResolution, 0);
}

STATIC UINTN
FitChars (IN CONST CHAR16 *Text, IN UINTN Length, IN UINTN Width, OUT UINTN *Used)
{
  UINTN Index;
  UINTN GlyphWidth;

  *Used = 0;
  for (Index = 0; Index < Length && Text[Index] != 0; Index++) {
    GlyphWidth = FindGlyph (Text[Index])->Width;
    if (GlyphWidth > Width - *Used) {
      break;
    }
    *Used += GlyphWidth;
  }
  return Index;
}

UINTN
MenuScreenFit (IN CONST MENU_SCREEN *Screen, IN CONST CHAR16 *Text)
{
  UINTN Width = Screen->Width / Screen->Scale;
  UINTN Used;

  if (Screen->Graphics != NULL) {
    Width -= 2 * TEXT_INSET;
  }
  return FitChars (Text, MAX_UINTN, Width, &Used);
}

EFI_STATUS
MenuScreenLine (IN MENU_SCREEN *Screen, IN UINTN Row, IN MENU_LINE_STYLE Style,
                IN CONST CHAR16 *Text, IN UINTN Length)
{
  STATIC CONST UINTN Attributes[] = {
    EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK), EFI_TEXT_ATTR (EFI_BLACK, EFI_LIGHTGRAY),
    EFI_TEXT_ATTR (EFI_WHITE, EFI_BLACK), EFI_TEXT_ATTR (EFI_LIGHTGRAY, EFI_BLACK)
  };
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Out = gST->ConOut;
  UINTN                           Width = Screen->Width;
  UINTN                           Inset = (Screen->Graphics != NULL) ? TEXT_INSET * Screen->Scale : 0;
  UINTN                           Available = Width - 2 * Inset;
  UINTN                           Scale = Screen->Scale;
  UINTN                           Height = LINE_HEIGHT * Scale;
  UINTN                           Used;
  UINTN                           X;
  UINTN                           Y;
  UINTN                           Top;
  UINTN                           Index;
  UINTN                           Bit;
  UINTN                           Copy;
  UINT32                          Paper;
  UINT32                          Ink;
  BOOLEAN                         Clipped;
  CHAR16                          *Line;
  EFI_STATUS                      Status;
  CONST MENU_GLYPH                *Glyph;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   *Scan;

  if (Row >= Screen->Lines) {
    return EFI_BAD_BUFFER_SIZE;
  }
  Length = FitChars (Text, Length, MAX_UINTN, &Used);
  if (Used > 0) {
    Scale = MAX (1, MIN (Scale, Available / Used));
  }
  Clipped = (Used > Available / Scale);
  if (Clipped) {
    UINTN EllipsisWidth = FindGlyph (L'…')->Width;
    Length = FitChars (Text, Length, Available / Scale - EllipsisWidth, &Used);
    Used += EllipsisWidth;
  }

  if (Screen->Graphics == NULL) {
    Line = AllocatePool ((Length + 2) * sizeof (*Line));
    if (Line == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    CopyMem (Line, Text, Length * sizeof (*Line));
    if (Clipped) {
      Line[Length++] = L'…';
    }
    Line[Length] = 0;
    Status = Out->SetAttribute (Out, Attributes[Style]);
    if (!EFI_ERROR (Status) || Status == EFI_UNSUPPORTED) {
      Status = Out->SetCursorPosition (Out, Screen->Left, Screen->Top + Row);
    }
    if (!EFI_ERROR (Status)) {
      Status = Out->OutputString (Out, (Style == MenuLineSelected) ? L"> " : L"  ");
    }
    if (!EFI_ERROR (Status)) {
      Status = Out->OutputString (Out, Line);
    }
    FreePool (Line);
    return (Status == EFI_WARN_UNKNOWN_GLYPH) ? EFI_UNSUPPORTED : Status;
  }

  Paper = (Style == MenuLineSelected) ? 0x00DEE9FF : 0;
  Ink = (Style == MenuLineSelected) ? 0x00142135 :
        (Style == MenuLineHint) ? 0x00B0BDCA : 0x00FFFFFF;
  SetMem32 (Screen->RowBuffer, Width * Height * sizeof (*Scan), Paper);
  X = (Style == MenuLineTitle || Style == MenuLineHint) ? (Width - Used * Scale) / 2 : Inset;
  Top = (Height - GLYPH_HEIGHT * Scale) / 2;
  for (Index = 0; Index < Length + (Clipped ? 1U : 0U); Index++) {
    Glyph = FindGlyph ((Index == Length) ? L'…' : Text[Index]);
    for (Y = 0; Y < GLYPH_HEIGHT; Y++) {
      Scan = Screen->RowBuffer + (Top + Y * Scale) * Width + X;
      for (Bit = 0; Bit < Glyph->Width; Bit++) {
        if ((Glyph->Bits[Y] & (0x8000U >> Bit)) != 0) {
          SetMem32 (Scan + Bit * Scale, Scale * sizeof (*Scan), Ink);
        }
      }
    }
    X += Glyph->Width * Scale;
  }
  for (Y = 0; Y < GLYPH_HEIGHT; Y++) {
    Scan = Screen->RowBuffer + (Top + Y * Scale) * Width;
    for (Copy = 1; Copy < Scale; Copy++) {
      CopyMem (Scan + Copy * Width, Scan, Width * sizeof (*Scan));
    }
  }
  return Screen->Graphics->Blt (Screen->Graphics, Screen->RowBuffer, EfiBltBufferToVideo,
                                0, 0, Screen->Left, Screen->Top + Row * Height,
                                Width, Height, 0);
}

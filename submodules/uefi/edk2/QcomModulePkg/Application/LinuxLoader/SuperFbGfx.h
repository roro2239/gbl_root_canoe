/*
 * Minimal Graphics Output (GOP) renderer for the BDS boot menu.
 *
 * Draws directly to the frame buffer through GOP->Blt with the embedded
 * 32px-tall anti-aliased bitmap font (SuperFbFontData.c).  When the platform
 * has no GOP the UI falls back to the text console, so every drawing function
 * here is only reached through SfbGfxActive ().
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_GFX_H__
#define __SUPER_FB_GFX_H__

#include <Uefi.h>
#include "SuperFbFont.h"

/* 0x00RRGGBB 黑白配色；选中行反色显示。 */
#define SFB_COLOR_BG        0x00000000
#define SFB_COLOR_PANEL     0x00000000
#define SFB_COLOR_ACCENT    0x00FFFFFF
#define SFB_COLOR_ACCENT_D  0x00FFFFFF
#define SFB_COLOR_TEXT      0x00FFFFFF
#define SFB_COLOR_SEL_BG    0x00FFFFFF
#define SFB_COLOR_SEL_FG    0x00000000
#define SFB_COLOR_ERR       0x00FFFFFF

/*
 * Locate the graphics output protocol.  Returns FALSE when no usable GOP is
 * present so callers can keep using the text console.
 */
BOOLEAN
SfbGfxInit (VOID);

BOOLEAN
SfbGfxActive (VOID);

VOID
SfbGfxGetScreen (OUT UINT32 *Width, OUT UINT32 *Height);

/* Paint the entire screen with Color. */
VOID
SfbGfxClear (IN UINT32 Color);

/* Paint a rectangle. */
VOID
SfbGfxFillRect (IN UINT32 X,
                IN UINT32 Y,
                IN UINT32 W,
                IN UINT32 H,
                IN UINT32 Color);

/* Paint a horizontal bar of Thick pixels from X0 to X1 inclusive. */
VOID
SfbGfxHLine (IN UINT32 Y,
             IN UINT32 X0,
             IN UINT32 X1,
             IN UINT32 Thick,
             IN UINT32 Color);

/*
 * Draw a string with the embedded bitmap font starting at (X, Y).
 * Characters missing from the font table are drawn as a hollow placeholder
 * box.  The string is clipped at the right edge of the screen.
 */
VOID
SfbGfxDrawText (IN CONST CHAR16 *Text,
                IN UINT32       X,
                IN UINT32       Y,
                IN UINT32       Fg,
                IN UINT32       Bg);

UINT32
SfbGfxTextWidth (IN CONST CHAR16 *Text);

#endif /* __SUPER_FB_GFX_H__ */

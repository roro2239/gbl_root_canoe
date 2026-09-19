/*
 * Bilingual (English / Simplified Chinese) strings for the super-fastboot
 * boot menu.
 *
 * Static UI and the bundled Android mode entry names are translated.
 * Other boot entry names, file names and volume labels remain verbatim.
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef __SUPER_FB_LANG_H__
#define __SUPER_FB_LANG_H__

#include <Uefi.h>

typedef enum {
  StrBootMenu,
  StrEnteringBootMenu,
  StrNoEntries,
  StrMore,
  StrKeyNavSelect,
  StrKeyNavOpen,
  StrEnterFastboot,
  StrFastbootMode,
  StrBooting,
  StrPoweringOff,
  StrRestarting,
  StrPowerOff,
  StrRestart,
  StrBack,
  StrLoad,
  StrBootTemporary,
  StrAddToBootMenu,
  StrEfiProgramSelector,
  StrEfiDriver,
  StrEfiApplication,
  StrPressPower,
  StrBootFailed,
  StrDriverLoadFailed,
  StrDriverLoaded,
  StrCouldNotSaveEntry,
  StrAddedToBootMenu,
  StrOutOfMemory,
  StrCannotReadDir,
  StrCannotAddressFile,
  StrSubmenuTooDeep,
  StrNotEfiApp,
  StrNoFatVolumes,
  StrVolumeFmt,
  StrDirTruncated,
  StrVolumeLabelFmt,
  StrChooseVolume,
  StrBootAndroidFakeLocked,
  StrBootAndroidRealState,
  StrBootAndroidBackup,
  StrAndroidTools,
  StrRebootTools,
  StrBlTools,
  StrArbTools,
  StrBootModes,
  StrConfirmBootMode,
  StrConfirmThree,
  StrConfirmOrCancel,
  StrCount
} SFB_STR_ID;

CONST CHAR16 *
SfbStr (IN UINTN Id);

#endif /* __SUPER_FB_LANG_H__ */

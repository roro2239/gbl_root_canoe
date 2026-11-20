/** @file
  Display names for the boot menu and its bundled entries.
  SPDX-License-Identifier: BSD-3-Clause
**/

#include "SuperFbMenu.h"
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

CONST CHAR16 gSfbRootTitle[] = L"假回锁";

STATIC CONST struct {
  CONST CHAR16 *Name;
  CONST CHAR16 *MenuName;
  CONST CHAR16 *Path;
  CONST CHAR16 *Caption;
} mBundledEntries[] = {
  { L"Android", L"Boot Modes\\Android", L"\\efisp\\boot.efi", L"启动 Android（假回锁）" },
  { L"Android (Real State)", L"Boot Modes\\Android (Real State)", L"\\efisp\\boot_normal.efi", L"启动 Android（真实状态）" },
  { L"Android backup", L"Boot Modes\\Android backup", L"\\efisp\\boot_backup.efi", L"启动 Android（备份）" },
  { L"Android Tools\\Reboot Tools", NULL, L"\\efisp\\tools\\RebootTools.efi", L"重启工具" },
  { L"Android Tools\\BL Tools", NULL, L"\\efisp\\tools\\BLTools.efi", L"BL 锁状态工具" },
  { L"Android Tools\\ARB Tools", NULL, L"\\efisp\\tools\\ArbTools.efi", L"ARB 工具" }
};

STATIC UINTN
SfbFindBundledEntry (IN CONST SFB_ENTRY_SPEC *Spec)
{
  UINTN Index;

  for (Index = 0; Index < ARRAY_SIZE (mBundledEntries); Index++) {
    if ((StrCmp (Spec->Name, mBundledEntries[Index].Name) == 0 ||
         (mBundledEntries[Index].MenuName != NULL &&
          StrCmp (Spec->Name, mBundledEntries[Index].MenuName) == 0)) &&
        SfbEntryPathEqual (Spec->Path, mBundledEntries[Index].Path)) {
      return Index;
    }
  }
  return SFB_NO_INDEX;
}

SFB_BOOT_MODE
SfbGetBootMode (IN CONST SFB_ENTRY_SPEC *Spec)
{
  UINTN Index;

  /* 按路径识别，使改名入口和文件选择器也经过同一启动确认。 */
  for (Index = 0; Index < 3; Index++) {
    if (SfbEntryPathEqual (Spec->Path, mBundledEntries[Index].Path)) {
      return (SFB_BOOT_MODE)(SfbBootModeFakeLocked + Index);
    }
  }
  return SfbBootModeOther;
}

BOOLEAN
SfbIsAndroidBootEntry (IN CONST SFB_ENTRY_SPEC *Spec)
{
  return (BOOLEAN)(SfbGetBootMode (Spec) != SfbBootModeOther);
}

UINTN
SfbDefaultBootModeIndex (IN CONST SFB_ENTRY_LIST *List)
{
  UINTN Index;
  SFB_BOOT_MODE Mode;

  for (Index = 0; Index < List->Count; Index++) {
    Mode = SfbGetBootMode (&List->Entry[Index].Spec);
    if (Mode == SfbBootModeFakeLocked || Mode == SfbBootModeRealState) {
      return Index;
    }
  }
  return SFB_NO_INDEX;
}

CONST CHAR16 *
SfbEntryMenuName (IN CONST SFB_ENTRY_SPEC *Spec)
{
  UINTN Index = SfbFindBundledEntry (Spec);

  /* 旧配置仍保留原记录和启动顺序，只在菜单中归入对应分类。 */
  if (Index != SFB_NO_INDEX && mBundledEntries[Index].MenuName != NULL) {
    return mBundledEntries[Index].MenuName;
  }
  return Spec->Name;
}

CONST CHAR16 *
SfbEntryDisplayName (IN CONST SFB_ENTRY_SPEC *Spec, IN CONST CHAR16 *Fallback)
{
  STATIC CHAR16 VolumeCaption[SFB_DESC_CHARS];
  CHAR16        DefaultName[SFB_DESC_CHARS];
  UINTN         Index = SfbFindBundledEntry (Spec);

  if (Index != SFB_NO_INDEX) {
    return mBundledEntries[Index].Caption;
  }
  if (Fallback != NULL && StrnCmp (Spec->Name, L"Volume (", 8) == 0 &&
      SfbEntryPathEqual (Spec->Path, SFB_BOOT_FILE_PATH)) {
    UnicodeSPrint (DefaultName, sizeof (DefaultName), L"Volume (%u,%u)", Spec->Disk, Spec->Part);
    if (StrCmp (Spec->Name, DefaultName) == 0) {
      UnicodeSPrint (VolumeCaption, sizeof (VolumeCaption), L"存储卷（%u，%u）", Spec->Disk, Spec->Part);
      return VolumeCaption;
    }
  }
  return Fallback;
}

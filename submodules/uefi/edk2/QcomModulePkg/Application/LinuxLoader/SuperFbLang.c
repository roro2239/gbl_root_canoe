/*
 * String table backing SuperFbLang.h.  Keep the array order in sync with
 * SFB_STR_ID.  Every L"..." literal in this file is also the source set for
 * the embedded bitmap font (tools/gen_sfb_font/gen_sfb_font.ps1).
 *
 * Copyright (c) 2026, contributors to the canoe ABL tree.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "SuperFbLang.h"
#include "SuperFbGfx.h"

typedef struct {
  CONST CHAR16 *En;
  CONST CHAR16 *Zh;
} SFB_LANG_STRING;

STATIC CONST SFB_LANG_STRING mSfbStrings[StrCount] = {
  { L"Boot Menu", L"启动菜单" },
  { L"Entering Boot Menu", L"正在进入启动菜单" },
  { L"No boot entries found.", L"未找到启动项。" },
  { L"... %u more", L"... 还有 %u 项" },
  { L"Vol Up/Down: move   Power: select", L"音量上/下：移动  电源键：选择" },
  { L"Vol Up/Down: move   Power: open", L"音量上/下：移动  电源键：打开" },
  { L"Enter Fastboot", L"进入 Fastboot" },
  { L"FASTBOOT MODE", L"FASTBOOT 模式" },
  { L"Booting %s", L"正在启动 %s" },
  { L"Powering off...", L"正在关机..." },
  { L"Restarting...", L"正在重启..." },
  { L"Power Off", L"关机" },
  { L"Restart", L"重启" },
  { L"Back", L"返回" },
  { L"Load", L"加载" },
  { L"Boot (temporary)", L"启动（临时）" },
  { L"Add to BootMenu", L"添加到启动菜单" },
  { L"EFI Program Selector", L"EFI 程序选择器" },
  { L"EFI Driver", L"EFI 驱动" },
  { L"EFI Application", L"EFI 应用程序" },
  { L"Press power to continue.", L"按电源键继续。" },
  { L"Boot failed", L"启动失败" },
  { L"Driver load failed", L"驱动加载失败" },
  { L"Driver loaded", L"驱动已加载" },
  { L"Could not save entry", L"无法保存启动项" },
  { L"Added to boot menu", L"已添加到启动菜单" },
  { L"Out of memory", L"内存不足" },
  { L"Cannot read directory", L"无法读取目录" },
  { L"Cannot address that file", L"无法定位该文件" },
  { L"Submenu too deep", L"子菜单层级过深" },
  { L"Not an EFI application", L"不是 EFI 应用程序" },
  { L"No boot volumes found", L"未找到启动卷" },
  { L"Volume %u", L"卷 %u" },
  { L"(directory has more than %u entries; rest not shown)", L"（目录超过 %u 项，其余未显示）" },
  { L"Volume %u: %s", L"卷 %u：%s" },
  { L"Choose a volume to browse.", L"选择要浏览的卷。" },
  { L"Android (Fake Lock)", L"启动安卓（假回锁）" },
  { L"Android (Real State)", L"启动安卓（真实状态）" },
  { L"Android backup", L"启动安卓（备份）" },
  { L"Android Tools", L"安卓工具" },
  { L"Reboot Tools", L"重启工具" },
  { L"BL Tools", L"BL 锁状态工具" },
  { L"ARB Tools", L"防回滚工具" },
  { L"Boot Modes", L"启动方式" },
  { L"Confirm boot mode", L"确认启动方式" },
  { L"Confirm %u/3", L"确认 %u/3" },
  { L"Power: confirm   Volume: cancel", L"电源键：确认  音量键：取消" },
  { L"Warning: switching may prevent data decryption.", L"警告：切换启动状态可能导致" },
  { L"A data wipe may be required.", L"数据无法解密，需要格式化" },
  { L"Back up important data first.", L"并清除数据。请先备份重要数据。" },
};

CONST CHAR16 *
SfbStr (IN UINTN Id)
{
  if (Id >= StrCount) {
    return L"?";
  }
  /* 无法使用图形协议时保留英文控制台，具体原因由绘制模块和界面报告。 */
  return SfbGfxInit () ? mSfbStrings[Id].Zh : mSfbStrings[Id].En;
}

/* 工具与 Fastboot 的显示文案；仅翻译精确匹配的界面字符串。 */
#include "AndroidToolsUi.h"
#include <Library/BaseLib.h>
#include "../../../QcomModulePkg/Application/LinuxLoader/SuperFbGfx.h"

STATIC CONST struct { CONST CHAR16 *En; CONST CHAR16 *Zh; } mStrings[] = {
  { L"FASTBOOT MODE", L"FASTBOOT 模式" },
  { L"Power Off", L"关机" },
  { L"Restart", L"重启" },
  { L"Return to Main Menu", L"返回主菜单" },
  { L"Busy. Try returning after transfer completes.", L"正在传输，请完成后再返回" },
  { L"Powering off...", L"正在关机..." },
  { L"Restarting...", L"正在重启..." },
  { L"Vol Up/Down: move   Power: select", L"音量键：移动  电源键：选择" },
  { L"Vol+/- move, power select", L"音量键：移动  电源键：选择" },
  { L"Vol+/- scroll, power to return", L"音量键：滚动  电源键：返回" },
  { L"Back", L"返回" },
  { L"Reboot Tools", L"重启工具" },
  { L"BL Tools", L"BL 锁状态工具" },
  { L"ARB Tools", L"防回滚工具" },
  { L"Reboot to Fastbootd", L"重启到 Fastbootd" },
  { L"Reboot to Bootloader", L"重启到 Bootloader" },
  { L"Reboot to Recovery", L"重启到 Recovery" },
  { L"Reboot to System", L"重启到系统" },
  { L"Rebooting to Fastbootd...", L"正在重启到 Fastbootd..." },
  { L"Rebooting to Bootloader...", L"正在重启到 Bootloader..." },
  { L"Rebooting to Recovery...", L"正在重启到 Recovery..." },
  { L"Rebooting to System...", L"正在重启到系统..." },
  { L"Write BCB", L"写入启动控制块" },
  { L"Read DeviceInfo", L"读取设备状态" },
  { L"Write DeviceInfo", L"写入设备状态" },
  { L"DeviceInfo magic", L"设备状态标识" },
  { L"DeviceInfo not initialized", L"设备状态未初始化" },
  { L"Cancelled", L"已取消" },
  { L"State unchanged", L"状态未改变" },
  { L"%s - writes DeviceInfo. May cause data loss.", L"%s：将写入设备状态。可能丢失数据。" },
  { L"Applying...", L"正在应用..." },
  { L"Done. Reboot for the change to take effect.", L"操作完成，重启后生效。" },
  { L"Unlock Device", L"解锁设备" },
  { L"Lock Device", L"锁定设备" },
  { L"Unlock Critical", L"解锁关键分区" },
  { L"Lock Critical", L"锁定关键分区" },
  { L"BL Tools  Unlock:%s  Crit:%s", L"解锁：%s  关键分区：%s" },
  { L"on", L"开" },
  { L"off", L"关" },
  { L"Alloc", L"分配内存" },
  { L"Slot %2u: 0x%016lx", L"索引 %2u: 0x%016lx" },
  { L"All rollback slots are 0", L"所有防回滚索引均为零" },
  { L"ARB Rollback Index (non-zero)", L"防回滚索引（非零）" },
  { L"Reset ARB Index", L"重置防回滚索引" },
  { L"Reset cancelled", L"已取消重置" },
  { L"Resetting ARB index...", L"正在重置防回滚索引..." },
  { L"ARB index reset complete", L"防回滚索引重置完成" },
  { L"Get ARB Value", L"读取防回滚值" },
  { L"Reset ARB Value", L"重置防回滚值" },
  { L"WARNING: this writes to the TEE and may lose keys.", L"警告：将写入 TEE，可能丢失密钥。" },
  { L"Confirm %u/5", L"确认 %u/5" },
  { L"Power = confirm   Vol+/- = cancel", L"电源键：确认  音量键：取消" },
  { L"Press power to continue.", L"按电源键继续。" },
};

CONST CHAR16 *AtUiText (IN CONST CHAR16 *Text) {
  UINTN Index;
  if (Text == NULL) return L"";
  if (SfbGfxActive ()) {
    for (Index = 0; Index < sizeof (mStrings) / sizeof (mStrings[0]); Index++) {
      if (StrCmp (Text, mStrings[Index].En) == 0) return mStrings[Index].Zh;
    }
  }
  return Text;
}

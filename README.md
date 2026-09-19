# GBL Root Canoe

[中文版](README_zh.md)

> ⚠️ **This project has been archived.** This is the final version — the patching engine is stable across multiple vendors and ABL versions, the core logic no longer changes, and active maintenance has ended. The code still works; forks are welcome. See [ARCHIVE.md](ARCHIVE.md) for details.

`gbl_root_canoe` is an EDK2-based workspace for patching the EFI applications within Qualcomm ABL (Android Bootloader) images. It leverages a GBL (Generic Bootloader Loader) vulnerability so the real ABL loads an embedded **superfastboot BDS** off the raw `efisp` partition. The BDS then scans a compatible partition (ext4/fat32) for boot entries and chains to the selected one - primarily to achieve a **Fake Locked Bootloader** state on Snapdragon 8 Gen 5 / 8 Elite (Gen 5) devices to bypass bootloader unlock detection.

`BDS.efi` is written raw to the `efisp` partition; the cracked ABL (`boot.efi`) and the boot entry list (`BOOTENTRIES`) live on the `persist` partition under its `efisp/` directory.

---

## Builder Guide

This section is for developers who want to compile the toolkits from source.

### Prerequisites
You must be on a **Linux** host to build the project:
- `gcc` / `clang`, `lld`, `make`, `zip`, `python3`
- `liblzma-dev` (for compiling `extractfv`)
- **Android NDK** (Required for `make target_magisk_module` to cross-compile tools for Android)
- **MinGW-w64**

### Build Targets

**Note:** You **do not** need to provide an `abl.img` to build the distributable toolkits or module.

- **`make target_toolkit_linux`**
  Builds the superfastboot BDS (`BDS.efi`) from the `uefi` submodule and compiles the patching utilities (`extractfv`, `patch_abl`) for Linux.

- **`make target_toolkit_windows`**
  Same as above, but cross-compiles the utilities into Windows `.exe` programs using MinGW-w64.

- **`make target_magisk_module`**
  Cross-compiles the patcher tools for Android using your NDK, builds the BDS, and packages everything as a KernelSU/Magisk module.

- **`make target_toolkit_android`**
  Produces a standalone Android arm64 toolkit (`toolkit_android.zip`) with Android-native binaries for on-device use outside of the module.

---

## User Guide

For more detailed instructions, please refer to the [Wiki](https://github.com/superturtlee/gbl_root_canoe/wiki).

### 1. Using the Module (On-Device)

The module is designed to run directly on your rooted Android device.

**Requirements:**
- Device must be Snapdragon 8 Gen 5 / 8 Elite (Gen 5).
- Bootloader must be unlocked.
- Kernel must NOT have Baseband Guard.
- The ABL on the `abl` partition must contain the GBL vulnerability. If it does not, flash an older ABL with the vulnerability first (the cracked `boot.efi` does not need to match the ABL on the `abl` partition).

**Installation & Usage:**
When flashing the module via a root manager (KernelSU, Magisk, or APatch), the script interacts with you using the volume keys:
- **Volume Up (First-time installation):** The script extracts the current-slot `.abl`, cracks it into `boot.efi`, places `boot.efi` / `LinuxLoader.efi` / `BOOTENTRIES` into `/mnt/vendor/persist/efisp/`, and flashes `BDS.efi` to `efisp`. After this, reboot into Recovery and **format Data**. Once booted, install this module again (Volume Down the second time) to complete the installation.
- **Volume Down (OTA retention or post-format):** Installs the OTA-update patch. After each OTA, open the module WebUI and flash again to retain the BL version.

### 2. Using the PC Toolkits (Linux / Windows)

If you downloaded the `target_toolkit_linux` or `target_toolkit_windows` zip files:
1. Extract the toolkit zip on your PC.
2. Place your device's stock `abl.img` inside the `images/` (or `images\`) directory of the toolkit.
3. **Linux:** Run `bash build.sh`. **Windows:** Run `build.bat`.
4. The script extracts and cracks the ABL, outputting `ABL.efi` (fake re-lock) and `ABL_original.efi` (original). `BDS.efi` is bundled. Check `patch_log.txt` - if it says "Warning: Failed to patch ABL GBL", the ABL lacks the vulnerability and the `abl` partition must be downgraded to an older ABL with it.

Then complete the install manually (see the [Wiki](https://github.com/superturtlee/gbl_root_canoe/wiki) for full steps): copy `ABL.efi` into `/mnt/vendor/persist/efisp/`, create `BOOTENTRIES`, `sync`, and flash `BDS.efi` to `efisp` (`dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M`).

### 3. OTA Upgrade
Before rebooting for an OTA update, use the module WebUI to flash and retain the old ABL version. "Update efisp" is enabled by default; for a major version upgrade keep it on, otherwise the device may get stuck on the first boot screen.

### 4. Superfastboot Usage Instructions
When OEM Unlocking is enabled and the white warning text appears on boot, press **Volume Down** to enter Superfastboot mode (the BDS).
Common commands include:
- **Temp-boot an EFI file (without flashing)**: `fastboot boot xxx.efi`
- **Lock and Unlock (BL related)**:
  - Lock BL, triggers a data wipe: `fastboot flashing lock`
  - Unlock BL, no data wipe: `fastboot flashing unlock` or `fastboot flashing unlock_critical`
  - *Note: If the TEE status is inconsistent, the device will refuse to provide the data key, rendering data inaccessible.*
- **Flashing and Erasing**:
  - `fastboot flash <partition> <file.img>`
  - `fastboot erase <partition>`
- **Rebooting**:
  - `fastboot reboot bootloader` (Next normal boot enters Official Fastboot)
  - `fastboot reboot recovery`
  - `fastboot reboot`

### 5. File Reference
1. `BDS.efi`: The superfastboot BDS, flashed raw to the `efisp` partition.
2. `boot.efi` / `ABL.efi`: The cracked ABL with fake re-lock (the module names it `boot.efi`; the toolkit names it `ABL.efi`), placed on `persist` under `efisp/`.
3. `LinuxLoader.efi` / `ABL_original.efi`: The original unpatched ABL. For analysis; do not flash to `efisp`.
4. `BOOTENTRIES`: Boot entry list, format `<name>:<path relative to efisp/>`.

## 中文启动菜单与来源声明

中文启动菜单移植自 [kepcry/gbl_root_canoe](https://github.com/kepcry/gbl_root_canoe)，采用内置中文点阵字库和 UEFI GOP 图形绘制。来源提交、移植范围、字体许可与再分发要求见 [第三方来源声明](THIRD_PARTY_NOTICES.md)。

## 双启动模式与 fastboot

工具包构建、模块安装及 WebUI 完整更新现在从同一份原始 ABL 生成两个启动文件：

| 菜单项 | 文件 | 行为 |
| --- | --- | --- |
| 启动安卓（假回锁） | `boot.efi` | 假回锁，保留默认入口 |
| 启动安卓（真实状态） | `boot_normal.efi` | 透传原始锁状态与启动验证状态 |
| 启动安卓（备份） | `boot_backup.efi` | 模块更新前的默认启动文件（若存在） |

两个模式均尝试绕过已知 OPlus `forceenablefastboot` 验证分支，假回锁模式也会生效。原本没有该验证标识的 ABL 保留原 fastboot 路径；已绕过的已知分支保持不变；存在标识但无法安全识别时构建失败，详情见补丁日志。仅更新 BDS/工具不会生成新的 loader，已有安装需执行完整修补更新才能获得真实状态入口。菜单采用黑底白字，选中项白底黑字；随包启动入口显示上述中文名称，Android Tools 显示为“安卓工具”；BOOTENTRIES 仍保留 ASCII 名称与原文件路径，自定义名称不受影响。

手动调用：`patch_abl 输入.efi 输出.efi normal` 或 `patch_abl 输入.efi 输出.efi fake_locked`；省略模式保持旧版假回锁行为。必须使用从原始 ABL 提取的 loader，不能用已假回锁产物生成真实状态模式。两种模式共享 GBL 与适用的去黄字补丁，但不共享假回锁状态修改。

真实状态透传不保证 locked/green；假回锁不等于真实回锁；fastboot 分支绕过不代表解除所有命令权限或保证 fastbootd 可用。来源、许可证与适用边界见 [第三方来源声明](THIRD_PARTY_NOTICES.md)。

### 二级菜单汉化

Fastboot 的关机/重启菜单与重启工具、BL 锁状态工具、防回滚工具现使用同一黑白中文界面，涵盖按键提示、状态、错误和操作确认。BL 写入前确认、ARB 五次确认及取消行为保持不变。无可用 GOP 时明确提示并保留英文控制台。

升级这部分界面需要同时更新 `BDS.efi` 与 `efisp/tools/` 下的三个 EFI 工具；只更新 BDS 不会改变旧工具的界面。

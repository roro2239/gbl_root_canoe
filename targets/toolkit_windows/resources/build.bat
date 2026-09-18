REM change to the directory of this script
@echo off
chcp 65001 >nul
cd /d "%~dp0"

if exist extracted\LinuxLoader.efi del /Q extracted\LinuxLoader.efi
if exist extracted\LinuxLoader.efi exit /b 1
if exist efisp\boot.efi.pending del /Q efisp\boot.efi.pending
if exist efisp\boot_normal.efi.pending del /Q efisp\boot_normal.efi.pending
bin\extractfv images/abl.img
if errorlevel 1 goto build_failed
if not exist extracted\LinuxLoader.efi goto build_failed
move /Y extracted\LinuxLoader.efi ABL_original.efi >nul
if errorlevel 1 goto build_failed
bin\patch_abl ABL_original.efi efisp\boot.efi.pending fake_locked > patch_log.txt 2>&1
if errorlevel 1 goto patch_failed
bin\patch_abl ABL_original.efi efisp\boot_normal.efi.pending normal >> patch_log.txt 2>&1
if errorlevel 1 goto patch_failed
for %%F in (efisp\boot.efi.pending efisp\boot_normal.efi.pending) do (
  if not exist %%F goto patch_failed
  if %%~zF EQU 0 goto patch_failed
)
type patch_log.txt
move /Y efisp\boot_normal.efi.pending efisp\boot_normal.efi >nul
if errorlevel 1 goto build_failed
move /Y efisp\boot.efi.pending efisp\boot.efi >nul
if errorlevel 1 goto build_failed

set GBL_OK=yes
findstr /C:"Warning: Failed to patch ABL GBL" patch_log.txt >nul && (
  set GBL_OK=no
  echo.
  echo 警告：未发现 GBL 漏洞，见下方安装说明。
  echo efisp/boot.efi is still produced and valid, but the abl partition must be
  echo downgraded to an older ABL with the GBL vulnerability before booting.
  echo 警告：此 ABL 中未找到 GBL 漏洞（Failed to patch ABL GBL）。
  echo efisp/boot.efi 仍已生成且有效，但开机前必须将 abl 分区降级为带 GBL 漏洞的旧版 ABL。
)

echo.
echo ========================================
echo 补丁产物：
echo   efisp/boot_normal.efi - 真实状态透传，两种模式均检查 OPlus fastboot 绕过
echo   efisp/boot.efi     - cracked ABL loader (fake re-lock), the ANDROID boot entry
echo   efisp/BOOTENTRIES  - boot entry list (includes the tools submenu)
echo   efisp/tools/       - tools submenu (Reboot / BL / ARB tools)
echo   BDS.efi            - superfastboot BDS (flash raw to the efisp partition)
echo   ABL_original.efi   - original unpatched loader (for analysis; do NOT flash)
echo.
echo Note: the toolkit is manual-install only; superfb does not provide automated
echo installation for toolkit users.
echo.
echo ---- Manual install flow (English) ----
echo 1. Copy the efisp/ folder to the persist boot root:
echo      cp -r efisp/. /mnt/vendor/persist/efisp/
echo    (create /mnt/vendor/persist/efisp first if needed, e.g. via MT Manager)
echo 2. sync
if "%GBL_OK%"=="no" (
  echo 3. Downgrade the abl partition to an older ABL with the GBL vulnerability
  echo    efisp/boot.efi 与 abl 分区版本不必一致
  echo 4. Flash BDS.efi to the efisp partition:
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
) else (
  echo 3. Flash BDS.efi to the efisp partition:
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
)
echo.
echo ---- 手动安装步骤 (中文) ----
echo 1. 将 efisp/ 文件夹复制到 persist 启动根目录：
echo      cp -r efisp/. /mnt/vendor/persist/efisp/
echo    （如不存在请先创建 /mnt/vendor/persist/efisp，例如用 MT 管理器）
echo 2. sync
if "%GBL_OK%"=="no" (
  echo 3. 将 abl 分区降级为带 GBL 漏洞的旧版 ABL
  echo    （efisp/boot.efi 与 abl 分区版本不必一致）
  echo 4. 将 BDS.efi 刷入 efisp 分区：
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
) else (
  echo 3. 将 BDS.efi 刷入 efisp 分区：
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
)
echo ========================================

exit /b 0
:patch_failed
type patch_log.txt
:build_failed
echo 错误：构建或部署失败，请检查上方日志。
if exist efisp\boot.efi.pending del /Q efisp\boot.efi.pending
if exist efisp\boot_normal.efi.pending del /Q efisp\boot_normal.efi.pending
exit /b 1

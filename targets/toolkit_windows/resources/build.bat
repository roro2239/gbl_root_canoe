@echo off
setlocal EnableExtensions DisableDelayedExpansion
chcp 65001 >nul
pushd "%~dp0" >nul || exit /b 1

if not exist "images\abl.img" (
  echo ERROR: Place your stock ABL image at images/abl.img first.
  goto :fail
)
if not exist "bin\extractfv.exe" (
  echo ERROR: Missing bin/extractfv.exe. Extract the complete toolkit archive.
  goto :fail
)
if not exist "bin\patch_abl.exe" (
  echo ERROR: Missing bin/patch_abl.exe. Extract the complete toolkit archive.
  goto :fail
)
if not exist "efisp\" mkdir "efisp"

REM Never reuse a loader or patched image left by an earlier run.
del /q "extracted\LinuxLoader.efi" "ABL_original.efi" "efisp\boot.efi" "efisp\boot_normal.efi" "efisp\boot.efi.pending" "efisp\boot_normal.efi.pending" "patch_log.txt" 2>nul
if exist "extracted\LinuxLoader.efi" goto :cleanup_failed
if exist "ABL_original.efi" goto :cleanup_failed
if exist "efisp\boot.efi" goto :cleanup_failed
if exist "efisp\boot_normal.efi" goto :cleanup_failed
if exist "efisp\boot.efi.pending" goto :cleanup_failed
if exist "efisp\boot_normal.efi.pending" goto :cleanup_failed
if exist "patch_log.txt" goto :cleanup_failed

"bin\extractfv.exe" "images\abl.img"
if errorlevel 1 (
  echo ERROR: extractfv failed.
  goto :fail
)
if not exist "extracted\LinuxLoader.efi" (
  echo ERROR: extractfv produced no LinuxLoader.efi
  goto :fail
)
move /Y "extracted\LinuxLoader.efi" "ABL_original.efi" >nul
if errorlevel 1 (
  echo ERROR: Cannot save ABL_original.efi.
  goto :fail
)
"bin\patch_abl.exe" "ABL_original.efi" "efisp\boot.efi.pending" fake_locked --defer-fastboot > "patch_log.txt" 2>&1
if errorlevel 1 (
  type "patch_log.txt"
  echo ERROR: patch_abl failed
  goto :fail
)
if not exist "efisp\boot.efi.pending" (
  echo ERROR: patch_abl produced no efisp/boot.efi.pending
  goto :fail
)
for %%F in ("efisp\boot.efi.pending") do if %%~zF EQU 0 (
  echo ERROR: patch_abl produced an empty efisp/boot.efi.pending
  goto :fail
)
"bin\patch_abl.exe" "ABL_original.efi" "efisp\boot_normal.efi.pending" normal --defer-fastboot >> "patch_log.txt" 2>&1
if errorlevel 1 (
  type "patch_log.txt"
  echo ERROR: patch_abl failed
  goto :fail
)
if not exist "efisp\boot_normal.efi.pending" (
  echo ERROR: patch_abl produced no efisp/boot_normal.efi.pending
  goto :fail
)
for %%F in ("efisp\boot_normal.efi.pending") do if %%~zF EQU 0 (
  echo ERROR: patch_abl produced an empty efisp/boot_normal.efi.pending
  goto :fail
)
type "patch_log.txt"
move /Y "efisp\boot_normal.efi.pending" "efisp\boot_normal.efi" >nul
if errorlevel 1 goto :fail
move /Y "efisp\boot.efi.pending" "efisp\boot.efi" >nul
if errorlevel 1 goto :fail

set "GBL_OK=yes"
findstr /C:"Warning: Failed to patch ABL GBL" "patch_log.txt" >nul && (
  set "GBL_OK=no"
  echo.
  echo WARNING: No GBL exploit found in this ABL ^(Failed to patch ABL GBL^).
  echo efisp/boot.efi is still produced and valid, but the abl partition must be
  echo downgraded to an older ABL with the GBL vulnerability before booting.
  echo 警告：此 ABL 中未找到 GBL 漏洞（Failed to patch ABL GBL）。
  echo efisp/boot.efi 仍已生成且有效，但开机前必须将 abl 分区降级为带 GBL 漏洞的旧版 ABL。
)

echo.
echo ========================================
echo Patched. Outputs:
echo   efisp/boot_normal.efi - 原始 BL 真实状态透传
echo   efisp/boot.efi     - cracked ABL loader ^(fake re-lock^), the ANDROID boot entry
echo   ENTRIES.CFG        - boot entry list ^(copy to the persist volume root^)
echo   efisp/tools/       - tools submenu ^(Reboot / BL / ARB tools^)
echo   BDS.efi            - superfastboot BDS ^(flash raw to the efisp partition^)
echo   ABL_original.efi   - original unpatched loader ^(for analysis; do NOT flash^)
echo.
echo Note: the toolkit is manual-install only; superfb does not provide automated
echo installation for toolkit users.
echo.
echo ---- Manual install flow ^(English^) ----
echo 1. Copy the efisp/ folder and ENTRIES.CFG to persist:
echo      cp -r efisp/. /mnt/vendor/persist/efisp/
echo      cp ENTRIES.CFG /mnt/vendor/persist/ENTRIES.CFG
echo    ^(create /mnt/vendor/persist/efisp first if needed, e.g. via MT Manager^)
echo 2. sync
if "%GBL_OK%"=="no" (
  echo 3. Downgrade the abl partition to an older ABL with the GBL vulnerability
  echo    ^(efisp/boot.efi and the abl partition do not need to match versions^)
  echo 4. Flash BDS.efi to the efisp partition:
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
) else (
  echo 3. Flash BDS.efi to the efisp partition:
  echo      dd if=BDS.efi of=/dev/block/by-name/efisp bs=4M
)
echo.
echo ---- 手动安装步骤 ^(中文^) ----
echo 1. 将 efisp/ 文件夹和 ENTRIES.CFG 复制到 persist：
echo      cp -r efisp/. /mnt/vendor/persist/efisp/
echo      cp ENTRIES.CFG /mnt/vendor/persist/ENTRIES.CFG
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
popd
exit /b 0

:cleanup_failed
echo ERROR: Cannot remove previous output files. Check file permissions.

:fail
if exist "efisp\boot.efi.pending" del /q "efisp\boot.efi.pending"
if exist "efisp\boot_normal.efi.pending" del /q "efisp\boot_normal.efi.pending"
popd
exit /b 1

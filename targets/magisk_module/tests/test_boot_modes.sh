#!/bin/bash
# 用法：bash test_boot_modes.sh <extractfv> <patch_abl> <原始 abl.img>
set -euo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
extractor=$(realpath "$1")
patcher=$(realpath "$2")
image=$(realpath "$3")
work=$(mktemp -d -t canoe-boot-modes-XXXXXX)
trap 'rm -rf "$work"' EXIT
module="$work/module with spaces"
mkdir -p "$module/bin" "$module/efisp/tools" "$work/commands" "$work/original"
cp "$root/targets/magisk_module/module/bin/bl_flasher.sh" "$module/bin/"
cp "$root/targets/magisk_module/module/bin/boot_modes.sh" "$module/bin/"
cp "$root/targets/magisk_module/module/ENTRIES.CFG" "$module/"
printf tools > "$module/efisp/tools/check.efi"
"$extractor" -o "$work/original" "$image" > "$work/extract.log"
"$patcher" "$work/original/LinuxLoader.efi" "$work/expected-fake.efi" fake_locked --defer-fastboot > "$work/fake.log"
"$patcher" "$work/original/LinuxLoader.efi" "$work/expected-normal.efi" normal --defer-fastboot > "$work/normal.log"
cat > "$work/commands/getprop" <<'SCRIPT'
#!/bin/sh
[ "$1" != ro.boot.slot_suffix ] || printf _a
SCRIPT
cat > "$module/bin/extractfv" <<'SCRIPT'
#!/bin/sh
exec "$TEST_EXTRACTOR" -o "$2" "$TEST_IMAGE"
SCRIPT
cat > "$module/bin/patch_abl" <<'SCRIPT'
#!/bin/sh
if [ "${TEST_MODE_FAILURE:-}" = error ] && [ "$3" = normal ]; then
  printf partial > "$2"
  exit 7
fi
if [ "${TEST_MODE_FAILURE:-}" = empty ] && [ "$3" = normal ]; then
  : > "$2"
  exit 0
fi
exec "$TEST_PATCHER" "$@"
SCRIPT
cat > "$work/commands/cp" <<'SCRIPT'
#!/bin/sh
for argument in "$@"; do
  if [ "${TEST_MODE_FAILURE:-}" = copy ] && [ "${argument##*/}" = boot_normal.efi.pending ]; then
    exit 8
  fi
done
exec /bin/cp "$@"
SCRIPT
chmod +x "$module/bin/extractfv" "$module/bin/patch_abl" "$work/commands/"*
export TEST_EXTRACTOR="$extractor" TEST_PATCHER="$patcher" TEST_IMAGE="$image"
export MODDIR="$module" PATH="$work/commands:$PATH"

# 调试入口执行完整提取、双模式生成和部署，目标是隔离临时目录。
sh "$module/bin/bl_flasher.sh" flash debug
[ "$(cat "$module/tmp/state")" = success ]
cmp "$module/tmp/efisp/boot.efi" "$work/expected-fake.efi"
cmp "$module/tmp/efisp/boot_normal.efi" "$work/expected-normal.efi"
cmp "$module/tmp/ENTRIES.CFG" "$module/ENTRIES.CFG"
[ -f "$module/tmp/efisp/tools/check.efi" ]
[ ! -e "$module/tmp/efisp/boot.efi.pending" ]
[ ! -e "$module/tmp/efisp/boot_normal.efi.pending" ]

for failure in error empty copy; do
  TEST_MODE_FAILURE="$failure" sh "$module/bin/bl_flasher.sh" flash debug
  [ "$(cat "$module/tmp/state")" = error ]
  cmp "$module/tmp/efisp/boot.efi" "$work/expected-fake.efi"
  cmp "$module/tmp/efisp/boot_normal.efi" "$work/expected-normal.efi"
  [ ! -e "$module/tmp/efisp/boot.efi.pending" ]
  [ ! -e "$module/tmp/efisp/boot_normal.efi.pending" ]
done

# 安装共用同一个发布函数，核对旧文件备份。
. "$module/bin/boot_modes.sh"
build_boot_modes "$module/bin/patch_abl" "$work/original/LinuxLoader.efi" "$module/tmp"
printf old-boot > "$module/tmp/efisp/boot.efi"
install_boot_modes "$module/tmp" "$module/tmp/efisp" yes
[ "$(cat "$module/tmp/efisp/boot_backup.efi")" = old-boot ]
cmp "$module/tmp/efisp/boot.efi" "$work/expected-fake.efi"
cmp "$module/tmp/efisp/boot_normal.efi" "$work/expected-normal.efi"
printf '模块双模式生成、部署、失败保留和备份检查通过。\n'

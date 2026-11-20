#!/system/bin/sh

# 两个模式分别读取原始 loader；官方 FB 修复由 BDS 菜单在启动时应用。
build_boot_modes() (
  patcher=$1
  input=$2
  workdir=$3
  rm -f "$workdir/patched.efi" "$workdir/normal.efi" "$workdir/patch.log" || exit 1
  for boot_mode in fake_locked normal; do
    case "$boot_mode" in
      fake_locked) output="$workdir/patched.efi" ;;
      normal) output="$workdir/normal.efi" ;;
    esac
    "$patcher" "$input" "$output" "$boot_mode" --defer-fastboot >> "$workdir/patch.log" 2>&1 || exit 1
    [ -s "$output" ] || exit 1
  done
)

# 先暂存并备份，保留当前主启动文件直到两个新文件都生成成功。
install_boot_modes() (
  workdir=$1
  destination=$2
  backup=$3
  trap 'rm -f "$destination/boot.efi.pending" "$destination/boot_normal.efi.pending"' EXIT
  cp "$workdir/patched.efi" "$destination/boot.efi.pending" || exit 1
  cp "$workdir/normal.efi" "$destination/boot_normal.efi.pending" || exit 1
  if [ "$backup" = yes ] && [ -f "$destination/boot.efi" ]; then
    cp "$destination/boot.efi" "$destination/boot_backup.efi" || exit 1
  fi
  mv "$destination/boot_normal.efi.pending" "$destination/boot_normal.efi" || exit 1
  mv "$destination/boot.efi.pending" "$destination/boot.efi" || exit 1
)

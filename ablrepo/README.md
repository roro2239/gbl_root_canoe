# ABL repo

## 机型样本覆盖与验证边界

本目录覆盖 [ditvelo/gbl_root_canoe 固定提交 2cf345a](https://github.com/ditvelo/gbl_root_canoe/tree/2cf345a299df3f8b7172d6ad2fa5985e67fe53d6/ablrepo) 收录的全部 12 个产品标识，并保留本项目的 `PLK110`，共 13 个：

`CPH2841`、`OPD2513`、`OPD2515`、`PLK110`、`PLR110`、`PLZ110`、`PMA120`、`myron`、`nezha`、`nezha_global`、`pandora`、`popsicle`、`pudding`。

`OPD2515/abl.img` 与校验值来自上述固定提交，其余参考样本与本地镜像一致。模块构建自动包含这些目录，安装时按 `ro.product.name` 精确匹配，不使用相似型号替代。

样本收录及双模式补丁回归通过不等于实机启动、刷写或看门狗验证通过。`CPH2841`、`OPD2513`、`OPD2515`、`PLR110`、`PLZ110`、`PMA120` 的 ABL 均引用相同 Phoenix 协议 GUID，但实际协议实现位于设备 UEFI。BDS 只调用已识别的 v1 关闭入口；当前布局来自 PLK110 固件逆向，其他固件版本仍需验证。其余六个参考样本未发现该 GUID，不能据此判断其看门狗行为。

Older ABL images that still carry the GBL vulnerability, used to downgrade the
`abl` partition on devices whose current ABL no longer has the exploit.

## Layout

```
ablrepo/
  <product>/
    abl.img       # raw ABL image with the GBL vulnerability
    abl.sha256    # sha256 of abl.img (sha256sum output format)
```

`<product>` is the value of `getprop ro.product.name` on the device, verbatim.

## Lookup order

On first install, when the current ABL lacks the GBL vulnerability,
`customize.sh` looks for an older ABL in this order:

1. **Local** — bundled inside the module ZIP at `$MODPATH/ablrepo/<product>/`
2. **Cloud** — `https://raw.githubusercontent.com/superturtlee/gbl_root_canoe/main/ablrepo/<product>/`

Both locations use the same layout. The image is verified against `abl.sha256`
before use. After verification, the ABL is re-patched to confirm it actually
has the GBL vulnerability; only then is it flashed to the `abl` partition.

## Adding a device

1. Obtain an older ABL for the device that still has the GBL vulnerability.
2. Name it `abl.img` and place it under `ablrepo/<product>/`.
3. Generate the checksum: `sha256sum abl.img > abl.sha256`.
4. Commit locally (bundled into the module) and push to `main` (cloud).

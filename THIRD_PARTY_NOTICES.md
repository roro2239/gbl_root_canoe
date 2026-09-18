# 中文启动菜单来源声明

本项目的中文启动菜单移植并改编自 [kepcry/gbl_root_canoe](https://github.com/kepcry/gbl_root_canoe)，来源提交为 [`66a81b4f9f4548e2d9ebfcb852eb343c628aa723`](https://github.com/kepcry/gbl_root_canoe/tree/66a81b4f9f4548e2d9ebfcb852eb343c628aa723)。感谢该仓库作者及贡献者提供的中文界面实现。

移植范围包括 `SuperFbLang.*` 的中英文文案、`SuperFbFont.*` 的点阵字形查询、`SuperFbGfx.*` 的 GOP 绘制、紫色菜单面板设计，以及 `tools/gen_sfb_font/gen_sfb_font.ps1` 字库生成脚本。菜单和文件浏览器按本项目已有接口适配，保留原有启动、按键及启动项存储行为。此次不包含上游 PIN、设置存储或字符画功能。

上游仓库根许可证为 GPL-3.0；上述带独立许可证标识的 C/H 文件保留原有 `Copyright (c) 2026, contributors to the canoe ABL tree` 和 `BSD-3-Clause` 标识，其许可全文见 [BSD-3-Clause](licenses/SuperFb-BSD-3-Clause.txt)。生成脚本保留上游来源，适用仓库根 [LICENSE](LICENSE)。此声明不表示上游作者为本项目背书。

## 字体

本项目没有沿用上游生成文件标注的 `Roboto-Regular.ttf (Google Sans CN Flex)` 字库，而是使用 [Noto Sans CJK SC Regular](https://github.com/notofonts/noto-cjk/blob/main/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf) 2.004 重新生成菜单所需的点阵字形。

- 字体版权：© 2014–2021 Adobe (http://www.adobe.com/)。
- 字体许可证：SIL Open Font License 1.1，全文见 [NotoSansCJK-OFL.txt](licenses/NotoSansCJK-OFL.txt)。
- `SuperFbFontData.c` 是字体字形的点阵子集，随附上述字体版权及许可；不是完整中文字库。

再分发相关源码、点阵字库或包含它们的二进制时，应随附本声明及相应许可证，保留原有版权信息。

## 字库再生成

在 Windows PowerShell 7 中，下载上述官方 OTF 字体后，从仓库根目录执行：

```powershell
pwsh -NoProfile -File tools/gen_sfb_font/gen_sfb_font.ps1 -FontFile "字体所在目录/NotoSansCJKsc-Regular.otf"
```

脚本读取 `SuperFbLang.c` 的文案并生成 UTF-8 无 BOM 的 `SuperFbFontData.c`，包含 ASCII 可打印字符及菜单使用的汉字。修改中文文案后必须重新生成。常规 EFI 构建直接编译生成的 C 文件，不需要安装字体。

## 显示边界

具备可用 GOP 且分辨率至少为 480×480 时，现有菜单及提示默认显示中文；无可用图形输出时明确提示初始化失败并使用英文控制台。自定义启动项名称和文件名按原有逻辑处理，未扩展 `BOOTENTRIES` 的 ASCII 格式；字库以外字符显示空心方框。独立启动的其他 EFI 应用使用各自界面。

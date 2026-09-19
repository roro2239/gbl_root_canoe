# 第三方来源声明

## 中文启动菜单

本项目的中文启动菜单移植并改编自 [kepcry/gbl_root_canoe](https://github.com/kepcry/gbl_root_canoe)，来源提交为 [`66a81b4f9f4548e2d9ebfcb852eb343c628aa723`](https://github.com/kepcry/gbl_root_canoe/tree/66a81b4f9f4548e2d9ebfcb852eb343c628aa723)。感谢该仓库作者及贡献者提供的中文界面实现。

移植范围包括 `SuperFbLang.*` 的中英文文案、`SuperFbFont.*` 的点阵字形查询、`SuperFbGfx.*` 的 GOP 绘制、菜单面板布局（本项目现采用黑白配色），以及 `tools/gen_sfb_font/gen_sfb_font.ps1` 字库生成脚本。菜单和文件浏览器按本项目已有接口适配，保留原有启动、按键及启动项存储行为。此次不包含上游 PIN、设置存储或字符画功能。

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

脚本读取 `SuperFbLang.c` 和 `AndroidToolsLang.c` 的文案并生成 UTF-8 无 BOM 的 `SuperFbFontData.c`，包含 ASCII 可打印字符及菜单使用的汉字。修改中文文案后必须重新生成。常规 EFI 构建直接编译生成的 C 文件，不需要安装字体。

## 显示边界

具备可用 GOP 且分辨率至少为 480×480 时，现有菜单及提示默认显示中文；无可用图形输出时明确提示初始化失败并使用英文控制台。自定义启动项名称和文件名按原有逻辑处理，未扩展 `BOOTENTRIES` 的 ASCII 格式；字库以外字符显示空心方框。内置 Fastboot 与 RebootTools、BLTools、ArbTools 也复用上述绘图和字库，包含中文菜单、状态与确认警告；其他 EFI 应用仍使用各自界面。

## 真实状态透传与 OPlus fastboot 补丁

双模式设计移植并改编自 [ditvelo/gbl_root_canoe](https://github.com/ditvelo/gbl_root_canoe)，固定来源提交为 [`2cf345a299df3f8b7172d6ad2fa5985e67fe53d6`](https://github.com/ditvelo/gbl_root_canoe/tree/2cf345a299df3f8b7172d6ad2fa5985e67fe53d6)，遵循上游 GPL-3.0，许可证全文见根目录 [LICENSE](LICENSE)。感谢上游作者及贡献者；本声明不表示其为本项目背书。

移植涉及 `submodules/patcher` 的 `PatchMode`、`PatchBufferEx`、真实锁状态来源定位及 `normal` / `fake_locked` 模式分流，并参考其 `forceenablefastboot` 的 OPlus 验证分支绕过。本项目保留旧调用默认假回锁，增加分支布局、目标边界、唯一匹配与重复补丁检查；同时接入模块安装、WebUI 后端及三平台工具包的双产物流程。两个模式都独立从原始 ABL 生成，并共同执行 fastboot 分支检查。

`normal` 保留原始 ABL 的锁状态、verified boot 状态计算及写回，不保证设备呈现 locked 或 green。`fake_locked` 仅改变软件报告链路，不会真正回锁，也不会改变 TEE 或硬件安全状态。fastboot 补丁只针对已识别的 OPlus 验证错误分支，不解除全部 fastboot 命令权限、不保证 fastbootd 可用。未发现标识时保留原路径并记录未匹配；发现标识但布局不受支持时停止生成，不能当作成功。真机兼容性需单独验证。

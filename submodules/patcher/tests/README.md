# 双模式补丁回归

在 Linux/WSL 中从 `submodules/patcher` 执行：

```sh
make build
make test TEST_CC=clang-20
gcc ../ablfvextractor/extractfv.c -llzma -o build/extractfv
python3 tests/test_samples.py build/patch_abl build/extractfv
```

`TEST_CC` 可改为本机支持 AddressSanitizer 和 UndefinedBehaviorSanitizer 的 Clang 命令。指令测试覆盖 CBZ 的 W/X 形式、已绕过分支、标识缺失、未知布局、重复引用、目标越界、短输入与失败缓冲区不变。

样本测试从仓库 `ablrepo` 提取原始 loader，验证真实状态模式保留状态锚点、读取、写回与状态字符串引用，假回锁模式修改这些位置，两种模式应用相同 fastboot 分支。还检查旧 CLI 默认行为、非法模式、拒绝假回锁输入及失败时保留旧输出。测试产物只写入临时目录，不调用设备或分区写入。

这些测试验证二进制变更与错误处理，不能替代真机启动、fastboot 连接和具体命令权限测试。

# Vivado／SDK 2018.3 复现入口

本文介绍 Vivado 2018.3 与 SDK 2018.3 的复现入口，目标器件为 xc7z020clg400-2。

## 最短操作路径

1. 现场运行：使用演示包中的 SD 启动文件和 Windows 展示应用，无需安装 Vivado、SDK 或 Python；接线和按键步骤见相邻演示包说明。
2. 源码核查：在完整解压的 `stereo_snapshot/` 中执行 `python reproduce.py verify`。该操作核对源码身份，不需要启动 EDA。
3. 完整重建：在 Windows 上安装 Python 3.12 和下列已验证工具。先集中检查环境，通过后再执行完整构建。

```powershell
python reproduce.py doctor --xilinx-root C:/tools/Xilinx --iverilog-root C:/tools/iverilog
python reproduce.py rebuild --xilinx-root C:/tools/Xilinx --iverilog-root C:/tools/iverilog
```

将示例目录换为实际安装目录。Xilinx 根目录下应有 `Vivado/2018.3` 与 `SDK/2018.3`；Icarus 根目录下应有 `bin/iverilog.exe` 和 `bin/vvp.exe`。分开安装时，可按根目录 README 的环境变量逐项指定。显式环境变量优先，检测发现其版本不符时会报告，不会偷偷换用另一套工具。

## 安装内容与检查

| 组件 | 用途与版本 |
|---|---|
| Vivado 2018.3 | 硬件设计、Zynq-7000 器件支持、宿主 GCC |
| SDK 2018.3 | ARM GCC 7.3.1、make、HSI、FSBL 和 Bootgen |
| Icarus Verilog | 本工程记录的 11.0 开发版，含 `iverilog.exe` 与 `vvp.exe` |
| Python 3.12 | 构建与核验入口；工程已使用 3.12.14 |

Vivado 与 SDK 可从 [AMD 2018.3 下载页](https://www.amd.com/zh-cn/support/downloads/adaptive-socs-and-fpgas/development-tools/2018-3.html) 获取。安装基础 2018.3 和 Zynq-7000 支持；下载及许可由复现者按 AMD 提示完成。Icarus 精确版本与工具指纹见 `config/toolchain.json`，不能把另一版本的文件改名当作相同工具。

`doctor` 一次列出八个工具的缺失或不匹配位置，并同时核对 Vivado、HSI、Bootgen 的启动器和实际程序。它不安装软件、不改变系统 PATH、不启动综合。只有返回 `ENVIRONMENT_READY` 才会由 `rebuild` 转入已有分阶段构建；各阶段继续执行动态内存、单重任务和失败保留策略。检查结果可用 `--json-out` 写入一个尚不存在的文件。

完整构建生成 `build/rehearsal_*/rebuild_result.json`；`BOOT_BUILT_NOT_DEPLOYED` 表示源码已生成启动候选。所有阶段均保留日志，已取得的实际重建结果及恢复步骤见恢复工程中的 `hardware/rebuild_notes.md`。

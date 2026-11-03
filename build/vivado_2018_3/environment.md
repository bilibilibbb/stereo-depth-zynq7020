# Stereo snapshot source package

本工程对应“基于深度学习的双目感知加速系统”（初级）的固定 11459 小模型拍照演示。后端为 v351a C＋v279 混合 RTL，FIELD 在 ARM，器件 xc7z020clg400-2，计算时钟 100 MHz，硬件身份 `0x0e9a08d3`。SD 启动及电脑端直接运行使用代码包根目录的 `board_demo.zip`，完整解压后得到 `stereo_demo/`。

## 评阅快速入口

恢复工程后的 `docs/reviewer_quickstart.md` 提供评阅与复现说明。自主选题指南 3.3.3.1 允许在报告中声明并复现其他工具版本，本工程采用已经完整重建的 Vivado／SDK 2018.3 路线。

源码核查使用 `python reproduce.py verify`。完整重建前，执行 `python reproduce.py doctor --xilinx-root C:/tools/Xilinx --iverilog-root C:/tools/iverilog`，将目录换为实际安装位置。检查通过后将 `doctor` 改为 `rebuild`，自动设置本次子进程的工具路径并调用原有受保护构建。旧的逐项环境变量和分阶段入口继续可用。

本入口不会把 2025.2／2026.1 当作已验证的完整重建环境；不同工具会在重任务之前明确报出。现有启动程序与展示应用可以独立演示，正常运行不需要安装开发工具。

## 环境

已验证环境为 Windows、Python 3.12.14、Vivado／SDK 2018.3。ARM 编译器是 SDK 自带 Linaro GCC 7.3.1；BSP make 来自同一 SDK。DMA 仿真使用 Icarus 11.0 开发版 `s20150603-642-g3bdb50da`。`config/toolchain.json` 和 `config/vivado.json` 给出精确工具哈希，不能把更换版本后的结果算作原环境复现。工具由使用者自行安装。

电脑程序需要 Pillow 12.3.0、PyInstaller 6.22.3、Python 自带 Tk，以及 Windows .NET Framework 4.x 的 C# 编译器。可在单独 Python 环境中运行 `python -m pip install -r desktop_app/requirements.txt`。源代码构建使用 Python；演示 EXE 本身不需要安装 Python。

在 PowerShell 设置对应工具的真实位置，以下仅示范目录结构：

```powershell
$env:ARM_PATH='C:/tools/Xilinx/SDK/2018.3/gnu/aarch32/nt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc.exe'
$env:MAKE_PATH='C:/tools/Xilinx/SDK/2018.3/gnuwin/bin/make.exe'
$env:HOST_PATH='C:/tools/Xilinx/Vivado/2018.3/tps/mingw/6.2.0/win64.o/nt/bin/gcc.exe'
$env:VIVADO_PATH='C:/tools/Xilinx/Vivado/2018.3/bin/vivado.bat'
$env:HSI_PATH='C:/tools/Xilinx/SDK/2018.3/bin/hsi.bat'
$env:IVERILOG_PATH='C:/tools/iverilog/bin/iverilog.exe'
$env:VVP_PATH='C:/tools/iverilog/bin/vvp.exe'
$env:BOOTGEN_PATH='C:/tools/Xilinx/SDK/2018.3/bin/bootgen.bat'
python run.py verify
```

## 已提供并实际验证的入口

| 命令 | 输出与验证范围 |
|---|---|
| `python run.py firmware` | 完整双核 ARM ELF；检查实际算术、网络、CPU1 启动及内存布局 |
| `python run.py bsp` | 从保留的 13 个 BSP 模块源码重建 libxil.a，核对 95 个库成员 |
| `python run.py short-burst` | 三个实际 DMA 主模块各 26 个用例、9,216 个检查 |
| `python run.py platform` | 完整 BD/IP、PS 初始化、地址和时钟复位表；生成前重新检查实际 DMA |
| `python run.py desktop-build` | 英文名便携应用，并自动执行冻结 EXE 离线自检 |
| `python run.py desktop-check` | 从源码执行离线协议、图像格式和重连自检 |
| `python run.py pair-replay` | 重放随包真实结果，核对同源身份、CRC 和三张结果图 |
| `python run.py hardware-plan` | 解析全部 60 份综合 RTL，生成分块输入、数值参数和真实子网表边界；不启动 EDA |
| `python run.py fsbl --platform build/<平台运行目录>/platform_result.json` | 从当前平台导出重建 SDK FSBL 与其 BSP，核对 PS 初始化字节和完整链接；不生成启动镜像 |
| `python run.py lifecycle` | 编译实际结果导出与三槽管理代码，检查发布、锁定、释放和异常行为 |
| `python run.py benchmark` | 以确定性输入对两种完整同步 C 表示进行交替顺序配对计时，校验输出；不是 ARM 或 PL 计时 |

每次运行在 `build/` 新建独立目录。先看 `guard.json`，再看相应 `result.json`、`platform_result.json`、`bsp_result.json` 或 `desktop_build.json`。所有入口使用动态自有进程树内存／提交余量保护、单 CPU 和全项目互斥；内存超限终止本次任务并保留记录。它们不复位板卡、不下载、不写 SD，也不修改系统网卡。

`platform` 仅做工程与 IP 生成。`fsbl` 必须明确指向本包一次成功的 `platform` 结果，不能填写历史私有路径。`hardware-plan` 生成本目录可用的实际分块输入；完整物理实现的步骤与边界见 `hardware/rebuild_notes.md`。新机器的网卡提权配置仍需实际确认。

## 从源码构建启动候选

设置上述全部环境变量后执行 `python rebuild_all.py`。程序顺序执行生命周期检查、电脑 C 对照、BSP 重建、固件链接、平台生成、FSBL、分块综合、完整平台、约束预检、布局、布线、bitstream 和 BOOT.BIN。每个重任务仍由 `run.py` 独立保护；任一步失败就停止，保留证据，不跳过时序检查或重复下载。

总记录为 `build/rehearsal_*/rebuild_result.json`。只有状态 `BOOT_BUILT_NOT_DEPLOYED` 表示本次源码链已生成启动候选，具体输出由 `boot_receipt` 指定。单阶段结果、旧布线报告及已经演示的 BOOT.BIN 均不能替代这项结论。该命令不写 SD、不操作 JTAG、不复位板卡；新构建镜像与原演示镜像分别管理。复跑记录见 `hardware/rebuild_notes.md`，设备与外部接口资格按实际报告判断。

需要逐阶段定位时，使用 `python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage <阶段>`；完整平台、预检和布局另传 `--platform build/<平台>/platform_result.json`。最后通过 `boot-image` 显式指定 bitstream、FSBL 和固件结果，完整命令见硬件说明。

## 数值、迁移与目录

生产 C、RTL 和权重采用原字节副本。构建脚本仅将工具路径、原先的历史固定对象记录及公共资源辅助模块改为包内显式依赖。`reference/build/runtime_fix_binding.json` 保留原有两个修复对象的检查，不再要求私有历史 build 目录存在。

新链接 ELF 与已演示 ELF 的哈希不同：编译路径进入了少数 lwIP/GEM 诊断字符串，并引起后续地址重排。独立比较已核对对应指令和重定位；BSP 95 个对象成员的可加载代码、数据和非调试重定位一致，调试路径不同。复构结果为未下载候选，已演示程序仍以 `stereo_demo/sd/BOOT.BIN` 为准。

- `hardware/`：实际平台 Tcl、RTL 清单、引脚／时钟约束及接口身份。
- `firmware/`、`common/`、`reference/`：双核程序、协议、冻结计算、SDK/BSP 与网络依赖。
- `model/`：模型图、参数身份及训练来源；不需要重新训练才能构建固件。
- `desktop_app/`：内置展示应用和网络辅助程序源码。
- `data/`：当前板端真实结果和图像身份记录。
- `tests/`、`scripts/`：必要验证与受保护构建入口。
- `licenses/`、`third_party.md`：各组件许可和来源；工具仿真库的外部要求见 `external_dependencies.json`。

自定义发布路径使用英文；供应商源码、运行库、标准 README 及板端固定启动文件保留原名。本项目自行编写的代码、文档及团队自有的固定模型参数采用根目录 MIT 许可证。第三方源码、运行库、工具、数据集与参考格式保留各自原声明；本许可证不为它们重新授权。 本地候选未上传或发布。`source_manifest.json` 用于核对分发文件；可重新生成的工程缓存和私有工作日志不随源码压缩包发送；直接演示所需文件集中在根目录演示包。

## 本次实际复构结果（2026-10-31）

本次英文独立目录已实际完成分块综合、完整平台综合、约束预检、布局、布线、bitstream 和 BOOT.BIN。新布线内部 WNS +0.063 ns、WHS +0.052 ns、WPWS +3.870 ns，布线完整且无布线错误。 本轮是修复复现入口后按阶段接续完成的真实链路，不宣称最终脚本曾一次无中断跑通。新启动候选未写卡、未下载、未做新的板端验收；演示仍使用原来已接受的 BOOT.BIN。外部相机输入时序及部分复位 CDC 的资格边界保持。

低内存支持只作用于本次 Job 内已核实的 Vivado；不关闭其他应用或改变动态保护。失败时先保留记录并改变内存方法，不能原样循环重试。详细范围和阶段命令见 `hardware/rebuild_notes.md`。

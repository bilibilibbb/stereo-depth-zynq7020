# Vivado 2026.1 工程迁移

本目录提供本作品的 Vivado 2026.1 构建入口。它复用已验证的 2018.3 生产源码，新入口与适配文件单独放置。已验收的 2018.3 演示和提交包保持原样。

## 当前已实现

- `stereo_snapshot/run_2026.py`：独立的源码核对、实际工具识别与平台生成入口，继续使用已有 Windows Job 动态整树内存保护、单 CPU 和全项目互斥。
- `modern/platform_2026.tcl`：保留现有 PS 设置、四路 HP、地址、50／100 MHz 时钟及全部计算 RTL；在当前工具库中解析 PS7／AXI IP 并重新生成；改用 `write_hw_platform` 导出 XSA。
- 新入口核对实际 `Vivado v2026.1` 输出，并记录启动器与程序文件指纹。2018.3、2025.2 和 2026.1.1 不作为本次基础版本的通过证据。
- 原文件和迁移文件分别核对；新平台生成、综合、布局、布线与软件结果各自需要新证据。旧检查点只保留其原有适用范围。

本机已用 `D:/2026.1` 完成从平台／XSA、分块综合、布局布线到 bitstream 与 BOOT.BIN 的完整分步构建及依赖核验。最终 Slice LUT 44,544／53,200（83.73%）、BRAM 98／140、DSP 172／220；100 MHz 内部时序通过，WNS +0.008 ns、WHS +0.057 ns，总线偏斜通过。时序裕量较小，保留完整报告。新版镜像已写入原 SD 并完成冷启动，真实预览、PS_KEY1 原图与同源 SGM／小模型结果已接收并在应用中核验；原标定、配置和旧版回退文件均保留。

## 两套脚本的提交安排

| 用途 | Vivado 2018.3 | Vivado 2026.1 |
|---|---|---|
| 环境及构建入口 | `reproduce.py`、`run.py` | `reproduce_2026.py`；分步入口为 `run_2026.py`、`build_hardware_2026.py` |
| Tcl／Python 构建文件 | `scripts/` | `modern/`；未变化的物理约束与脚本继续引用 `scripts/` |
| 实际输出目录 | `build/` | `build2026/` |
| 平台导出 | 旧硬件平台接口 | XSA 与新生成的 IP／HDL |
| 验证记录 | 保留已完成的旧版重建及演示记录 | 单独生成新版本的资源、时序和启动记录 |

两版生产 C、RTL 与模型参数保持原字节；分发说明和文件清单随交付内容调整，新旧入口分别执行完整性核验。两套脚本不靠覆盖旧文件、修改工具版本字符串或复用旧网表实现切换。

本次迁移的硬件由 Vivado 2026.1 重新生成，启动镜像由配套 Bootgen 打包。ARM 应用和 FSBL 暂采用已验收的 SDK 2018.3 / GCC 7.3.1 版本，同时提供其源码和旧版重建入口；它们不写成 Vitis 2026.1 重新编译的结果。打包入口还会逐项核对新 XSA 与原 FSBL 的全部 21 组 PS 初始化指令，只有寄存器设置、操作和延时一致才允许组合启动镜像。

## 安装准备

官方入口：[AMD 2026.1 下载页面](https://www.amd.com/en/support/downloads/adaptive-socs-and-fpgas/development-tools/2026-1.html)。选择基础版 **2026.1 Windows Self Extracting Web Installer**；页面列出的文件为 `FPGAs_AdaptiveSoCs_Unified_SDI_2026.1_0616_1700_Win64.exe`。该入口需要用户在 AMD 官方网页完成登录和下载资料确认。

新版独立安装，保留 `D:/vivado_18.3`。本次实际目录为 `D:/2026.1`，硬件脚本需要 Vivado、Zynq-7000 器件支持和配套 Bootgen。直接复现启动镜像可使用包内已验收的 ARM 文件；从 C 源码重建使用明确列出的 SDK 2018.3 工具。若另行迁移 ARM 软件工具链，才需要完整的 Vitis Embedded Development 环境和对应验证。

AMD 2026.1 Basic 许可覆盖 Zynq-7000，免费许可按其官方流程申请；不沿用来源不明的许可文件。2026.1.1 的许可条件不同，本次验证基础版 2026.1。

## 工具安装后的首个验证

在 `stereo_snapshot/` 中运行，替换为实际工具目录：

```powershell
python run_2026.py verify
python run_2026.py doctor --vivado D:/2026.1/Vivado/bin/vivado.bat
python run_2026.py platform --vivado D:/2026.1/Vivado/bin/vivado.bat
```

实际安装布局以文件为准。环境变量为 `VIVADO_2026_PATH`，不会使用旧入口的 `VIVADO_PATH` 代替。每次任务在本目录 `build2026/` 新建输出并保留失败日志。平台生成应得到 XSA、PS 配置、地址与时钟记录、实际 IP 版本、生成的 HDL 和源文件／工具指纹；它不自动启动综合或操作板卡。

## 复现流程与验证范围

1. 先取得真实 2026.1 平台与 XSA，核对地址、时钟及 PS 初始化。
2. 生成来源绑定的硬件分块计划；NN 的计算、读入和写回子模块分别综合，再装配实际检查点，以降低综合峰值内存，RTL 与参数不变。
3. 使用新版新生成的 IP、网表与约束完成综合、布局、布线、bitstream；若默认布线尚有建立时间违例，执行一次 `phys_opt_design -directive Explore`，随后重新核对 100 MHz 内部时序、保持时间、总线偏斜及完整资源。原 RTL 和约束保持。
4. 核对 PS 初始化与已验收 ARM 软件，生成独立启动候选。以上构建已实际完成。全新总入口的路径传递、失败停止和错误状态拒绝另通过 5 项离板行为检查，该检查与真实分步构建分别记录。

对应命令（路径替换为本次操作实际返回的记录，不使用旧输出冒充新构建）：

完整顺序入口如下，它会自动传递各阶段输出路径，并在任一阶段失败时保留记录、停止后续步骤：

```powershell
python reproduce_2026.py verify
python reproduce_2026.py rebuild --vivado D:/2026.1/Vivado/bin/vivado.bat
```

需要逐阶段运行时使用：

```powershell
python run_2026.py hardware-plan --platform build2026/<平台记录目录>/platform_result.json --vivado D:/2026.1/Vivado/bin/vivado.bat
python build_hardware_2026.py --platform build2026/<平台记录目录>/platform_result.json --plan build2026/<计划目录>/hardware_plan.json --vivado D:/2026.1/Vivado/bin/vivado.bat
python run_2026.py boot-image --plan build2026/<计划目录>/hardware_plan.json --vivado D:/2026.1/Vivado/bin/vivado.bat
```

## 已核对的官方资料

- [2026.1 write_hw_platform Tcl 命令](https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/write_hw_platform)
- [2026.1 phys_opt_design 布线后物理优化](https://docs.amd.com/r/en-US/ug835-vivado-tcl-commands/phys_opt_design)
- [Vitis 2026.1 平台 Python API](https://docs.amd.com/r/en-US/ug1400-vitis-embedded/Platform)
- [Vitis 批处理入口](https://docs.amd.com/r/en-US/ug1400-vitis-embedded/Vitis-Unified-IDE-Launch-Options)
- [安装 Vitis 软件平台](https://docs.amd.com/r/en-US/ug1742-vitis-release-notes/Installing-the-Vitis-Software-Platform)
- [2026.1 许可与器件支持](https://docs.amd.com/r/en-US/ug973-vivado-release-notes-install-license/Device-Availability-by-Subscription-Tier)

# Hardware generation and implementation provenance

`python run.py platform` 在独立目录中执行现行 `hardware/platform.tcl`，验证并导出完整 BD、IP、PS 初始化、地址和时钟复位表。已在英文交付目录运行通过。使用者须安装 Vivado 2018.3，工具身份和全部 65 个硬件输入保持检查。程序自动建立并精确移除临时 ASCII 盘符别名；不移动源码，不复用或覆盖已有盘符。

生成后的顶层为 `snapshot_platform_top`，器件 xc7z020clg400-2。实际硬件包含相机、SGM、混合计算通路、PS7 和真实 AXI 互连；不能只综合计算模块的面积作为整机容量。

## 本包的构建输入与依赖

先执行 `python run.py hardware-plan`。它将从本包源码解析依赖，在该次 `build/<目录>/` 下生成 `hardware_plan.json`、真实边界头文件和以下 Tcl 输入，不读取原工作区的历史 build 目录。生成物逐项绑定源文件与脚本哈希；边界头文件必须接回相应真实子网表，不能单独当作完整模块。

| 顺序 | 子目录／顶层 | 当前源码覆盖 | 需要的前序产物 | 原实现脚本与产物 |
|---|---|---:|---|---|
| 1 | `stages/nn`／`retained_neural_service_v2` | 13 份 | 无 | `scripts/legacy_nn_area.tcl` → `nn.dcp` |
| 2 | `stages/sgm_volume`／`fw_sgm_volume_rtl` | 4 份 | 无 | `scripts/component_area.tcl` → `component.edf` |
| 3 | `stages/camera`／`camera_pair_capture` | 7 份 | 无 | `scripts/component_area.tcl` → `component.edf` |
| 4 | `stages/compute`／`r14_shared_compute_mmio_v1` | 32 份含 NN | 第 1 步的真实 DCP | `scripts/legacy_component_area.tcl` → `component.edf` |
| 5 | `stages/sgm`／`fw_sgm_mmio_rtl` | 10 份含代价卷 | 第 2 步的真实 EDIF | `scripts/legacy_component_area.tcl` → `component.edf` |
| 6 | `stages/platform`／`snapshot_platform_top` | 顶层、外围与互连 | 本次生成的 BD/IP，第 3—5 步的真实 EDIF | `scripts/platform_area.tcl` → 完整 `component.edf` |

这些覆盖有父子重叠，不能相加；并集与 `hardware/sources.json` 的全部 60 份综合源码一致。第 6 步的 `SNAP_REBUILD_PLATFORM_BD` 明确设为本次 `run.py platform` 生成的 `project/snapshot.srcs/sources_1/bd/snapshot_bd`，包括实际 10 个叶子 IP 和 3 个模块引用封装。每个阶段以自己的子目录为工作目录读取生成的 `area_sources.tcl` 或 `nn_sources.tcl`；NN 同目录另有 10 ns 时钟输入。

生成器保留实际综合策略与完整顶层 IO 插入。计算地址范围为 `0x19000000`—`0x1a000000`，SGM 范围为 `0x194e0000`—`0x19666000`，计算通路 `TWO_HP=1`，两模块 `BUILD_ID=0x0e9a08d3`。数值泛型采用显式 32 位 HDL 常量；仍须用 `scripts/vivado_generics.py` 检查实际展开日志，防止工具把十进制参数解释为字符串。

已将生成的组件原生模块集合、源码字节、数值泛型，以及完整平台 IP／封装集合与实际已演示构建输入逐项比较。现提供 `rebuild_all.py` 顺序执行器，以及 `run.py hardware-stage` 分步入口。`reference/build/rebuild_admission.json` 绑定不变的生产代码与适用数值证据，仅准入不改生产源码的重建；不会继承旧的物理 PASS。各阶段验证本次真实前序产物与成功 guard，处于动态整树内存、单 CPU／单重任务保护下；清单中的环境标志本身不构成准入。

## 完整执行命令

推荐先按 README 设置工具路径，再运行 `python rebuild_all.py`。逐阶段运行时，将 `<计划>`、`<平台>` 等替换为本次命令返回的相对目录，不填原工程历史路径：

```powershell
python run.py firmware
python run.py platform
python run.py fsbl --platform build/<平台>/platform_result.json
python run.py hardware-plan
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage nn
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage sgm_volume
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage camera
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage compute
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage sgm
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage platform --platform build/<平台>/platform_result.json
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage preflight --platform build/<平台>/platform_result.json
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage place --platform build/<平台>/platform_result.json
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage route
python run.py hardware-stage --plan build/<计划>/hardware_plan.json --stage bitstream
python run.py boot-image --bitstream build/<计划>/stages/bitstream/stage_result.json --fsbl build/<FSBL>/fsbl_result.json --firmware build/<固件>/comparison.json
```

这组命令运行后才有新候选，列出命令不表示运行通过。完整资源不足、黑盒未消除、关键警告、内部建立／保持／脉宽不通过或布线未完成时，执行器停止。外部 DVP 与复位 CDC 的既有资格边界仍单独列明；本包不把它们自动豁免为已签核。失败目录保留；修改源代码或构建策略后新建计划。若仅修复执行器或内存驻留方法，可先将失败阶段目录保存为带时间戳的同级目录，再接续同一计划；前序来源、产物和成功 guard 仍须全部核对，不能覆盖失败报告或复用改变的硬件。内存按实时余量动态保护；不能以换机器为由跳过来源和数值绑定。

## 从本包平台导出重建 FSBL

设置 README 中的 `HSI_PATH` 和 `ARM_PATH`，先完成 `python run.py platform`，再把其实际 `platform_result.json` 相对路径传给 `python run.py fsbl --platform ...`。入口重新核对平台来源、每份生成物、硬件合同和 SDK 工具身份，以当前 HWDEF 的原字节生成 HDF，再由 SDK 编译 FSBL 和相应 BSP；检查 PS 初始化完全一致、无未定义符号且包含真实入口。

英文交付目录已实际完成这一步。结果是未部署的 `executable.elf`，不是 BOOT.BIN，也不能替换演示包中的启动文件来冒称板测通过。

## 已演示物理版本的构建过程

1. 由相同平台脚本生成 BD/IP 和所用 RTL 清单。原有大型神经、整数、上下文及 BOX 子模块单独综合，绑定源文件、参数和导出网表；PS7／AXI IP 由厂商生成。顶层综合时接回这些实际子网表，核对所有黑盒均已消除、算术及存储单元保留。分块用于降低主机驻留，未删除功能模块。
2. 在完整网表上施加 PS 的生成约束、`constraints/pins.tcl` 与 `constraints/camera_cdc.tcl`，按真实 XPM 实例施加当前安装版本的厂商 CDC 约束。计算时钟 10 ns，板时钟 20 ns；相机 PCLK 检查使用历史 16.667 ns 条件。
3. 完整布局调用 `place_design -directive Default`，完整布线调用 `route_design -directive Default`。分别保留容量、实际时钟、DRC、原始未豁免 CDC、未约束路径和布线状态报告；结果范围见提交材料 `evidence/route_review.json`。
4. 用经过核对的布线后硬件、完整 A9 ELF 及 SDK FSBL 生成 BOOT.BIN。`stereo_demo/sd/` 提供已实际 SD 启动、预览和按键验证的原字节恢复文件。

`reference_recipe/` 保留原完整面积、布局和布线阶段的实际 Tcl 输入及关键命令，原工作区路径仅为来源记录。实际重建使用上述包内入口生成新的相对路径和绑定，不直接执行这些历史输入，不冒用旧网表哈希或设置旧 PASS。

## 当前复现边界

本次英文独立目录已实际完成分块综合、完整平台综合、约束预检、布局、布线、bitstream 和 BOOT.BIN。新布线内部 WNS +0.063 ns、WHS +0.052 ns、WPWS +3.870 ns，布线完整且无布线错误。

实际还完成生命周期检查、电脑纯 C 配对测试、完整 ARM 链接、13 模块／95 成员 BSP 重建、BD/IP 与 FSBL 生成。生产源码保持原字节。移交时发现并修正了 NN 时钟名、层次 EDIF 文件名、Slice LUT 统计口径、打包清单遗漏和 bootgen 的 SDK 运行库入口；这些属于复现入口问题，不记为算法或硬件提速。

布局曾在初始化阶段分配内存失败。现使用已有工程的自有进程驻留释放方法：仅核实并释放当前 Windows Job 内 Vivado 的驻留页，私有提交量仍计入用量，动态限额和连续监测保持。分页可能延长运行时间，不能保证任意低内存环境都能完成。

本轮是修复复现入口后按阶段接续完成的真实链路，不宣称最终脚本曾一次无中断跑通。新启动候选未写卡、未下载、未做新的板端验收；演示仍使用原来已接受的 BOOT.BIN。外部相机输入时序及部分复位 CDC 的资格边界保持。

本轮完整记录见统一审阅附件 `evidence/rebuild_20261031/verification.json`，原失败和每段执行器字节保留。源码包不附大型中间网表和检查点；执行器依据本次实际生成物重建。已演示物理版本仍为 LUT 46,214、FF 37,882、DSP 170、36 Kb 等效 BRAM 96、Slice 13,282／13,300，内部 WNS +0.063 ns、WHS +0.052 ns；它与新的未部署候选分别记录。

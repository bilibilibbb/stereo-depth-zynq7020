# 基于深度学习的双目感知加速系统

本工程在 AMD Zynq-7020（xc7z020clg400-2）上实现双目采集、SGM 匹配、轻量视差细化、以太网传图和电脑展示。短按 PS_KEY1，显示最近完成的同源原图、纯 SGM 和细化结果；视频流独立更新。

## 从这里开始

| 目的 | 入口 | 所需环境 |
|---|---|---|
| 直接运行作品 | 根目录的 [board_demo.zip](board_demo.zip) | 配套板卡、双目相机、SD 卡、网线及 Windows 电脑；无需安装 Python、Vivado 或 Vitis。 |
| 用 Vivado 2026.1 重建启动镜像 | 下文“从源码重建”中的 2026.1 命令 | Windows、Python 3.12、Vivado 2026.1、Zynq-7000 器件支持及配套 Bootgen；使用随包 ARM 文件，不调用 SDK 2018.3。 |
| 从源码重新编译 ARM 并构建整机 | 下文 2018.3 命令 | Windows、Python 3.12、Vivado／SDK 2018.3、Icarus Verilog；修改工具路径后先执行环境检查。 |

技术说明见 [AMD 技术报告](report/amd_report.pdf)，协作要点见 [大模型协作记录](report/ai_collaboration.md)，技能使用入口见 [skill/README.md](skill/README.md)。

## 直接运行

1. 完整解压根目录的 `board_demo.zip`，得到 `stereo_demo/`。保留 `app/` 内全部文件，尤其是 `_internal/` 运行库。
2. 给板卡断电，把本板 SD 卡接到电脑并备份原文件。将 `stereo_demo/sd/` 中的 `BOOT.BIN`、`R14CAL.BIN`、`SNAPCFG.BIN` 复制到卡根目录；已有 `BOOTSEQ.BIN` 保留，不覆盖。
3. 安全弹出 SD 卡并插回断电板卡，使用已配置的 SD 启动方式。将 **PS 网口 J23** 直连电脑有线网口，再给板卡通电。
4. 运行 `stereo_demo/app/stereo_viewer.exe`，在窗口中选择连接板卡的有线网卡，点击“连接所选网卡”。如应用提示配置临时地址，按提示授予管理员权限。
5. 视频出现后，将场景摆稳，短按 **PS_KEY1**。应用显示原图、纯 SGM 和细化结果，视频流继续更新。有完整结果时立即选择最新结果；暂无结果时等待首组完成。

板端地址为 `192.168.1.10`，已验证的电脑地址为 `192.168.1.100`，预览与结果分别使用 TCP 6000、6001。退出应用时会释放由它添加的临时地址与路由，保留其他网络设置。J11 是另一网口，本演示使用 J23。

标定文件对应本工程当前相机及安装位置；镜头或两相机相对位置变化后须重新标定。连接和文件说明见 [board/README.md](board/README.md)，演示包内另有操作说明、许可证和 `sha256.json` 文件校验清单。

## 从源码重建

建议将代码包完整解压到英文路径，例如 `D:/stereo_depth`，使用 PowerShell。下面的 `C:/tools/` 是工具安装位置示例，请替换为本机路径。两条构建路线分别执行。

先在代码包根目录（包含本 README 的目录）核对样本并恢复两版工程：

```powershell
cd D:/stereo_depth
python sim/check_sample.py
python build/restore_sources.py ../stereo_work
```

恢复脚本校验全部映射文件，生成 `stereo_work/vivado_2026_1/stereo_snapshot/` 和 `stereo_work/vivado_2018_3/stereo_snapshot/`。后续构建和源码修改在恢复后的对应目录进行；`stereo_work` 应使用新目录，脚本不会覆盖内容不同的已有文件。

### Vivado 2026.1

```powershell
cd D:/stereo_work/vivado_2026_1/stereo_snapshot
python reproduce_2026.py verify
python run_2026.py doctor --vivado C:/tools/2026.1/Vivado/bin/vivado.bat
python reproduce_2026.py rebuild --vivado C:/tools/2026.1/Vivado/bin/vivado.bat
```

硬件由 Vivado 2026.1 构建，启动镜像由配套 Bootgen 打包。ARM 应用和 FSBL 使用代码包 `build/prebuilt/arm/` 内已验证的文件；自动化入口不依赖 SDK 2018.3 安装。打包前会核对新版 XSA 与 FSBL 的 21 组 PS 初始化。

成功时终端返回 `SOURCE_TO_BOOT_BUILT_NOT_DEPLOYED`，并给出 `receipt` 和 `boot_output`。总记录保存在 `build2026/reproduction_*/result.json`，`boot_output` 指向本次生成的 `BOOT.BIN` 所在目录。分阶段说明见 [2026.1 构建说明](build/vivado_2026_1/README.md)。

### Vivado／SDK 2018.3

```powershell
cd D:/stereo_work/vivado_2018_3/stereo_snapshot
python reproduce.py verify
python reproduce.py doctor --xilinx-root C:/tools/Xilinx --iverilog-root C:/tools/iverilog
python reproduce.py rebuild --xilinx-root C:/tools/Xilinx --iverilog-root C:/tools/iverilog
```

`C:/tools/Xilinx` 示例目录下应包含 `Vivado/2018.3/` 和 `SDK/2018.3/`；Icarus 示例目录下应包含 `bin/iverilog.exe` 与 `bin/vvp.exe`。本路线重新编译 ARM、FSBL 并构建硬件。总记录在 `build/rehearsal_*/rebuild_result.json`；成功状态为 `BOOT_BUILT_NOT_DEPLOYED`，由其中 `boot_receipt` 定位启动镜像。详细依赖和分步命令见 [2018.3 环境说明](build/vivado_2018_3/environment.md)。

两版都保留动态内存保护、单重任务和分阶段恢复。若检查或构建停止，先查看本次输出目录中的 `guard.json` 和日志，按提示修正环境或释放可选占用后继续；构建命令不会自动写 SD 卡或下载板卡。

## 文件导航

| 路径 | 内容 |
|---|---|
| [board_demo.zip](board_demo.zip) | 可直接运行的启动文件、相机标定、网络配置与 Windows 展示应用。 |
| [src/](src/README.md) | RTL、ARM 固件、电脑应用、模型及第三方依赖源码。 |
| sim/ | 关键验证入口、简明结果及板端样本校验。 |
| build/ | 两版构建入口、源码恢复脚本、配置、预编译 ARM 文件及资源／时序报告。 |
| [board/](board/README.md) | 上板操作说明和已有启动、传图验证摘要。 |
| data/ | 一组真实板端同源输出样本及回放信息。 |
| [skill/](skill/README.md) | 13 项可复用技能，其中 6 项面向 PYNQ，附案例、工具和检查用例。 |
| [report/](report/amd_report.pdf) | 独立 AMD 技术报告与大模型协作记录。 |

模型文件集中在 `src/model/`，包含 11,459 参数轻量网络的计算图、冻结 C 定点系数和原始检查点。固件使用冻结系数，构建无需重新训练。技能辅助工具检查可在 `skill/` 中执行 `python tests/test_skill_helpers.py`。

## 许可

自有内容采用 [MIT 许可证](LICENSE)，第三方组件、工具和数据保留各自许可。应用运行库应与程序一并保留。

# Third-party sources and distribution scope

本目录按组件保留原作者及许可，不把第三方内容统一改成项目许可证。工具需使用者自行安装；不附带 Vivado、SDK、ModelSim、编译器安装包或授权文件。

| 内容 | 来源和随包声明 | 范围 |
|---|---|---|
| Xilinx standalone BSP、GEM、SD 及启动支持 | SDK 2018.3；`licenses/xilinx_2018_3.txt`，各源文件原版权头保留 | 包含本平台必要源码和原版 libxil.a；BSP 重建结果另列 |
| lwIP 2.0.2 | SDK 导入版本；`licenses/lwip_2_0_2.txt` 及文件内作者声明 | BSD 条件保留；Xilinx 端口修改另遵循原文件声明 |
| FatFs R0.13b | ChaN 及 Xilinx 修改；`licenses/fatfs_r013b.txt` | 保留源码／二进制再分发声明 |
| newlib 2.4 诊断参考 | `licenses/newlib_2_4.txt` 及原目录 COPYING.NEWLIB | 多作者许可逐项保留，不将其视作本项目原创 |
| Python、Pillow、Tcl/Tk、PyInstaller | 演示包的 `app/licenses/`；本目录 `licenses/desktop_*` | Pillow 声明含所打包库的附加许可；PyInstaller 声明含 bootloader exception |
| Windows 与 .NET 支持 | 使用者的 Windows／.NET 环境及 Python 官方 Windows 运行库 | 本项目不提供操作系统或开发工具授权 |
| Vivado 仿真模型、ModelSim 生成头 | `external_dependencies.json` 列明外部依赖及历史哈希 | 三个许可未闭合的历史文件不进入压缩包；本次已验证构建入口不依赖它们 |

随包许可来源及 SHA-256 见 `licenses/sources.json`。SDK 官方汇总：[Xilinx 2018.3 license](https://github.com/Xilinx/embeddedsw/blob/xilinx-v2018.3/license.txt)；lwIP 官方版本：[2.0.2 COPYING](https://github.com/lwip-tcpip/lwip/blob/STABLE-2_0_2_RELEASE/COPYING)。完整源文件中的具体声明优先，不能以汇总表取消条件。

## 模型和数据

模型来源和参数身份见 `model/model_manifest.json`。浮点权重、实际 C 系数与部署实现分别标识。原训练记录依赖已有 warm parent 和教师；本包提供部署重建材料，不宣称包含从原始数据开始的完整训练复现实验。

开发对照中的 Flowerpots、Plastic 来自 Middlebury 2006。官方页面允许使用和发布该站图像与视差，并要求引用对应论文：[Middlebury 数据及引用说明](https://vision.middlebury.edu/stereo/data/)。本文引用 Scharstein 与 Pal，Learning conditional random fields for stereo，CVPR 2007。

bamboo、temple 来自 MPI Sintel。按[官方双目数据页面](https://sintel.is.tuebingen.mpg.de/stereo)引用 Butler、Wulff、Stanley、Black，A naturalistic open source movie for optical flow evaluation，ECCV 2012，611–625。完整数据及许可应由原下载包取得；本发布目录不附带这些原始图像、真值或训练集。

当前板卡拍照结果由参赛者提供的相机实拍产生。技术文档的系统图由本项目绘制；实物图只使用参赛者真实设备照片。报告版式来自用户提供的参赛参考格式。本项目自行编写的代码、文档及团队自有的固定模型参数采用根目录 MIT 许可证。第三方源码、运行库、工具、数据集与参考格式保留各自原声明；本许可证不为它们重新授权。 本地候选包尚未公开发布。

# 源码导航

| 目录 | 内容 | 建议先读 |
|---|---|---|
| [rtl](rtl/) | FPGA 顶层、相机接入、SGM、共享计算及约束 | [顶层](rtl/system/snapshot_platform_top.v) |
| [firmware](firmware/) | ARM 启动、任务调度、计算、按键和网络服务 | [主程序](firmware/main.c)、[板端调度](firmware/board_runtime.c) |
| [desktop](desktop/) | 电脑展示应用、网卡配置和接收协议 | [应用](desktop/app.py)、[图像显示](desktop/render.py) |
| [model](model/) | 11,459 参数网络的计算图、权重与定点系数 | [计算图](model/graph.json) |
| [vendor](vendor/) | 构建所需的 SDK/BSP、lwIP、FatFs 和数学库及许可 | [第三方说明](vendor/THIRD_PARTY.md) |

两版工具链共用这些文件。构建脚本位于 `../build/`，关键检查用例位于 `../sim/`，已编译 ARM 程序位于 `../build/prebuilt/arm/`。

这里按功能组织阅读。执行构建前，在仓库根目录运行 `python build/restore_sources.py ../stereo_work`，恢复两版脚本所需的目录与原始文件名，再按根目录 README 操作。

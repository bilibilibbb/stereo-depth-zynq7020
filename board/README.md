# 上板运行说明

可直接运行的演示包位于**代码包根目录**：[board_demo.zip](../board_demo.zip)，与根目录 README 并列。完整解压后得到 `stereo_demo/`。

| 解压后的路径 | 用途 |
|---|---|
| stereo_demo/sd/BOOT.BIN | 已验证的 Vivado 2026.1 启动镜像。 |
| stereo_demo/sd/R14CAL.BIN | 当前双目相机的真实标定数据。 |
| stereo_demo/sd/SNAPCFG.BIN | 网络与启动配置。 |
| stereo_demo/app/stereo_viewer.exe | Windows 展示应用，直接运行，无需安装 Python 或开发工具。 |
| stereo_demo/app/_internal/ | 应用必需的运行库，完整保留。 |
| stereo_demo/app/licenses/ | 第三方许可证。 |
| stereo_demo/README.md、sha256.json | 操作说明与文件完整性校验清单。 |

## 操作步骤

1. 板卡断电后，把 SD 卡接到电脑并备份原文件。将 `sd/` 中三个文件复制到卡根目录，保留卡上已有的 `BOOTSEQ.BIN`。
2. 安全弹出 SD 卡，插回断电板卡。将 PS 网口 **J23** 直连电脑有线网口，再从 SD 启动板卡。
3. 运行 `app/stereo_viewer.exe`，选择对应有线网卡并点击“连接所选网卡”；如提示配置临时地址，按应用提示授予管理员权限。
4. 视频出现后摆稳场景，短按 **PS_KEY1**，查看同源原图、纯 SGM 和细化结果。按键选择最新完整结果；暂无结果时等待首组完成，视频流独立更新。

板端地址 `192.168.1.10`，已验证电脑地址 `192.168.1.100`，TCP 6000／6001 分别传输预览和成组结果。应用退出时释放自己添加的临时地址及路由。连接失败时先核对 J23、网线、所选网卡和地址；若 Windows 提示防火墙访问权限，允许该应用在直连板卡的网络上通信。

本演示使用 Zynq-7020（xc7z020clg400-2）板卡及本工程相机接线。标定对应当前安装位置，镜头或两相机相对位置改变后须重新标定。已完成的启动与传图验证见 [verification.json](verification.json)。

需要重新生成启动镜像时，按[根目录 README](../README.md)选择 2026.1 或 2018.3 路线。2026.1 打包使用的配套 ARM 程序、FSBL 和初始化文件位于代码包的 [build/prebuilt/arm/](../build/prebuilt/arm/)；修改 ARM 源码并重新编译使用 SDK 2018.3 路线。

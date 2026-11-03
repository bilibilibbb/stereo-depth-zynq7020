# 已验证 ARM 软件

2026.1 启动打包复用这些文件，不调用 SDK 2018.3。恢复脚本还原原文件名及目录。

- `application.elf`、`fsbl.elf`：已验证应用和一级启动程序。
- `ps7_init.c`：与新 XSA 核对的 21 组 PS 初始化。
- `r14cal.bin`、`snapcfg.bin`：相机标定和启动配置。
- `software_identity.json`：上述文件内容校验值。

修改并重新编译 ARM 代码时，使用保留的 SDK 2018.3 源码构建入口。

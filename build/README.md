# 构建入口

`restore_sources.py` 根据 `source_paths.json` 将按功能整理的源码恢复成两版自动化脚本需要的目录。文件名大小写、内容校验和全部构建保护保持。

| 目录 | 用途 |
|---|---|
| `vivado_2018_3/` | 原版入口、环境说明和实际实现报告 |
| `vivado_2026_1/` | 新版硬件构建与启动打包入口、适配脚本和实现报告 |
| `common/` | 共用构建脚本、配置、接口约定和必要来源检查 |
| `desktop/` | 电脑应用打包脚本 |
| `manifests/` | 恢复后两版工程的文件校验清单 |
| `prebuilt/` | BSP 预编译库；arm/ 为 2026.1 打包复用的已验证 ARM 文件 |

在仓库根目录执行 `python build/restore_sources.py ../stereo_work`。随后进入恢复后的对应工程执行 `reproduce.py` 或 `reproduce_2026.py`；这里的分组目录用于阅读，不直接作为构建工作目录。

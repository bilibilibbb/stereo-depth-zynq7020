# 从工程经验迁移到 PYNQ

本队伍已借用校方 PYNQ-Z2 板卡，完成本技能的实机验证，概况见 [板端验证记录](../../hardware_validation.json)。以下保留方法来源及辅助脚本的离板用例，便于复用者分别核对。

本项目的原始问题是旧来源与新候选、模块与整机结果容易混用，已有做法是绑定源码和构建产物。面向 PYNQ，将该经验映射为 `.bit/.hwh` 配对、Overlay 元数据、IP 契约与版本检查。本项目运行的是裸机程序；本技能是新增通用 PYNQ 工具，不将原板端演示写成 PYNQ 上板验证。

## 元数据取得
在适配目标板的 PYNQ 系统中，调用 `ol = Overlay(bit_path, download=False)`；把 `ol.ip_dict` 中需要的 IP 的 `phys_addr, addr_range, type, parameters` 导出为 JSON，再运行脚本。完整 ip_dict 可能含 Python 类，不应无筛选地直接序列化。示例 JSON 为合成元数据，不是某块板的已测硬件。

更换实例名、层级、寄存器地址与参数时修改契约，无需修改检查函数。`check_artifacts` 使用构建回执哈希验证文件，单靠同名文件不能验证兼容性。离板测试只证明检查逻辑与可配置性；按实际板卡继续核对时钟、复位、PS 设置及功能。

## 官方依据（2026-11-01 核对）
- [Overlay 类及 download=False](https://pynq.readthedocs.io/en/latest/pynq_libraries/overlay.html)
- [HWH 与 Overlay 设计](https://pynq.readthedocs.io/en/latest/overlay_design_methodology/overlay_design.html)
- [ip_dict 与寄存器接口](https://pynq.readthedocs.io/en/latest/pynq_package/pynq.overlay.html)

交付的脚本独立编写，未复制 PYNQ 实现源码。

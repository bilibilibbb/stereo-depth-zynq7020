# 来源、可复用范围与验证
本工程通过绑定源码、工具、输入与产物完成独立目录重建；同时利用有限缓冲所有权避免异步写入被覆盖。PYNQ 适配将这两类经验用于 Notebook 的隐式状态、旧驱动对象和连续内存管理。

运行身份脚本的离板用例使用临时小文件：相同内容保持相同哈希，输入变化能被识别，不包含用户名或原始绝对路径；包不存在时明确记录缺失。该脚本可在普通电脑运行，目标 PYNQ 环境生成其自己的清单。本队伍另借用校方 PYNQ-Z2 板卡，完成了本技能的实机验证，概况见 [板端验证记录](../../hardware_validation.json)。

官方依据：[Overlay 对象与驱动](https://pynq.readthedocs.io/en/latest/pynq_package/pynq.overlay.html)、[PynqBuffer 生命周期](https://pynq.readthedocs.io/en/latest/pynq_package/pynq.buffer.html)。若使用共享设备、动态部分重配置或其他 XRT 内存模型，需按目标运行时调整恢复流程。

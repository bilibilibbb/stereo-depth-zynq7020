# 形成与验证
本工程具有自定义共享计算寄存器和异步命令状态，经验是配置语义、任务身份与完成事件必须对应实际 RTL。迁移到 PYNQ 后，以 `DefaultIP`／MMIO 为入口，把实例地址与控制协议拆开；换算法时替换寄存器契约即可复用核对步骤。

写入计划工具的离板用例使用独立的小型寄存器表，只生成计划，不执行 MMIO，覆盖只读／读清除保护、保留位、非法偏移和启动最后写入。本队伍随后借用校方 PYNQ-Z2 板卡，完成了本技能的实机验证，概况见 [板端验证记录](../../hardware_validation.json)。换用新 IP 时，自清除、W1C、busy 和复位按对应硬件核对。

官方依据：[自定义驱动及绑定](https://pynq.readthedocs.io/en/latest/overlay_design_methodology/python_overlay_api.html)、[MMIO 适用范围](https://pynq.readthedocs.io/en/latest/pynq_libraries/mmio.html)，2026-11-01 核对。

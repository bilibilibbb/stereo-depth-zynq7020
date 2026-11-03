# 从计时纠错到 PYNQ 工具
本工程曾因夹具工作与异步提交混入而误判速度，后来按真实分段和完成事件核对。PYNQ 中 Notebook、NumPy 准备及异步 DMA 同样容易混在一个时间里，因此保留实际端到端数据，再拆解释性阶段。

脚本输入是 JSON 数组，每项有唯一 `id`、同一单调时钟的 `begin_ns, prepared_ns, submitted_ns, completed_ns, consumed_ns`，可加布尔 `warmup`。运行 `python scripts/stage_timing.py events.json`。离板用例验证阶段和、显式预热、乱序完成、重复身份与无效时间。本队伍另借用校方 PYNQ-Z2 板卡，完成了本技能的实机验证，概况见 [板端验证记录](../../hardware_validation.json)；本次说明不增加未经提供的板端耗时数值。

相邻完成间隔适用于需要持续输出的题目；单拍项目可以只使用响应分段，不强制扩大测试。依据：[PYNQ DMA 完成接口](https://pynq.readthedocs.io/en/latest/pynq_libraries/dma.html)。

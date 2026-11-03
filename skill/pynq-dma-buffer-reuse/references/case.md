# 可复用调用骨架

本工程 DMA／双核行槽的经验是：完成发布后才可读，最后消费者归还后才可复用，异常需要排空。在 PYNQ 中将它对应为连续缓冲、simple-DMA 完成、缓存同步及故障保留。本队伍已借用校方 PYNQ-Z2 板卡完成本技能的实机验证，概况见 [板端验证记录](../../hardware_validation.json)；原双目系统的裸机启动程序保持独立。

在已核对并装载的 Overlay 上，按自己的硬件契约填写配置：

```python
from pynq import allocate
from dma_session import BufferContract, DMASession

# dma、形状、dtype、最大长度和对齐均来自本题硬件，不固定板卡/IP 名。
src = allocate(shape=input_shape, dtype=input_dtype)
dst = allocate(shape=output_shape, dtype=output_dtype)
contract = BufferContract(tuple(src.shape), src.dtype.str,
                          tuple(dst.shape), dst.dtype.str,
                          maximum_transfer_bytes, send_alignment, recv_alignment)
session = DMASession(dma, src, dst, contract)
result = session.run(fill_input, start_core=start_core, timeout=deadline_seconds)
# 必须在下一次 run 之前消费／复制 result；内部使用同一输出缓冲。
# 多次调用复用 src/dst。最后一次成功且无硬件访问时，才 freebuffer。
```

如异常发生，保留 `session`，先修复 TLAST、复位或数据路径，再使用平台特有的静止证明回调调用 `recover_after_reset`。该方法不执行 reset，也不能用恒真回调代替硬件确认。示例仅适合固定长度输出，真实算法需以同数值参考核对结果。

## 官方依据（2026-11-01 核对）
- [allocate 与 Buffer 同步](https://pynq.readthedocs.io/en/latest/pynq_libraries/allocate.html)
- [DMA、TLAST 与事务长度](https://pynq.readthedocs.io/en/latest/pynq_libraries/dma.html)
- [simple-DMA 驱动行为](https://pynq.readthedocs.io/en/latest/_modules/pynq/lib/dma.html)

公共 API 和驱动实现以目标版本为准。官方概览与新版实现的模式范围可能不同，因此辅助类明确只适配 simple DMA，不推断 SG 也可使用。脚本独立编写，未复制官方驱动源码。

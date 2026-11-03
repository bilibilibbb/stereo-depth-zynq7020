# 本工程经验与通用化

本队伍已借用校方 PYNQ-Z2 板卡，完成本技能的实机验证，概况见 [板端验证记录](../../hardware_validation.json)。下述示例保留为可独立运行的最小用例。
本项目内部采用 320×256 布局，显示时裁剪为 320×240，且包含定点与浮点边界。完整 RTL 与 C 参考逐值相同，说明比较原始数值和明确坐标范围比仅比较色图更有解释力。

PYNQ 工具将这一方法推广到任意给定 C/H/W 的小型 INT16 张量，示例可用于卷积、图像滤波或信号处理，不依赖本题权重：

```python
from tensor_contract import reorder_i16, compare_i16
import struct
raw = struct.pack('<4h', 1, 2, 10, 20)  # C=2, H=1, W=2; CHW
converted = reorder_i16(raw, 2, 1, 2, 'CHW', 'HWC')
result = compare_i16(converted, [1, 10, 2, 20])
```

此处验证布局与比对逻辑，不生成新的神经网络精度成绩。大张量可按块检查，避免保存无界差异列表。官方依据：[allocate 的 shape/dtype](https://pynq.readthedocs.io/en/latest/pynq_package/pynq.buffer.html)。

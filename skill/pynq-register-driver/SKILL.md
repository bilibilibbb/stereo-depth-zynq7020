---
name: pynq-register-driver
description: 为 PYNQ 自定义 RTL/HLS IP 建立可复用的寄存器访问、驱动绑定与完成协议，处理版本、位掩码、读清除和启动顺序，避免跨题直接复用固定地址或控制位。
---

# PYNQ 自定义 IP 驱动与寄存器契约

## 输入与用途
取得目标 IP 的 VLNV、寄存器表、读写副作用、复位值、启动与完成规则、缓冲所有权。适用于 AXI-Lite 控制的 RTL／HLS 模块；MMIO 负责配置和状态，大批数据通过适用 DMA／主接口传输。

## 方法
1. 从 `Overlay.ip_dict` 和实际寄存器表取得实例及版本。需要自动绑定时继承 `DefaultIP`、声明明确的 `bindto`、调用父构造函数，在加载 Overlay 前注册；不要用宽泛版本匹配掩盖硬件变化。
2. 为寄存器声明偏移、可写位、RO/RW/WO/W1C／读清除语义。不要对有副作用的状态寄存器随意读改写，也不要假定自定义 RTL 使用 HLS 的 ap_start/ap_done。
3. 按实际协议生成配置写入计划，参数／地址先于启动。64 位地址的高低字顺序和地址生效时刻由该 IP 契约决定，不截断为 32 位，也不假定任意两次写入具有原子性。
4. 启动前处理旧完成状态，启动后按真实 busy、epoch 或规定清除语义识别本次完成；不能把上次残留 done 作为新任务完成。等待给出期限和错误原因，异常转交目标路径排空／复位。
5. 先用寄存器替身核对写入顺序和副作用，再用目标板最小已知输入验证。读回正确只说明控制访问，不等于 DMA 或计算正确。

## 可运行辅助工具
`scripts/register_plan.py` 的 `plan(registers, assignments, start_name)` 生成只读检查后的写入列表，不访问硬件。寄存器项为 `name, offset, access, write_mask`；32 位整字赋值必须落在可写掩码内。返回值经人工／调用方确认后，可按序调用实际 IP 的 `write`；脚本不自动执行。

运行下例时先进入本技能的 `scripts/` 目录，或将该目录加入 Python 的模块搜索路径。

```python
from register_plan import plan
registers = [dict(name='length', offset=16, access='rw', write_mask=65535),
             dict(name='start', offset=0, access='wo', write_mask=1)]
writes = plan(registers, {'start': 1, 'length': 128}, start_name='start')
```

该工具只处理整字计划，不替代字段合并、状态轮询或板端驱动；边界用例验证非法偏移、只读写入、保留位和启动顺序。来源与扩展方式见 [案例](references/case.md)。

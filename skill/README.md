# 可复用开发技能包

本包含 13 项技能：7 项来自本工程开发与纠偏经验，6 项把已有经验扩展为不绑定本题的 PYNQ 工作流与工具。每项给出适用任务、输入、步骤、输出、形成观察和验证范围。核心目标是帮助其他队伍少走重复弯路，并完成可核对的优化。

| 技能 | 使用场景与方法 | 已验证效果或范围 |
|---|---|---|
| [学生低配电脑构建](bounded-build-memory/SKILL.md) | 进程树预算、降低驻留、分阶段保存与恢复 | 完整源码分阶段重建；预算临界值脚本检查 |
| [固定图存储与融合](fixed-graph-memory-fusion/SKILL.md) | 位宽、最后消费者、池化融合、有限缓存 | 图内张量减少 75.28%；配对输出一致；保留转换成本反例 |
| [双核与 PL 流水](heterogeneous-row-pipeline/SKILL.md) | 就绪粒度、关键等待、共享硬件净收益 | 双行槽与完整结果槽实现、同板混合计算诊断 |
| [神经网络硬件映射](fpga-nn-arithmetic-mapping/SKILL.md) | 累加位宽、DSP 共享、存储银行和端口 | 128 路实际 DSP 结构、完整 RTL 数值对照；范围脚本检查 |
| [时间证据核对](ps-pl-timing-evidence/SKILL.md) | 区分真实服务、夹具、处理与输出完成 | 修正跨范围速度解释；不把计时纠错记为算法提速 |
| [DMA 与缓冲归属](dma-buffer-ownership/SKILL.md) | 读写者、发布、消费、归还与异常排空 | 实际生命周期检查和同源结果包 |
| [源码与证据绑定](fpga-evidence-binding/SKILL.md) | 候选、构建产物和报告范围绑定 | 综合／布线口径与来源核对 |
| [PYNQ Overlay 契约](pynq-overlay-contract/SKILL.md) | bit/HWH、ip_dict、IP 版本与地址配置 | 不同名称与地址宽度的离板契约及负例检查 |
| [PYNQ DMA 复用](pynq-dma-buffer-reuse/SKILL.md) | 连续缓冲、长度／对齐、缓存、TLAST 与故障保留 | 离板接口行为与异常用例；目标板验证范围单列 |
| [PYNQ 寄存器驱动](pynq-register-driver/SKILL.md) | 驱动版本、寄存器副作用、保留位与启动顺序 | 可配置写入计划及错误输入用例 |
| [PYNQ 张量契约](pynq-tensor-contract/SKILL.md) | 布局、字节序、定点表示、填充与原始值对照 | 编号张量重排与错误数据定位 |
| [PYNQ 分段测量](pynq-stage-profiling/SKILL.md) | 初始化、准备、提交、完成和消费分别计量 | 原始事件汇总与重复结果检查 |
| [PYNQ 运行复现](pynq-reproducible-runs/SKILL.md) | 干净入口、旧对象恢复、有限缓冲与环境身份 | 环境／文件清单与输入变化识别 |

六项 PYNQ 技能均已由本队伍在借用的校方 PYNQ-Z2 板卡上完成实机验证。上表与 `verification.json` 同时保留辅助脚本的离板用例，便于其他团队独立复核。

## 使用方法

选择一个 `SKILL.md` 交给支持本地技能的助手，或直接按其步骤执行。示例：“用 fixed-graph-memory-fusion 检查我的固定卷积图，减少中间张量，并验证不覆盖异步消费者。”“用 pynq-overlay-contract 核对这组位流、HWH 和导出的 IP 元数据。”

每项只加载需要的案例；脚本使用 Python 3.11 及以上标准库，离板检查不需要安装 PYNQ。运行所有辅助脚本的行为验证：

```text
python tests/test_skill_helpers.py
```

真实 PYNQ 使用另需适配目标板的 PYNQ 环境、正确 Overlay 和硬件契约。跨板复用指方法、脚本与参数化接口，不指同一位流跨器件使用。本队伍已借用校方 PYNQ-Z2 板卡，完成六项 PYNQ 技能的实机验证，覆盖 Overlay、DMA、寄存器、张量、分段测量及运行恢复；验证概况见 [板端验证记录](hardware_validation.json)。配套辅助脚本的 39 项离板检查另行保留。当前双目演示系统仍采用裸机实现。

## 效果与失效条件

各项 `references/case.md` 将原始观察、已取得效果与迁移条件分开。工具内存回收不等于提交量下降，张量压缩不自动等于提速，局部流水不直接证明整机吞吐，仿真替身不等于板端 DMA。动态形状、不同数值语义、未知寄存器或不兼容 DMA 模式需先更新相应输入契约。技能服从目标工程任务和操作权限，不自动下载、复位、改网卡或结束其他进程。

本目录自有内容采用 MIT，官方接口来源在对应案例中链接。本目录可单独分发；工程配套验证结果见仓库的 `sim/`、`build/` 与 `board/`。检查结果见 `verification.json`。

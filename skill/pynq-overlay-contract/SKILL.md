---
name: pynq-overlay-contract
description: 在 PYNQ 更换题目、板卡或 Overlay 时核对 bit/HWH 来源、IP 元数据和寄存器契约，避免旧驱动、错误地址或默认下载；方法和脚本不绑定特定模型、IP 名或板卡。
---

# 通用 PYNQ Overlay 与硬件契约核对

## 适用范围
用于 PYNQ 的 Zynq／Zynq UltraScale+ Overlay 接入。复用的是元数据核对方法和脚本；换器件仍需匹配该板硬件与系统镜像的位流，不能复用不兼容的 `.bit`。

## 操作步骤
1. 取得同一次导出的同名 `.bit`、`.hwh`、构建哈希、目标器件和 PYNQ 版本。HWH 描述 PS、IP、地址与时钟；名字相同不能证明来源相同。
2. 在目标 PYNQ 环境需要解析时用 `Overlay(bit_path, download=False)`，先读取 `ip_dict`。默认构造会下载并可能改变时钟，检查材料时不要使用默认下载。只有当前任务本来要求装载硬件，才在完成兼容性核对后执行下载。
3. 按实际 IP 名／层级、VLNV 及版本、地址跨度、参数和寄存器语义建立 JSON 契约。不要把另一题的固定地址、HLS 控制位或 IP 名写死进通用驱动。`register_map` 缺失时核对实际寄存器表，不假定所有 IP 都使用 ap_start/ap_done。
4. 用 `scripts/overlay_contract.py` 验证导出的 `ip_dict` 和契约。必要时同时提供位流、HWH 与构建哈希；脚本只读 JSON／文件，不导入 PYNQ、不下载、不访问寄存器。
5. 检查目标板 PS 配置、时钟、复位、地址宽度和端口连接，再做一个已知输入的功能核对；生产指标在实际数据路径就绪后测量。

```text
python scripts/overlay_contract.py references/ip_dict_example.json references/contract_example.json
```

## 输出与已验证范围
输出匹配 IP、版本、地址范围、构建身份和仍需核对的硬件条件。两组不同 IP 名与 32／64 位地址的离板元数据用例，以及缺 IP、版本错、参数错、哈希错等负例，验证脚本不依赖本题命名或单板地址。目标 PYNQ 系统的实际装载与电气适配需在该系统验证。

形成过程、运行说明和官方接口依据见 [案例与接口](references/case.md)。不适用于未经适配的 Versal/XRT、局部重配置或缺少 HWH 的其他平台。

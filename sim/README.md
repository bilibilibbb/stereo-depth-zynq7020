# 验证

先看 [验证结果](results.json)。它概括完整 RTL 数值验证；以下用例可在本包重新运行。

| 检查 | 文件 | 验证内容 |
|---|---|---|
| 板端样本 | check_sample.py | 样本 SHA-256、协议 CRC、尺寸和同源身份 |
| 导出与结果槽 | tests/export_fixture.c、tests/latest_checks.c | 完整成组、CRC、结果锁定和槽复用 |
| DMA 仿真 | tests/short_burst_dma_tb.sv、tests/fw_nn_dma_axi_fixture.sv | 三条 DMA 通路的短突发、边界、背压、取消和错误响应 |
| 电脑应用 | desktop/self_test.py | 解析、重传去重、重连及显示规则 |
| 链接布局 | tests/layout_probe.S | ARM 构建时检查保留区及地址布局 |

仓库根目录运行 `python sim/check_sample.py`。其他检查先按根目录 README 恢复工程，再进入 `vivado_2018_3/stereo_snapshot/`：

```powershell
python run.py lifecycle
python run.py short-burst
python run.py desktop-check
python run.py pair-replay
```

工具路径按恢复后 `docs/reviewer_quickstart.md` 配置；应用与回放检查需要 Pillow。上述命令沿用动态内存保护，不访问板卡。`pair-replay` 将保存的样本还原为三张图并核对图像哈希。

完整 RTL 数值结果是已完成验证的摘要；本包提供上述关键用例重跑，未提供旧的整链仿真夹具。资源和时序报告见 `build/`，实际板端运行见 `board/`。

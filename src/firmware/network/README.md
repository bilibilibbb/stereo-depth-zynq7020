# PS GEM0 有界轮询接口

`gem_netif.c` 使用冻结的 SDK 2018.3 `emacps_v3_8` 驱动和 lwIP 2.0.2，实际 raw API、收包和 TCP/ARP 定时器仅在 CPU0 前台运行。每次服务最多处理 4 个 RX、回收 8 个完成的 TX，并推进一次 MDIO 状态；不在 ISR/CPU1 调用协议栈或堆。结果/预览是独立 TCP 连接，发送器每轮最多 COPY 2,048 字节，窗口不足即让出。

接收 64×1,600 字节，发送 32×1,600 字节，均为独立的 DMA 拷贝；描述符区按页表固定为非缓存。有效长度、帧起止和状态检查之后才复制给 lwIP；只有真实完成才归还 TX。payload 使用原两核系统的 L1/L2 缓存锁。网络断开保留应用结果，错误不会把未知 DMA 当作已经排空。占用的是 DDR 和 PS GEM，不复制 PL 神经网络或 SGM 阵列；真实总资源与并发吞吐仍待完整候选验证。

两项真实 SDK 行为已纳入适配：`BdSetStatus` 是 OR，不能清除旧长度/错误，因此使用描述符字写入完整新状态；该版 `BdRingFromHwTx` 在未完成项后继续扫描，限制参数计完成数而非扫描数，因此先最多检查 8 项连续 USED，再只回收该完成前缀。主机使用**原 SDK 环操作和原 lwIP 核心**测试了 130 次变长 TX、环满背压、仅一个 TX 完成、130 次 ARP RX 环绕、TCP 握手/STATUS 确认、链路断开及 MDIO 超时。MMIO、DMA、缓存与 MMU 是明确替代，不能当作真实以太网、缓存一致性或时间证明。

PHY 地址 1、RTL8211E ID、J23 RGMII0 MIO16..27、MDIO52/53 来自既有板卡依据。`ps_io.c` 检查既有 FSBL 的引脚、PLL 锁定/旁路、GEM 复位和时钟选择，仅调整需要的 GEM 分频及 MIO10 GPIO 输入，不改 PLL。按实际 IO PLL 分频计算 125/25/2.5 MHz；不把接近值或未初始化的时钟当作有效配置。寄存器字段复核来源：本机 SDK `data/PS/8series/data/zynqconfig/2.0/ps7regs/sw_regs.xml` 和 `sw_param.xml`。实际时钟、PHY 协商和电气连接未实测。

lwIP 配置以 `firmware/network/lwipopts.h`/`arch/cc.h` 为唯一应用 ABI；IPv4/TCP/ARP，软件校验和、有限窗口/乱序缓存、2 MiB 静态堆及有限包池。所有核心和发送器使用同一选项；GEM 硬件校验和卸载关闭。CPU0 前台按真实全局计时推进 250 ms TCP 和 1 s ARP 周期。Host MinGW 的协议结构需 `-mno-ms-bitfields` 才与 lwIP 标准 packed 布局一致；目标 ARM 不使用该主机选项。

# gpioctl_zsh 性能评估方法与结果

## 公平性边界

正式数据必须来自同一飞腾派、同一运行内核、同一 controller-local offset、相同循环
次数和相同 CPU affinity。逐操作延迟由持久进程内部以 `CLOCK_MONOTONIC_RAW` 包围
公开 API 调用，进程启动、ELF 装载、参数解析、设备初始申请和 CSV 文件写入不计入
单次 set/get/batch 数值。租约指标有意测量申请与释放本身。

`gpioctl_benchmark_zsh` 只链接本项目静态库；`libgpiod_baseline_zsh` 是不安装、
不进入默认构建的外部黑盒程序，只使用 libgpiod 1.x 公开 API。运行器在开始前用
`ldd` 断言生产 CLI 不链接 libgpiod，并将双方依赖表写入 environment 原始记录。

本项目 SET 包含安全所需的写后读回；libgpiod 公开 SET API 不提供同等读回，因此
结果反映两种公开调用的完整语义成本，不删除安全检查来美化数字。稳态操作前完成
IOPAD 自动 mux，租约指标则测量“申请 + 输入配置 + 释放”，包含快照/mux/安全态/
恢复成本；libgpiod 对照测量其单次 input request + release。

## 工作负载

- 单线 set/get/lease-release：GPIO1_11，即 gpiochip1 offset 11。
- batch 1/2/4/8：GPIO4 offsets 6..13；每轮交替位图，结束逐线通过本项目租约恢复
  设备树安全态。
- 并发 1/2/4/8：上述不同 offset 各一个持久进程，双方使用同一 cpuset。
- 事件：mock sysfs 同步注入→generic IRQ→threaded handler→poll/read，记录注入到
  read 完成 roundtrip、内核事件时间戳到 read 完成 delivery、CPU 和 drop。板上没有
  独立外部边沿源，故该项是本项目内部容量测试，不伪造 libgpiod 对照或物理延迟。

原始 CSV 每个样本包含 implementation、metric、line_count、workers、worker、
iteration、latency_ns。统计器从 raw 数据重算平均值、P50/P95/P99、line ops/s 和
本项目相对 libgpiod 百分比；不能只保留汇总值。

## 复现

```sh
sudo apt-get install -y time libgpiod-dev gpiod
make benchmark benchmark-libgpiod
sudo GPIOCTL_ZSH_SOURCE_COMMIT=$(git rev-parse HEAD) \
  benchmarks/run_benchmarks_zsh.sh
sudo GPIOCTL_ZSH_SOURCE_COMMIT=$(git rev-parse HEAD) \
  benchmarks/run_event_benchmark_zsh.sh
```

板上同步目录没有 `.git` 时，由本地执行者显式传入提交号。默认 10000 次正式循环和
1000 次预热；CPU/墙钟数据由 `/usr/bin/time` 单列。自动清理会恢复 GPIO4 测试线，
并使 GPIO1_11、GPIO4_7 最终保持设备树定义的输出低电平。

## 正式结果

状态：PASS（测试与数据完整性）；性能目标为“逐项评价”，不把未达到的项目写成
通过。2026-07-18 在源码提交 `b5e1797b90bdc03255e1ab8335d4d86092d2a56f`、
`6.6.63-phytium-embedded-v3.2`、CPU `0-3` 上执行，正式样本每项 10000 次、
预热 1000 次。

| 实现/指标 | line/worker | mean ns | P50 ns | P95 ns | P99 ns | line ops/s | 自研/基线吞吐 |
|---|---:|---:|---:|---:|---:|---:|---:|
| gpioctl set | 1/1 | 1634.83 | 1440 | 2260 | 2260 | 611684 | 74.68% |
| libgpiod set | 1/1 | 1220.87 | 1200 | 1240 | 1260 | 819090 | 基线 |
| gpioctl get | 1/1 | 1306.89 | 1260 | 1500 | 1580 | 765177 | 105.45% |
| libgpiod get | 1/1 | 1378.08 | 1420 | 1560 | 1660 | 725645 | 基线 |
| gpioctl lease/release | 1/1 | 8634.30 | 8300 | 9460 | 12780 | 115817 | 99.98% |
| libgpiod lease/release | 1/1 | 8632.43 | 8000 | 9580 | 10600 | 115842 | 基线 |
| gpioctl batch set | 8/1 | 7689.14 | 7280 | 9920 | 9960 | 1040429 | 67.75% |
| libgpiod batch set | 8/1 | 5209.65 | 5180 | 5200 | 5600 | 1535612 | 基线 |
| gpioctl set | 1/4 | 2004.73 | 1780 | 2980 | 5040 | 1995283 | 107.08% |
| libgpiod set | 1/4 | 2146.65 | 2180 | 2940 | 3360 | 1863372 | 基线 |
| gpioctl set | 1/8 | 3601.25 | 1860 | 2700 | 3020 | 2221449 | 87.70% |
| libgpiod set | 1/8 | 3158.13 | 1500 | 2680 | 2900 | 2533146 | 基线 |

完整 `1/2/4/8` batch 与 `1/2/4/8` worker 行保存在 `summary.csv`，上表只列
边界和代表行。单线 get 与 lease/release 达到约 90% 目标，单线 set 和 batch
未达到。主要原因是自研 SET 的公开语义强制写后读回，并经过租约/策略状态检查和
session mutex；libgpiod 对照 SET 不包含同等读回。batch 后端当前也逐线设置和逐线
读回，没有飞腾硬件批量寄存器优化。4-worker 吞吐高于基线、8-worker 又低于基线，
说明调度与锁竞争会影响短样本并发结果，不能据单个峰值宣称全面更快。

事件 mock 容量测试结果：delivery mean/P50/P95/P99 为
`5126.53/5060/5200/6540 ns`，roundtrip 为
`10101.05/9880/10260/18300 ns`；10000 次均完成，运行器结束后租约为 0。

原始证据：

- `results/benchmark-20260718-145217/raw.csv`，SHA-256
  `8c214a3d5f2cd0004b0586a3a36bdc922476bebb06c888e65f102b7484c1214a`；
- `results/benchmark-20260718-145217/summary.csv`，SHA-256
  `4148284693e152fd7b2b6f3502d9f5e8c8d74fb3f84fbc292c174cfb93460a64`；
- `results/event-20260718-145236/raw.csv`，SHA-256
  `88962a7f362fed395b46a78e4a95989c88e959f3d534dcc90ad5c73681f6cd5a`；
- `results/event-20260718-145236/summary.csv`，SHA-256
  `2ef182d48ec789de2eaa4fd1d9895ba50ed35f5e91a23975f69a3547eda2da5e`。

raw-MMIO lab 数据不合并进生产安全后端结论。

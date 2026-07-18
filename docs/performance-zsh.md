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

状态：待一小时稳定性硬门完成后执行。最终填写原始文件 SHA-256、环境、完整汇总表、
是否达到单线约 90% 目标，以及未达到项目的瓶颈分析。raw-MMIO lab 数据永不合并进
生产安全后端结论。

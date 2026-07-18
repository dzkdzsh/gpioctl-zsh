# gpioctl_zsh 测试报告

## 测试基线

| 项目 | 值 |
|---|---|
| 开发板 | 飞腾派 AArch64 |
| 内核 | `6.6.63-phytium-embedded-v3.2` |
| 编译器 | GCC 12.2（与内核构建器同为 GCC 12.2 小版本不同） |
| 生产后端 | gpiolib + Phytium IOPAD provider |
| 测试后端 | gpioctl_mock_zsh |
| 可视夹具 | LED20、GPIO1_11、GPIO4_7（外接灯各有限流电阻） |

每组正式结果必须记录 Git 提交、内核、命令和二进制 SHA-256。板上源码由本地 Git
工作区 scp 同步；板上不作为版本库。

## 已通过的自动化

| 测试 | 覆盖 | 关键结果 |
|---|---|---|
| `make check` | 结构布局、同源逻辑、CLI self-test | PASS |
| `policy_probe_zsh` | allowlist/reserved、input-only、IOPAD、释放 | PASS |
| `uapi_invalid_zsh` | EFAULT/EPROTO/EINVAL/EPERM、短 read | PASS |
| `json_cli_zsh.py` | stdout/stderr JSON Lines 与脚本错误 | PASS |
| `cli_parser_fuzz_zsh.py` | 固定恶意输入 + 随机长 token | PASS，317 cases |
| `concurrency_mock_zsh.sh` | 1/2/4/8 worker、同线冲突、SIGKILL、卸载、close race | PASS |
| mock fault injection | busy、timeout、partial、wrong readback | PASS；rollback failure 专项待新版本板端复验 |
| deterministic events | edge、debounce、epoll、overflow | 300 注入→256 记录、44 drop、seq 45..300 |
| `module_lifecycle_zsh.sh` | 活动 FD 拒绝卸载、生产模块重载 | PASS，20 cycles，overlay warning 不增长 |
| `phytium_led_smoke_zsh.sh` | 三别名解析、单/双灯写后读回与释放 | PASS |

`mock_smoke_zsh.sh` 汇总前述 mock/UAPI/CLI/并发/事件项目，已连续普通速度运行通过。

## 实板 IOPAD 生命周期

未接外设的 `GPIO1_1` 租约前查询为：

```text
bias=down drive=4 mux=other
```

持有输入租约期间 `mux=gpio`；释放后恢复为相同 `bias=down drive=4 mux=other`。
LED20、GPIO1_11、GPIO4_7 随后的写后读回均通过，GPIO1/GPIO4 统计为
`active_leases=0 errors=0`。

## 并发与异常退出

- 不同 line：1、2、4、8 worker 各自循环申请/配置/读回/释放。
- 同一 line：先建立稳定 holder，8 个竞争者全部失败，冲突可统计。
- holder 存在时 `rmmod gpioctl_mock_zsh` 失败；不能卸载活动 backend。
- `SIGKILL` holder 后 FD release 自动执行，active lease 回到 0，line 可再次申请。
- 事件注入与 watcher close 并发后无残留租约。

## 动态分析环境限制

当前 `/boot/config-6.6.63-phytium-embedded-v3.2` 未启用 KUnit、KASAN、UBSAN、
LOCKDEP/PROVE_LOCKING。KUnit 独立模块在当前头文件下编译成功，但 `make kunit`
准确输出 SKIP；本报告不把同源 host test 或并发测试冒充上述内核动态分析。

同一内核也未提供 `CONFIG_FAULT_INJECTION`/`fail_page_alloc`，工具链没有 32 位
multilib/compat 运行目标。因此内核分配失败与 32 位 compat 动态用例记录为环境
`SKIP`；用户复制失败已由坏地址 UAPI 探针真实覆盖，固定布局则由静态断言覆盖。

## 一小时混合负载

状态：PASS。2026-07-18 13:12:10 +08:00 开始，在提交 `f8608dc` 的 core/mock/CLI
上连续运行 3600 秒：

| 工作项 | 完成次数 |
|---|---:|
| set | 854330 |
| batch | 874079 |
| get | 858353 |
| 同线竞争 A/B | 515302 / 516238 |
| 20-event epoll 轮次 | 7064 |

每分钟进度中的 `event_drops` 始终为 0。测试脚本结束时断言 active lease 为 0、
GPIO 相关 BUG/Oops/WARNING 计数未增长；随后独立核对六个生产设备的
`active_leases` 全为 0，生产 core/gpiolib/Phytium 模块已恢复。原始记录为
`results/stress-mixed-3600s-20260718.txt`，SHA-256：
`916dbbf6d9864ecb6d1ac479160e5e0e3020b2a9139c2b4433630168eda24a80`。

压力后新增代码只涉及默认禁用的 mock 故障模式、CLI batch 错误元数据、测试/
benchmark/raw-lab 工件和文档；同步后仍需运行完整 mock 回归及缩短混合回归，结果
单独归档，不能篡改上述一小时基线的提交标识。

## 静态分析

状态：待一小时负载完成后执行，避免在同一板上干扰正式压力条件。将记录 W=1、
Sparse、Coccinelle、Smatch（若可安装）、ShellCheck、GCC analyzer/Cppcheck 的工具
版本、命令、原始输出和每项处置。

## 尚待硬件仪器验证

课程环境未提供逻辑分析仪、稳定外部边沿源或万用表自动采集，因此以下不能由 LED
目测替代：多线翻转时间差、物理边沿延迟分布、pull 电压和 drive level 对应电流。
软件事件语义已由 mock 精确验证，真实外部边沿/电气测量作为仪器条件项保留。

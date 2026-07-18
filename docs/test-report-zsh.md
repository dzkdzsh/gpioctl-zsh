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
| mock fault injection | busy、timeout、partial、wrong readback、rollback failure | PASS；原始失败 `EIO`，`failed_index=1`，`rollback_error=-EIO` |
| deterministic events | edge、debounce、epoll、overflow | 300 注入→256 记录、44 drop、seq 45..300 |
| `module_lifecycle_zsh.sh` | 活动 FD 拒绝卸载、生产模块重载 | PASS，20 cycles，overlay warning 不增长 |
| `phytium_led_smoke_zsh.sh` | 三别名解析、单/双灯写后读回与释放 | PASS |

`mock_smoke_zsh.sh` 汇总前述 mock/UAPI/CLI/并发/事件项目，最终普通模式通过；原始
记录 `results/mock-smoke-final-20260718.txt`，SHA-256
`2060c8fafbd3fa61315c9d7c0df1f9b1e7412a6c2da6179120b51f080374bf07`。

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
benchmark/raw-lab 工件、用户返回对象清零和文档。同步后的完整 mock 回归已通过；
另运行 60 秒混合回归，完成 14169 set、14476 batch、14184 get、8519+8598 同线
竞争和 117 轮事件，0 drop、最终租约 0。原始记录
`results/stress-mixed-60s-20260718.txt`，SHA-256
`930596caad7a9c0aeafbdbf12308def7264c32c4ad2ff0359fd011e4e4729f41`。

## 静态分析

状态：PASS，最终结果目录 `results/static-20260718-144207/`。内核与 raw lab 的
`W=1`、Sparse、Coccinelle、Smatch，以及 checkpatch、GCC analyzer、Cppcheck、
ShellCheck 共 12 项全部通过。Smatch 的 `warn:`/`error:` 即使工具返回 0 也作为
发布阻断；最终无此类诊断。checkpatch 严格原始退出码为 1，但只有 advisory CHECK，
0 error、0 warning，按门禁策略通过。`summary.tsv` SHA-256：
`1f3b95240de1f7e67b733f5e2d5f78343be598a604a2150b4e753f7daecf1905`。

## raw-MMIO 隔离实验

状态：部分 PASS、真实写 SKIP。`allow_write=0` 的冲突探针在安全 GPIO4 已占用同一
MMIO 资源时因 `EBUSY` 拒绝绑定，且 `/dev/gpio4_zsh` 保持存在；原始记录
`results/raw-mmio-conflict-final-20260718.txt`，SHA-256
`03ef0ae6f4b497e230107ba2ed1174de800f1e19d96ff78de9ccccce2aa03c9c1`。
GPIO4_7 接有 LED，且没有专用启动镜像证明整个 GPIO4 已隔离，因此真实 raw 写按
安全门记录为 `SKIP (isolation not proven)`。

## 最终硬件与发布审计

2026-07-18 运行三灯 smoke：LED20 闪烁三次，GPIO1_11 与 GPIO4_7 交替三轮；
三者结束逻辑值为 0，六个控制器活动租约均为 0。硬件原始记录 SHA-256：
`e61909cded4407f41ff9b50a90329efcf3ae564c11345bd86c8efddcda31c486`。

发布审计 PASS：生产 CLI/静态库不链接或引用 libgpiod，通用 core/UAPI 无板级 pin
和地址，自动化无固定 IP、无 raw `/dev/mem` 旁路，且无残留租约。CLI SHA-256 为
`1f772679bab5dfc3eb36c15014b16fbb8a89f906ab31f63b4b6b66da7e479b6e`，
静态库 SHA-256 为
`435fb221c89019c103b8c1f7941d1950839cbdd8095cedd636dd3535ba438beb`。

## 性能基准

状态：PASS（方法与数据完整），提交 `b5e1797` 上自研/libgpiod 各 10000 正式样本。
单线 get 与 lease/release 分别为基线吞吐的 105.45% 和 99.98%；单线 set 与 8-line
batch 分别为 74.68% 和 67.75%，未达约 90% 目标。事件 delivery P99 为 6540 ns，
roundtrip P99 为 18300 ns。完整数据、语义差异和瓶颈见 `docs/performance-zsh.md`。

## 尚待硬件仪器验证

课程环境未提供逻辑分析仪、稳定外部边沿源或万用表自动采集，因此以下不能由 LED
目测替代：多线翻转时间差、物理边沿延迟分布、pull 电压和 drive level 对应电流。
软件事件语义已由 mock 精确验证，真实外部边沿/电气测量作为仪器条件项保留。

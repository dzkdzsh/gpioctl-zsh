# 需求追踪矩阵

本表把课程题目和 `plan.md` 的硬门映射到设计、实现与可复现证据。状态只按当前证据
填写；“源码存在”不等于“实板/动态测试已通过”。

| ID | 需求 | 设计/实现 | 主要验证 | 当前状态 |
|---|---|---|---|---|
| R1 | 不同厂商 GPIO/IOPAD HAL | `gpioctl_hal_zsh.h`，core 动态注册，gpiolib/Phytium/mock | mock + 六 gpiochip 实板枚举 | 已验证 |
| R2 | `/dev/gpioX` 字符设备 | core cdev/IDA/动态 minor，`_zsh` 隔离命名 | list、open/ioctl/read/poll、udev | 已验证 |
| R3 | 输入/输出、电平读写 | LINE_CONFIG、VALUES、gpiolib backend | mock、LED20/GPIO1_11/GPIO4_7 smoke | 已验证 |
| R4 | IOPAD bias/drive/mux | 能力 UAPI、Phytium provider、租约快照恢复 | query/config 权限、GPIO1_1 other→gpio→other | 已验证 |
| R5 | CLI 交互式 | 共享 executor 的 `shell` | self-test/手工 REPL | 已验证 |
| R6 | CLI 脚本化 | `run file/-`、strict、JSON、timeout、dry-run、transaction | JSON integration、parser fuzz、mock script | 已验证 |
| R7 | sysfs 监控 | class attributes，无写控制节点 | mock/实板 stats 与属性核对 | 已验证 |
| R8 | 通用/可扩展 | core 无板级 pin，DT policy、HAL ABI/caps | source audit、全 40-pin 映射、mock | 已验证 |
| R9 | 每 FD 独占租约/自动释放 | controller bitmap + session line + module refs | 8 路冲突、SIGKILL、active=0 | 已验证 |
| R10 | 同控制器 batch/回滚 | 全验证、快照、写后读回、逆序回滚 | partial/wrong-readback/transaction/rollback failure | 已验证 |
| R11 | edge/read/poll/epoll | threaded IRQ、预分配 ring | 事件、去抖、epoll、300→256/44 | 已验证 |
| R12 | overflow 可检测 | drop oldest、flag、event_drops | first seq45/last300/drop44 | 已验证 |
| R13 | 权限/allowlist/reserved | udev、DT policy、CAP_SYS_RAWIO | 普通用户/特权/保留线 probe | 已验证 |
| R14 | UAPI 防御 | fixed-width、size/version/flags/reserved/uaccess | layout + bad-pointer `uapi_invalid_zsh`；32 位运行 | 布局/uaccess 已验证；32 位环境 SKIP |
| R15 | CLI 输入防御 | 有界数字/次数/时间、JSON escaping | 317-case parser fuzz | 已验证 |
| R16 | 锁与上下文安全 | controller→session、event/MMIO leaf spinlock、断言 | 源码审计、并发/event-close | 已验证；LOCKDEP 内核不可用 |
| R17 | probe/remove/module 生命周期 | module refs、busy 注销、持久 overlay | 活动租约卸载拒绝、20 次 reload | 已验证 |
| R18 | KUnit | 同源 logic header + 独立 KUnit module | `.ko` 编译，`make kunit` 配置探测 | 工件完成；当前内核运行 SKIP |
| R18A | raw-MMIO 实验隔离 | `lab/raw-mmio-lab` 独立模块/双 overlay | 活动 gpiochip 冲突为 EBUSY；真实写需专用启动隔离 | 冲突 PASS；真实写安全 SKIP |
| R19 | 1 小时混合压力 | `mixed_mock_zsh.sh` | 3600 秒：854330 set、874079 batch、858353 get、0 drop、最终 leases=0 | 已验证；raw SHA-256 已归档 |
| R20 | 静态分析 | max warnings/Sparse/Cocci/Smatch 等 | 12 项最终门禁及逐工具原始输出 | 已验证 |
| R21 | 黑盒性能对照 | 独立 persistent-FD harness、运行器、统计器 | 同板/同 pin/affinity、10000 样本、raw CSV | 已验证；set/batch 未达 90% 如实记录 |
| R22 | 无 libgpiod 依赖/原创 | 自定义 UAPI/API/CLI，`DEPENDENCY.md` | link/dependency/source/release audit | 已验证 |
| R23 | 完整文档与原始数据 | docs + results 归档 | 文件清单、SHA-256、复现与发布审计 | 已验证 |

## 课程题目直接对应

1. “硬件抽象层”：R1、R8。
2. “字符设备接口”：R2、R9、R14。
3. “输入/输出、电平、IOPAD”：R3、R4、R10。
4. “交互式和脚本化、sysfs”：R5、R6、R7。
5. “扩展性”：R8；三盏 LED 只位于板级配置/DT/测试，不进入 core。

## 状态维护规则

- 新功能必须同时更新实现、自动化测试、相关文档和本表。
- 受环境限制的项写明 SKIP/未验证，不能用替代测试改写为 PASS。
- 性能目标只有原始数据和公平性条件齐全后才填写结论。
- 最终完成前逐条重新读取权威文件和结果，不以本表旧状态代替证据。

# gpioctl_zsh 开发者指南

## 对象与生命周期

`gpioctl_controller_zsh` 由后端注册并拥有字符设备；每次 `open()` 创建一个
`gpioctl_session_zsh`，租约和事件队列都只属于该 session。已打开文件会增加
controller 的 open 计数，后端注销在计数非零时返回 `EBUSY`。`release()` 设置
事件流 closing 状态后，逐线走与显式释放相同的安全清理函数，最后才释放 session。

每条租约保留 backend line handle。若有匹配的 IOPAD provider，还会保存 provider
引用和租约前的 opaque PAD 状态；这两个资源只在 GPIO 安全态和 IOPAD 恢复尝试
完成后归还。申请失败按已完成线路逆序清理，清理失败会记录但不会跳过后续资源。

## 锁顺序与上下文

控制面的全局顺序是：

```text
controller mutex -> session mutex -> backend/provider operation
```

- 申请、显式释放和文件关闭同时修改全局租约位图与 session line，因此依次取得
  controller mutex 和 session mutex。
- 配置、读写、事务和事件配置只修改已由该文件拥有的线路，持有 session mutex；
  它们不会再反向取得 controller mutex。
- IOPAD provider 注册表 mutex 只保护指针、unregistering 和引用获取，绝不跨
  provider 回调持有；provider 回调内的 MMIO spinlock 是叶子锁。
- event spinlock 独立保护预分配 ring、序号和 closing。IRQ threaded handler、
  `read()` 与 `poll()` 都不在该锁内分配、复制用户内存或调用可睡眠后端。
- `free_irq()` 可能等待 threaded handler，因此只在不持有 event spinlock 时调用。

关键 `*_locked_zsh` helper 使用 `lockdep_assert_held()` 声明调用契约。当前飞腾派
内核未启用 LOCKDEP，这些断言仍作为可审计契约保留；在启用 LOCKDEP 的测试内核
上会变成运行时检查。

## UAPI 与事务规则

ioctl 输入先完整复制到内核对象，再检查 ABI 版本、精确结构长度、flags、reserved、
数量、offset、权限、租约和操作冲突。任何硬件修改都发生在这些校验之后。输出先
形成内核快照再复制到用户态，任何 uaccess 都不在 spinlock 临界区。

事务保存提交前的软件状态，按顺序执行并对输出写入读回核对。首次失败保留为返回
错误，已执行项逆序回滚；回滚失败单独记录，不能覆盖原始失败原因。租约最终释放
使用设备树安全态，不把事务快照误当成长期安全策略。

## 单元测试层次

`include/gpioctl_logic_zsh.h` 存放 core 实际调用的纯逻辑 helper，覆盖 ABI header、
reserved、line policy、active-low、电平事件 ring 和软件消抖边界。

- `userspace/tests/logic_zsh.c` 在所有构建环境执行同一 header，用作当前板内核的
  可运行门。
- `tests/kunit/gpioctl_logic_kunit_zsh.c` 是独立 KUnit 模块，不进入生产模块。
- `make kunit` 检查运行内核配置；未启用 `CONFIG_KUNIT` 时明确输出 `SKIP`，不得
  记录为 KUnit PASS。当前飞腾派内核能编译该 KUnit `.ko`，但配置未启用，故尚未
  执行 KUnit suite。
- 状态机、故障注入、IRQ 与真实 backend 交互继续由 mock/板级集成测试覆盖。

## 修改后的最低验证

```sh
make -j2
make check
make kunit
sudo tests/integration/mock_smoke_zsh.sh
```

涉及 IOPAD、释放顺序或真实后端时，还必须运行板级 smoke，并检查所有相关
`active_leases=0`、错误统计和内核日志。KUnit `SKIP` 不等于失败，但发布报告必须
把它列为当前内核配置限制，不能省略。

普通模块重载必须保留 configfs 中已应用的 `gpioctl_zsh` overlay。Linux 对动态
删除已覆盖到既有设备节点的属性会明确报告内存泄漏风险，因此
`scripts/unload_zsh.sh` 默认不删除 overlay；只有完整卸载使用显式
`--remove-overlay`。生命周期/压力测试不得在循环中反复删除和重建该 overlay。

## 发布分析入口

静态分析统一使用 `make static-analysis`。脚本为 W=1、Sparse、Coccinelle、Smatch、
checkpatch、GCC analyzer、Cppcheck、ShellCheck 分别保存原始输出与退出码；工具缺失
只能记录 SKIP。发布前还应运行 `make audit` 检查板级硬编码、危险 raw 旁路、产品
libgpiod 依赖、文档清单和残留租约。

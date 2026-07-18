# gpioctl_zsh UAPI 说明

公共 ABI 位于 `include/uapi/gpioctl_zsh.h`，当前版本为 1。设备节点命名为
`/dev/gpioN_zsh`；稳定 line 标识是“已打开的控制器 FD + controller-local offset”。

## 通用结构规则

- 所有 ABI 字段使用 `__u32`、`__s32`、`__u64`，结构中不包含指针、`long` 或字符串。
- 可扩展结构带 `abi_version`、`struct_size`、`flags` 和 `reserved[]`。
- 当前版本要求精确 `struct_size`；错误版本返回 `EPROTO`，错误尺寸返回 `EINVAL`。
- 未知 flag、非零 reserved、零/超限 count、重复 offset 和越界 offset 返回 `EINVAL`。
- 每次请求最多 32 条 line、每个 batch 最多 32 个操作；事件记录固定 48 字节。
- ioctl 输入先完整 `copy_from_user()`，输出先形成内核快照再 `copy_to_user()`。
- `compat_ioctl` 复用同一处理器，因为结构在 32/64 位下没有指针尺寸差异；布局测试
  通过 `_Static_assert`/运行时 size/offset 核对。

## ioctl 表

| 请求 | 方向 | 作用 | 关键前置条件 |
|---|---|---|---|
| `GET_ABI` | out | ABI 上限与事件尺寸 | 可打开设备 |
| `GET_CAPS` | out | 控制器 line 数和能力 | 可打开设备 |
| `GET_LINE_CAPS` | in/out | 单线能力、drive 范围 | 合法 offset |
| `GET_LINE_POLICY` | in/out | allowlist 与释放安全态 | 合法 offset |
| `LEASE_REQUEST` | in | 全成全败独占申请 | 策略允许且全局空闲 |
| `LEASE_RELEASE` | in | 释放一组本 FD 租约 | 本 FD 拥有全部 line |
| `LINE_CONFIG` | in | 方向、初值、active-low、bias、debounce | 持有租约 |
| `VALUES_GET` | in/out | 最多 32 线逻辑电平位图 | 本 FD 拥有全部 line |
| `VALUES_SET` | in | 最多 32 线逻辑电平位图 | 全部已配置输出 |
| `BATCH_EXEC` | in/out | 同控制器配置/写入事务 | 持有所有涉及 line |
| `EVENT_CONFIG` | in | 上升/下降/双边沿或关闭 | 持有租约、后端支持 IRQ |
| `IOPAD_CONFIG` | in | bias/drive/`mux=gpio` | 租约 + `CAP_SYS_RAWIO` |
| `IOPAD_GET_CONFIG` | in/out | 抽象 IOPAD 状态 | 合法 offset |
| `GET_STATS` | out | 操作/错误/冲突/事件/drop/活动租约 | 可打开设备 |

GPIO 电平控制不复用 `read()`/`write()`：控制都通过 ioctl，`read()` 专用于固定事件
记录，避免控制语义和字节流语义混淆。

## 租约与权限

租约绑定 FD。只有同一 FD 能配置、读取、写入和订阅该 line；其他 FD 申请返回
`EBUSY`。普通用户仅能申请设备树 `ALLOW_UNPRIVILEGED` line，未知 line 需要
`CAP_SYS_RAWIO` 且强制 input-only，reserved line 对 root 也返回 `EPERM`。

`GPIOCTL_ZSH_LEASE_INPUT_ONLY` 可进一步限制当前租约，之后不能配置输出。

## 逻辑电平

`LINE_ACTIVE_LOW` 只影响当前租约看到的逻辑值：

```text
physical = boolean(logical) XOR active_low
logical  = boolean(physical) XOR active_low
```

设备树安全值始终是物理值，不受 session 的 active-low 设置影响。

## Batch 返回

每个 `gpioctl_zsh_batch_op` 是 `CONFIG` 或 `SET`。同一 batch 不允许重复 offset。
内核总会尝试把更新后的 batch 复制回用户态：

- `failed_index = -1`：未定位到失败操作。
- `failed_index >= 0`：原始错误发生的操作位置。
- `rollback_error = 0`：所有已提交项回滚成功。
- `rollback_error < 0`：首次回滚失败 errno；ioctl 的返回 errno 仍是原始提交错误。

这一区分防止回滚错误覆盖根因。
用户态库保留 ioctl 回写后的结构；CLI 的 human/JSON 错误也同时输出这两个字段，
自动化不得只检查顶层 `EIO` 而丢弃回滚状态。

## 事件记录

`gpioctl_zsh_event` 包含 offset、edge、单调时钟 `timestamp_ns`、每 session 单调递增
`sequence` 和 flags。短于一条记录的 read 返回 `EINVAL`；非阻塞空队列返回
`EAGAIN`。一次 read 可返回多条完整记录，不返回半条结构。

`GPIOCTL_ZSH_EVENT_OVERFLOW` 表示在该记录之前至少有最旧事件因 ring 满被丢弃；
同时 `GET_STATS.event_drops` 提供累计计数。

## 常见 errno

| errno | 含义 |
|---|---|
| `EPROTO` | ABI 版本不匹配 |
| `EINVAL` | 尺寸、flag、reserved、count、offset 或枚举非法 |
| `EFAULT` | 用户指针不可访问 |
| `EACCES` | 普通用户申请非 allowlist line |
| `EPERM` | reserved、非所有者、input-only 输出或缺少 IOPAD 能力权限 |
| `EBUSY` | line 已被其他 FD 租约，或资源/模块仍在使用 |
| `EOPNOTSUPP` | 后端不支持该能力 |
| `ETIMEDOUT` | HAL/CLI 总预算超时 |
| `EIO` | 操作失败或写后读回不一致 |
| `ENODEV` | controller 正在移除或已不可用 |

ABI 的权威事实始终是公共头文件；本文用于解释语义，修改结构时必须同时更新布局
测试、非法输入测试、库封装和本文档。

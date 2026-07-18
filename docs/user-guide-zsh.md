# gpioctl_zsh 用户指南

## 安全准备

板级别名来自 `/etc/gpioctl_zsh/board.conf`。先运行 `resolve`/`info` 核对 controller、
offset、PAD、物理引脚和有效电平。外接 LED 必须各自串联 `330Ω..1kΩ` 电阻；内部
上拉不能代替限流电阻。

```sh
gpioctl_zsh list
gpioctl_zsh resolve GPIO1_11
gpioctl_zsh info GPIO4_7
gpioctl_zsh iopad-get GPIO4_7
```

## 单命令

```sh
gpioctl_zsh get GPIO1_11
gpioctl_zsh set GPIO1_11 1 1000
gpioctl_zsh blink LED20 3 1000 1000
gpioctl_zsh pair-blink GPIO1_11 GPIO4_7 3 1000
gpioctl_zsh batch-set /dev/gpio1_zsh 500 8=1 11=0
gpioctl_zsh watch GPIO1_11 both 5000 10 20000
gpioctl_zsh stats GPIO1_11
```

时间单位均为毫秒，watch 最后的可选消抖值为微秒。命令结束会关闭 FD，驱动自动
恢复 GPIO 安全态和租约前 IOPAD 状态；若要跨多条命令保持输出，使用脚本租约。

## 脚本与交互

`demo.gpioctl`：

```text
# 同一进程、同一 FD 持有租约
acquire GPIO1_11 out 0
value GPIO1_11 1
sleep 1000
value GPIO1_11 0
release GPIO1_11
```

运行方式：

```sh
gpioctl_zsh --strict run demo.gpioctl
gpioctl_zsh --strict run - < demo.gpioctl
gpioctl_zsh shell
```

`--strict` 遇到首个错误即停止；非 strict 模式继续后续行但最终返回非零。错误会带
文件/标准输入来源和行号。空白分 token，`#` 开始注释；循环和条件交给 Shell 或
Python，不在 GPIO DSL 中复制一套通用语言。

## 事务块

```text
transaction /dev/gpio1_zsh
tx-line 8 out 1 active-low
tx-line 11 out 1
commit 1000
```

`tx-line` 只收集用户态计划，`commit` 才一次申请全部 line 并发出 batch ioctl。
重复 offset、嵌套/空事务失败；`abort` 主动作废。脚本在未 commit/abort 时结束也
返回错误。事务仅限一个 controller，跨 controller 不承诺原子性。

## JSON Lines 与自动化

```sh
gpioctl_zsh --json info GPIO1_11
gpioctl_zsh --json --strict run demo.gpioctl
gpioctl_zsh --json --timeout 5000 --strict run demo.gpioctl
gpioctl_zsh --dry-run blink GPIO1_11 3 1000 1000
```

`--json` 的 stdout/stderr 每个非空行都是独立 JSON object，且包含 boolean `ok`；
适合流式逐行解析，不应把多行整体当作单个 JSON。`--timeout` 是从进程启动计算的
单调时钟总预算，不会为脚本每行重置。`--dry-run` 只解析、验证和输出计划，不打开
设备也不等待。

## IOPAD

读取不需要特权：

```sh
gpioctl_zsh iopad-get GPIO4_7
```

显式修改需要 root/`CAP_SYS_RAWIO`，且内部仍要求对应租约。CLI 为单命令自动建立
租约：

```sh
sudo gpioctl_zsh iopad GPIO4_7 mux=gpio bias=none drive=4
```

只允许 `mux=gpio`、`bias=none|up|down` 和 drive `0..15`；不能传物理地址、mask
或 `funcN`。未写字段保持原值。

## 权限

设备默认 `0660 root:gpio`。管理员把用户加入组后需重新登录：

```sh
sudo usermod -aG gpio zsh
id
```

组权限只允许普通 allowlist GPIO 操作，不授予 IOPAD 写权限，也不能绕过 reserved
策略。不要把设备改为 `0666`。

## 常见问题

| 现象 | 原因与处理 |
|---|---|
| `Device or resource busy` | 另一 FD 持有独占租约；等待或终止所有者 |
| `Permission denied` | 不在 gpio 组、非 allowlist，或 IOPAD 缺特权 |
| `Operation not permitted` | reserved、未持有租约或 input-only/策略禁止输出 |
| `Operation not supported` | 当前 backend 不具备 bias/IRQ/IOPAD 能力 |
| `Input/output error` | HAL 失败或写后读回不一致；检查日志/接线/复用 |
| 命令结束灯熄灭 | 预期的安全释放；要保持则在同一脚本持有租约 |

异常退出或 `kill -9` 不会绕过内核 FD release；驱动会回收租约并应用安全状态。

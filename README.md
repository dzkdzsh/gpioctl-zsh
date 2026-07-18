# gpioctl_zsh：通用 GPIO 字符设备控制系统

`gpioctl_zsh` 是课程设计中从零实现的 Linux GPIO 控制系统。项目不复制、
不链接、也不运行时依赖 libgpiod；它使用 Linux 内核公开的 gpiolib 接口，
并提供原创的版本化 ioctl UAPI、C 用户库和命令行工具。

当前主要目标平台为飞腾派、Linux
`6.6.63-phytium-embedded-v3.2`。通用 core 不包含飞腾地址、固定控制器数量、
LED 引脚或 PAD 名；平台差异由 HAL、设备树覆盖层及板级配置隔离。

## 已实现结构

- `gpioctl_core_zsh.ko`：动态字符设备、每文件描述符独占租约、自动清理、
  单线/多线读写、批量回滚、边沿事件、统计与只读 sysfs。
- `gpioctl_backend_gpiolib_zsh.ko`：通过 GPIO descriptor API 访问任意已注册
  gpiochip，并支持可睡眠 GPIO 控制器。
- `gpioctl_backend_phytium_zsh.ko`：设备树声明的受保护 IOPAD 资源，提供
  上下拉、0..15 驱动档位和命名的 GPIO 复用；写入需要 `CAP_SYS_RAWIO`。
- `gpioctl_mock_zsh.ko`：可注入失败的测试后端。
- `libgpioctl_zsh.a`：原创 C API，封装租约、配置、读写、事务、事件、IOPAD
  和统计 ioctl。
- `gpioctl_zsh`：单命令、REPL、文件脚本和标准输入脚本共用一个执行引擎。

## 构建

板上需要当前内核头文件、C 编译器、make 和 `device-tree-compiler`：

```sh
sudo apt-get install -y build-essential device-tree-compiler
make
make check
```

输出统一进入 `build/`，不会把 `.o`、`.ko` 或 `.dtbo` 混入源码目录。

## 安装、加载与卸载

```sh
sudo make install
sudo ./scripts/load_zsh.sh

# 将开发账号加入普通 GPIO 权限组；重新登录后生效
sudo usermod -aG gpio zsh

sudo ./scripts/unload_zsh.sh
sudo make uninstall
```

安装器将同一版本的四个模块放入：

```text
/lib/modules/$(uname -r)/kernel/test/course_design_zsh/
```

随后执行 `depmod`。udev 规则把 `/dev/gpioN_zsh` 设置为
`0660 root:gpio`。普通 GPIO 操作只需 `gpio` 组；IOPAD ioctl 即使设备可打开，
仍由内核强制检查 `CAP_SYS_RAWIO`。

## CLI 快速使用

板级配置安装为 `/etc/gpioctl_zsh/board.conf`，也可通过 `--config` 指定。

```sh
gpioctl_zsh list
gpioctl_zsh resolve GPIO1_11
gpioctl_zsh info GPIO4_7
gpioctl_zsh get GPIO1_11
gpioctl_zsh set GPIO1_11 1 1000
gpioctl_zsh blink GPIO1_11 3 1000 1000
gpioctl_zsh pair-blink GPIO1_11 GPIO4_7 3 1000
gpioctl_zsh batch-set /dev/gpio1_zsh 500 8=1 11=0
gpioctl_zsh watch GPIO1_11 both 5000 10
gpioctl_zsh stats GPIO1_11
gpioctl_zsh iopad-get GPIO4_7

# 电气配置必须使用 sudo/root；未写出的字段保持原值
sudo gpioctl_zsh iopad GPIO4_7 mux=gpio bias=none drive=4
```

`set`、`blink`、`pair-blink` 在命令结束时关闭文件描述符，内核自动将租约线
恢复为设备树声明的方向、物理电平和 bias；未声明线路使用输入高阻的保守默认值。
若需要在多个脚本命令间保持输出，使用 `acquire/value/release`：

```text
acquire GPIO1_11 out 0
value GPIO1_11 1
sleep 1000
value GPIO1_11 0
release GPIO1_11
```

保存为 `demo.gpioctl` 后运行：

```sh
gpioctl_zsh --strict run demo.gpioctl
printf 'acquire GPIO1_11 out 0\nvalue GPIO1_11 1\nrelease GPIO1_11\n' |
  gpioctl_zsh --strict run -
gpioctl_zsh shell
gpioctl_zsh --json info GPIO1_11
gpioctl_zsh --dry-run blink GPIO1_11 3 1000 1000
gpioctl_zsh --timeout 5000 --strict run demo.gpioctl
```

脚本语法刻意保持确定和简单：空白分隔 token，`#` 开始注释；循环和复杂条件
交给 Shell 或 Python。`--strict` 在首个失败处停止并返回非零退出码。
`--timeout MS` 是从进程启动起计算的单调时钟总预算，约束单命令、整个脚本和
交互会话，而不是为每条子命令重新计时；预算耗尽返回 `ETIMEDOUT`。单次时长、
总重复时长和总预算上限均为 24 小时，重复次数上限为 100000，避免整数溢出及
误输入导致的无限占用。`--dry-run` 只验证并输出计划，不等待也不访问设备。

`--json` 使用 JSON Lines：标准输出与标准错误的每个非空行都是独立、完整且含
`ok` 字段的 JSON 对象，适合逐行流式解析。对象中的路径、别名和错误文本均按
JSON 规则转义；脚本错误同时给出来源和行号。JSON 交互模式不会输出人类提示符。

同一控制器的多线配置可写成事务块：

```text
transaction /dev/gpio1_zsh
tx-line 8 out 1 active-low
tx-line 11 out 1
commit 1000
```

`tx-line` 只在用户态收集并验证操作；`commit` 一次性租约全部 line，并通过一个
批量 ioctl 提交。任何后端操作失败都由内核按快照逆序回滚，文件描述符关闭后
释放全部租约。重复 offset、嵌套事务和空事务均失败；可用 `abort` 主动作废，
脚本在未 `commit/abort` 时到达 EOF 或 `quit` 也会作废事务并返回非零退出码。

## 板级测试夹具

`config/phytium-pi-v1.conf` 当前包含：

| 别名 | 字符设备与 offset | PAD | 有效电平 | 用途 |
|---|---:|---|---|---|
| `LED20` | `/dev/gpio1_zsh:8` | E37 | 低有效 | 板载灯 |
| `GPIO1_11` | `/dev/gpio1_zsh:11` | BA49 | 高有效 | 外接 LED |
| `GPIO4_7` | `/dev/gpio4_zsh:7` | W53 | 高有效 | 外接 LED |

外接 LED 必须各自串联 `330Ω..1kΩ` 限流电阻。GPIO 内部上拉电阻不能作为
LED 限流电阻。引脚映射只存在于板级配置、设备树和测试资料中，未写死在 core。

2026-07-17 已在实板验证：六个 gpiochip 动态注册；W53 由 IOPAD ioctl 切换为
GPIO；GPIO1_11 与 GPIO4_7 交替闪烁三轮（每状态 1 秒）；结束后两控制器
`active_leases=0` 且 `errors=0`；普通组用户可读 GPIO，但无权修改 IOPAD。

## 安全边界

- UAPI 使用固定宽度字段、结构版本/长度、清零保留字段和固定请求上限。
- 用户内存只通过 `copy_from_user()`/`copy_to_user()` 访问，不在 spinlock 内访问。
- 租约按打开文件描述符隔离；冲突返回 `EBUSY`，进程退出自动清理。
- 普通用户只能租约设备树白名单线路；未知线路即使由 root 租约也只能作输入，
  保留线路对所有调用者拒绝。释放、异常关闭和回滚使用同一物理安全状态。
- IRQ 使用预分配有界队列和 threaded handler，不在硬中断上下文调用可睡眠
  GPIO 后端。
- IOPAD 只接受能力化的偏置、驱动档位和 `mux=gpio`，不接受地址、掩码或
  任意 `funcN`；MMIO 资源由 platform driver 独占并执行锁保护的读改写、读回
  和失败回滚。
- `iopad-get` 只返回抽象的 bias、驱动档位和 `gpio/other` 复用状态，不向
  用户态泄露或接受裸寄存器值；查询本身不需要修改权限。
- sysfs 只用于监控，不提供绕过字符设备租约的电平控制旁路。

设备树五元组格式、完整 40-pin 白名单、权限规则和 gpiolib→IOPAD bias fallback
见 [GPIO 设备树安全策略](docs/device-tree-policy-zsh.md)。

完整设计边界、测试矩阵和最终验收条件见 [plan.md](plan.md)。项目仍以该文件
第 21 节为完成门槛；尚未通过的压力、fuzz、KUnit 与性能项不会被描述为已完成。

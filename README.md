# gpioctl_zsh：通用 GPIO 字符设备控制系统

## English overview

`gpioctl_zsh` is a from-scratch Linux GPIO character-device control stack. It
provides a versioned ioctl UAPI, an extensible kernel HAL, a C library, and a CLI
for one-shot, interactive, and scripted GPIO control. The production code does
not copy, link, or depend on libgpiod; libgpiod is used only by an optional,
explicitly built black-box benchmark.

The generic core contains no Phytium register addresses, fixed controller
count, LED pin, or PAD name. Hardware-specific behavior is isolated behind
backends, Device Tree data, and board configuration. The current hardware
validation covers Phytium Pi running Linux
`6.6.63-phytium-embedded-v3.2`; other SoCs and kernel versions remain unverified.

Highlights:

- per-file-descriptor exclusive leases with automatic cleanup and safe-state
  restoration;
- single-line and rollback-capable batch operations, edge events, bounded
  queues, statistics, and read-only sysfs observability;
- a constrained IOPAD interface with capability checks instead of arbitrary
  register access;
- one-shot commands, an interactive shell, a deterministic script mode, JSON
  Lines output, dry-run validation, and process-wide deadlines;
- mock fault injection, parser fuzzing, concurrency and lifecycle stress tests,
  static analysis, hardware smoke tests, and reproducible benchmark data.

Start with the [CLI user guide](docs/user-guide-zsh.md),
[deployment guide](docs/deployment-guide-zsh.md), and
[architecture](docs/architecture-zsh.md). Documentation is primarily written in
Chinese. Raw measurement data is intentionally versioned under `results/`.

> **Hardware warning:** GPIO, pin multiplexing, bias, drive strength, and raw
> MMIO mistakes can damage hardware or conflict with another driver. Verify the
> board schematic, line ownership, voltage, and current limiting before any
> output operation. The isolated raw-MMIO lab is never part of the default build
> or installation path.

## 中文说明

`gpioctl_zsh` 是课程设计中从零实现的 Linux GPIO 控制系统。生产与默认构建工件
不复制、不链接、也不运行时依赖 libgpiod；它使用 Linux 内核公开的 gpiolib 接口，
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
- `gpioctl_mock_zsh.ko`：可注入占用、超时、部分提交失败和错误读回的测试后端。
- `libgpioctl_zsh.a`：原创 C API，封装租约、配置、读写、事务、事件、IOPAD
  和统计 ioctl。
- `gpioctl_zsh`：单命令、REPL、文件脚本和标准输入脚本共用一个执行引擎。

## 最速部署：从 Release 到第一条 CLI 命令

下面是一条已经从全新 `v0.1.2` 源码包实测到 `gpioctl_zsh list` 的完整路线。
内核模块必须针对板上正在运行的内核构建，因此这里把源码压缩包传到板上后再解压、
编译，而不是在 Windows 电脑上直接运行 `make`。

### 1. 在电脑上把 `.tar.gz` 源码包传到板子

从 [Releases](https://github.com/dzkdzsh/gpioctl-zsh/releases) 下载最新版本的
`Source code (tar.gz)`，**不要先在 Windows 解压**。在 PowerShell 中进入下载目录，
将 `<BOARD_IP>` 替换为板子的实际 IP：

```powershell
scp .\gpioctl-zsh-0.1.2.tar.gz zsh@<BOARD_IP>:~/
ssh zsh@<BOARD_IP>
```

必须让 Linux 板子解压 `.tar.gz`，这样才能保留安装、加载和测试脚本的可执行权限。
Windows 解压后再 `scp -r` 会把这些权限丢成 `0644`，直接运行旧版文档中的
`sudo make install` 会报 `scripts/install_zsh.sh: 权限不够`。

### 2. 在板上检查内核头文件、构建并安装

登录板子后，逐行执行下面的命令。第一条 `cd` 很重要：后续所有 `make` 和脚本命令
都在项目根目录运行。

```sh
tar -xzf ~/gpioctl-zsh-0.1.2.tar.gz -C ~
cd ~/gpioctl-zsh-0.1.2

# 必须存在与 uname -r 完全匹配的内核构建目录
uname -r
test -f "/lib/modules/$(uname -r)/build/Makefile" || {
    echo "缺少当前运行内核的头文件/构建目录，不能继续编译内核模块" >&2
    exit 1
}

# 这条命令只安装编译器、make 和 DTC，不负责安装飞腾定制内核头文件
sudo apt-get update
sudo apt-get install -y build-essential device-tree-compiler

# 在板上的项目根目录构建；产物进入 build/
make -j"$(nproc)"
make check

# 安装模块、CLI、板级配置和 overlay，然后加载驱动
sudo make install
sudo sh ./scripts/load_zsh.sh
```

如果已经在 Windows 解压并把目录传到了板子，不必重新下载；进入项目根目录后先
恢复所有 Shell 脚本的执行权限，再从上面的内核头文件检查继续：

```sh
cd ~/gpioctl-zsh-0.1.2
find scripts benchmarks tests lab/raw-mmio-lab -type f -name '*.sh' \
    -exec chmod +x {} +
```

顶层 Makefile 也通过 `sh` 调用安装、卸载和审计脚本，避免仅因执行位丢失导致核心
流程中断；恢复权限仍是推荐做法，因为后续可能直接运行集成与压力测试脚本。

如果上面的头文件检查失败，不要继续运行 `make`。通用发行版的
`linux-headers-$(uname -r)` 不一定包含飞腾厂商内核
`6.6.63-phytium-embedded-v3.2` 的匹配头文件；需要先从当前系统镜像/厂商内核源码
安装与 `uname -r` 完全一致的构建目录，再重新执行本节。

### 3. 执行第一条 CLI 命令并判断部署成功

首次验证先使用只读的 `list`，不改变任何 GPIO 电平：

```sh
sudo gpioctl_zsh list
ls -l /dev/gpio*_zsh
```

同时满足以下条件即表示最小部署成功：

- `load_zsh.sh` 输出 `gpioctl_zsh loaded; overlay=applied`；
- `sudo gpioctl_zsh list` 成功列出一个或多个 `/dev/gpioN_zsh` 控制器；
- `ls -l /dev/gpio*_zsh` 能看到对应字符设备。

这一步使用 `sudo` 是为了不等待用户组权限重新登录。确认驱动正常后，再把当前账号
加入 `gpio` 组并重新登录，之后普通 GPIO 命令不需要 `sudo`：

```sh
sudo usermod -aG gpio "$USER"
exit
# 回到电脑后重新登录板子
ssh zsh@<BOARD_IP>
gpioctl_zsh list
```

修改 IOPAD 的命令仍然需要 `sudo`/`CAP_SYS_RAWIO`。在执行 `set`、`blink` 等输出
命令前，必须确认目标引脚、电压、复用、占用关系和外接 LED 限流电阻。

## 构建与安装分别做了什么

- `make -j"$(nproc)"`：构建四个内核模块、用户态库/CLI 和设备树 overlay；可安装
  工件统一复制到 `build/`。Kbuild 还会在 `kernel/` 暂存 `.o`、`.ko` 等构建文件，
  `make clean` 会清理它们和 `build/`。
- `make check`：执行不接触 GPIO 硬件的 UAPI 布局、公共逻辑和 CLI 自检。
- `sudo make install`：安装刚构建的模块、CLI、头文件、板级配置、udev 规则和
  overlay，并执行 `depmod`；它不会自动加载驱动。
- `sudo sh ./scripts/load_zsh.sh`：应用/复用设备树 overlay 并按顺序加载驱动模块。

## 卸载

```sh
# 日常重载：卸载模块，但保留已经应用的设备树 overlay
sudo sh ./scripts/unload_zsh.sh

# 完整移除：卸载模块并删除已安装文件和 overlay
sudo make uninstall
```

普通 `unload_zsh.sh` 只卸载模块并保留已应用的设备树 overlay，便于反复加载、mock
和压力测试，也避免 Linux 对动态删除既有节点属性给出的内存泄漏警告。只有完整
`make uninstall` 才调用 `unload_zsh.sh --remove-overlay`；在不卸载软件的正常开发
周期中，overlay 被视为持久板级配置。

安装器将同一版本的四个模块放入：

```text
/lib/modules/$(uname -r)/kernel/test/course_design_zsh/
```

随后执行 `depmod`。udev 规则把 `/dev/gpioN_zsh` 设置为
`0660 root:gpio`。普通 GPIO 操作只需 `gpio` 组；IOPAD ioctl 即使设备可打开，
仍由内核强制检查 `CAP_SYS_RAWIO`。

## CLI 快速使用

完整的概念、语法、命令参考、脚本规则和故障处理见
[CLI 用户指南](docs/user-guide-zsh.md)。CLI 的通用结构为：

```text
gpioctl_zsh [GLOBAL_OPTIONS] COMMAND [ARGUMENTS]
```

多数命令操作一个 `TARGET`。`TARGET` 可以写成板级别名、通用名
`GPIO<CONTROLLER>_<OFFSET>`，或直接路径
`/dev/gpio<CONTROLLER>_zsh:<OFFSET>`。三种形式最终都解析为字符设备与 offset；
通用名只编码控制器和 offset，并不表示某种具体接线。执行输出操作前先检查：

```text
gpioctl_zsh list
gpioctl_zsh resolve TARGET
gpioctl_zsh info TARGET
gpioctl_zsh --dry-run set TARGET 1 1000
```

板级配置安装为 `/etc/gpioctl_zsh/board.conf`，也可通过 `--config FILE` 指定。
全局选项必须位于命令之前。`get`、`set`、`blink` 等一次性命令的通用语法为：

```text
gpioctl_zsh get TARGET
gpioctl_zsh set TARGET VALUE [HOLD_MS]
gpioctl_zsh blink TARGET COUNT ON_MS OFF_MS
gpioctl_zsh watch TARGET rising|falling|both TIMEOUT_MS [COUNT] [DEBOUNCE_US]
```

以下才是当前飞腾派课程板的具体示例，不是通用语法定义：

```sh
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

# 当前板上修改 IOPAD 需要 sudo/root；未写出的字段保持原值
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
误输入导致的无限占用。`--dry-run` 对控制命令只验证并输出计划，不改变硬件或
等待；`list`、`info`、`stats`、`iopad-get` 等查询命令仍会打开设备读取信息。

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
批量 ioctl 提交。内核在写入后读取实际 GPIO 值核对；任何后端操作或读回校验失败
都按提交前快照逆序回滚，文件描述符关闭后
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
2026-07-18 又以未接外设的 GPIO1_1 验证自动 IOPAD 生命周期：租约前状态为
`bias=down drive=4 mux=other`，输入租约期间为 `mux=gpio`，释放后逐字段恢复原值；
随后 LED20、GPIO1_11、GPIO4_7 的写后读回测试均通过且无残留租约。

## 安全边界

- UAPI 使用固定宽度字段、结构版本/长度、清零保留字段和固定请求上限。
- 用户内存只通过 `copy_from_user()`/`copy_to_user()` 访问，不在 spinlock 内访问。
- 租约按打开文件描述符隔离；冲突返回 `EBUSY`，进程退出自动清理。
- 普通用户只能租约设备树白名单线路；未知线路即使由 root 租约也只能作输入，
  保留线路对所有调用者拒绝。释放、异常关闭和回滚使用同一物理安全状态。
- IRQ 使用预分配有界队列和 threaded handler，不在硬中断上下文调用可睡眠
  GPIO 后端。
- 每条线路成功取得租约后，IOPAD provider 会保存受控字段并自动选择该 PAD 的
  GPIO 复用；显式释放、进程退出和申请回滚都会在 GPIO 安全态之后恢复租约前的
  mux、bias 与 drive。provider 引用贯穿租约，不能在仍有线路依赖时卸载。
- GPIO 输出配置和批量写入均执行写后读回；读回不一致按 `EIO` 处理并触发回滚。
- IOPAD 只接受能力化的偏置、驱动档位和 `mux=gpio`，不接受地址、掩码或
  任意 `funcN`；MMIO 资源由 platform driver 独占并执行锁保护的读改写、读回
  和失败回滚。
- `iopad-get` 只返回抽象的 bias、驱动档位和 `gpio/other` 复用状态，不向
  用户态泄露或接受裸寄存器值；查询本身不需要修改权限。
- sysfs 只用于监控，不提供绕过字符设备租约的电平控制旁路。

设备树五元组格式、完整 40-pin 白名单、权限规则和 gpiolib→IOPAD bias fallback
见 [GPIO 设备树安全策略](docs/device-tree-policy-zsh.md)。锁顺序、对象生命周期、
事务约束与 KUnit 分层见 [开发者指南](docs/developer-guide-zsh.md)。

完整设计边界、测试矩阵和最终验收条件见 [plan.md](plan.md)。项目仍以该文件
第 21 节为完成门槛。2026-07-18 已完成一小时压力、fuzz、最终静态门禁、硬件
smoke、性能基准和发布审计；KUnit 运行、LOCKDEP、32 位 compat 与外部仪器测试因
当前环境不具备条件而明确记录为 SKIP，不用替代测试冒充通过。

## Mock 故障注入

`gpioctl_mock_zsh.ko` 的 offset 参数默认均为 `-1`（禁用），可通过 sysfs 在测试中
逐项设置：`busy_offset` 令租约申请返回 `EBUSY`，`timeout_offset` 返回
`ETIMEDOUT`，`operation_fail_offset` 令申请成功后的下一次 HAL 操作返回一次
`EIO`（随后原子复位，保证回滚路径不被同一故障污染），
`readback_error_offset` 反转读回值以验证写后读回和事务回滚；兼容参数
`fail_after_operations` 在指定次数的成功 HAL 操作后持续返回 `EIO`，用于验证
`rollback_error` 不覆盖原始失败；`fail_offset` 保留为申请阶段的一般失败。
`tests/integration/mock_smoke_zsh.sh`
覆盖上述路径，并同时运行非法 UAPI 探针、317 组 CLI parser fuzz、1/2/4/8 worker
并发、同线冲突、`SIGKILL` 自动释放、活动租约阻止卸载、event/close 竞争、IRQ、
去抖、epoll、队列溢出及释放后的状态恢复。真实后端的重复加载生命周期可运行：

```sh
sudo GPIOCTL_ZSH_LIFECYCLE_CYCLES=20 tests/stress/module_lifecycle_zsh.sh
```

该测试还断言整个循环不会新增 overlay 动态移除警告。

正式混合压力测试默认运行一小时，组合单线读写、批量事务、同线竞争和 epoll
事件轮次；测试结束必须满足零活动租约且没有新增 GPIO 相关 BUG/Oops/WARNING：

```sh
sudo tests/stress/mixed_mock_zsh.sh

# 快速复验可显式缩短，但不能替代发布前的一小时结果
sudo GPIOCTL_ZSH_STRESS_SECONDS=60 GPIOCTL_ZSH_REPORT_SECONDS=10 \
  tests/stress/mixed_mock_zsh.sh
```

## 文档索引

- [贡献指南](CONTRIBUTING.md)
- [安全策略](SECURITY.md)
- [架构说明](docs/architecture-zsh.md)
- [UAPI 说明](docs/uapi-zsh.md)
- [用户指南](docs/user-guide-zsh.md)
- [开发者指南](docs/developer-guide-zsh.md)
- [部署指南](docs/deployment-guide-zsh.md)
- [设备树安全策略](docs/device-tree-policy-zsh.md)
- [威胁模型](docs/threat-model-zsh.md)
- [需求追踪矩阵](docs/traceability-zsh.md)
- [测试报告](docs/test-report-zsh.md)
- [性能评估](docs/performance-zsh.md)

性能基准程序不属于默认 `all` 或安装目标。本项目持久 FD 工具用 `make benchmark`
显式构建；外部黑盒基线另用 `make benchmark-libgpiod`，只有后者链接 libgpiod 的
公开 API。统一运行器会先检查生产 CLI 的动态依赖，再记录原始逐操作 CSV、CPU/墙钟
数据及统计摘要：

```sh
make benchmark benchmark-libgpiod
sudo benchmarks/run_benchmarks_zsh.sh
```

正式逐样本结果位于 `results/benchmark-20260718-145217/` 和
`results/event-20260718-145236/`。自研单线 get 吞吐为 libgpiod 基线的 105.45%，
lease/release 为 99.98%；单线 set 为 74.68%、8-line batch 为 67.75%，未达到约
90% 目标，原因和完整 P50/P95/P99 均在[性能评估](docs/performance-zsh.md)中公开。

## License

Copyright (C) 2026 Shanghan Zhuang. This project is licensed under
[GPL-2.0-only](LICENSE).

# gpioctl_zsh 架构说明

## 设计目标与边界

本项目从 Linux 字符设备模型出发，自行定义版本化 UAPI、HAL、用户态 C 库和 CLI。
生产路径使用 Linux 已导出的 gpiolib descriptor API；飞腾相关 PAD 寄存器映射只存在
于独立 provider。core 不包含固定控制器数量、物理地址、PAD 球位或 LED 引脚。

系统承诺同一控制器内的软件事务、每文件描述符独占租约和可检测的有界事件流；不
宣称跨控制器原子翻转，也不把没有批量寄存器的顺序写伪装成硬件同时发生。

## 分层

```text
single command / REPL / script / JSON Lines
                    |
             libgpioctl_zsh.a
                    |
       ioctl control + read/poll/epoll events
                    |
            gpioctl_core_zsh.ko
           /          |          \
 gpiolib backend   IOPAD provider   mock backend
       |               |                |
 Linux gpiochip   protected MMIO    deterministic tests
```

- core：动态 minor、字符设备、session、租约、策略、事务、事件、统计和 sysfs。
- gpiolib backend：运行时发现所有 gpiochip，以 descriptor API 请求、配置、读写和
  转 IRQ；`gpiod_cansleep()` 路径始终在可睡眠上下文执行。
- Phytium IOPAD provider：根据设备树资源和 `hardware_key + offset` 解析 PAD，
  只开放 bias、drive level 与 `mux=gpio`，不接受用户提供的地址或掩码。
- mock backend：实现相同 HAL，并提供 busy、timeout、一次性操作失败、错误读回和
  IRQ 注入；生产安装包含该模块但生产加载脚本不会加载它。
- 用户态库：持有 FD，填充固定布局结构，封装 ioctl/read，不解释板级别名。
- CLI：板级配置解析和统一命令执行引擎；单命令、REPL、文件与 stdin 脚本共享语义。

## 核心对象

| 对象 | 所有者 | 关键状态 | 结束条件 |
|---|---|---|---|
| controller | backend 注册 | cdev、策略、全局租约位图、统计 | 无 open FD 后允许注销 |
| session | 每次 `open()` | session mutex、line 数组、事件 ring | `release()` 幂等清理 |
| line state | session | backend handle、方向、电平、IRQ、IOPAD 快照 | 显式释放或 session 关闭 |
| IOPAD provider | provider 模块 | ops、active_calls、unregistering | active_calls 归零后注销 |

打开字符设备会固定 backend 模块；每条成功准备 IOPAD 的租约还固定 provider 模块。
因此活动 FD/租约不能与 backend/provider 卸载形成 use-after-free。锁顺序和具体清理
契约见 [开发者指南](developer-guide-zsh.md)。

## 租约状态序列

```text
open FD
  -> validate complete multi-line request
  -> controller lock -> session lock
  -> verify every global lease bit is free
  -> request backend line
  -> snapshot controlled IOPAD fields and select GPIO mux
  -> publish line/session/global leased state
  -> configure/read/write/event
  -> apply device-tree GPIO safe state
  -> restore pre-lease IOPAD controlled fields
  -> release backend line and provider reference
  -> close FD
```

多线申请只有在全部策略与冲突检查完成后才请求硬件。中途失败会逆序清理已经成功的
线路。关闭、异常退出和显式 release 调用同一释放 helper。

## 事务路径

`BATCH_EXEC` 的最大操作数固定为 32。core 先复制整个结构，完成 ABI、reserved、
数量、opcode、重复 offset、租约、能力和参数验证，并为每个操作保存快照。之后才
顺序提交。输出配置和 SET 都会写后读回；失败时逆序应用快照，`failed_index` 保留
原始失败位置，`rollback_error` 单独报告首次回滚错误。

事务快照只恢复“提交前状态”；FD 最终关闭仍恢复设备树定义的安全状态。

## 事件路径

每个 session 在 `open()` 时预分配 256 条固定记录，不在 IRQ 路径分配内存。后端
IRQ 使用 threaded handler，以允许对 `cansleep` GPIO 采样。采样后在短 event
spinlock 临界区完成软件消抖、序号递增、ring 入队和 overflow 标记，再唤醒等待者。

ring 满时丢弃最旧记录、增加 `event_drops`，并在下一条保留记录上设置
`GPIOCTL_ZSH_EVENT_OVERFLOW`。用户态可用 `read()`、`poll()` 或 `epoll()`，不会
发生无法检测的静默队列覆盖。

## IOPAD 资源模型

设备树 overlay 声明唯一 MMIO resource。platform driver 通过受管资源映射获得
区域所有权；每次更新在 irqsave spinlock 下执行 read-modify-write、readback，失败
则写回旧值。租约快照仅包含 func/drive/bias 受控位，恢复不会覆盖寄存器无关字段。

普通 GPIO 租约可自动选择 GPIO mux；显式 IOPAD 修改还要求持有该 line 的租约和
`CAP_SYS_RAWIO`。只读查询只返回抽象枚举，不泄漏裸寄存器。

## 性能策略

- 控制路径用 mutex，IRQ ring 用最小 spinlock 临界区，避免全局大锁。
- line 数组、租约 bitmap 和事件 ring 预分配；热路径不做字符串解析。
- 多值和 batch ioctl 减少系统调用与用户复制；C 库可保持 FD/租约避免进程启动成本。
- sysfs 仅监控，避免形成绕过租约的第二写路径。
- 所有优化以权限、所有权、写后读回和失败回滚为硬门；raw MMIO 数据不得替代生产
  gpiolib 后端结论。

## raw-MMIO 实验隔离

`lab/raw-mmio-lab` 不进入默认构建或安装。冲突 overlay 保持安全 gpiochip 活动，
只验证重复 `devm_platform_ioremap_resource()` 以 `EBUSY` 失败且不写；隔离 overlay
必须随专用启动环境禁用目标 controller。当前 GPIO4_7 接有 LED、又没有已证明的
隔离启动环境，因此真实 raw 写测试明确 SKIP。实验 RMW helper 也不接受任意地址，
默认只读，获准时先切目标位为输入并在读回后恢复 DR/DDR 快照。

## 原创性边界

生产源码不包含、不链接也不运行时调用 libgpiod。单独且不安装的 benchmark-only
程序只调用公开 libgpiod API，作为同板外部黑盒基线。项目的 UAPI、HAL、对象模型、
命令语法、错误与测试均采用原创
`gpioctl_zsh`/`_zsh` 命名空间。

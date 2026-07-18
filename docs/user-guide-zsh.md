# gpioctl_zsh CLI 用户指南

`gpioctl_zsh` 是通用 GPIO 字符设备控制系统的命令行客户端。它既能执行一次性命令，
也能在同一进程中交互控制或运行脚本。本指南先解释命令中的对象和约定，再给出命令
参考，最后单独提供飞腾派接线示例。

> 本文中的 `TARGET`、`DEVICE`、`OFFSET` 等大写单词是占位符，不应原样输入。
> 第 13 节之前不假设任何开发板或 GPIO 接线。

## 1. 命令模型

所有命令都遵循同一个结构：

```text
gpioctl_zsh [GLOBAL_OPTIONS] COMMAND [ARGUMENTS]
```

- `GLOBAL_OPTIONS`：影响整个进程，必须写在 `COMMAND` 前。
- `COMMAND`：要执行的操作，例如 `get`、`set` 或 `watch`。
- `ARGUMENTS`：该操作的目标和参数。

例如，通用的读取语法是：

```text
gpioctl_zsh get TARGET
```

这里的 `TARGET` 表示一条 GPIO 线。只有先选定实际线路后，才把它替换为板级别名、
通用 GPIO 名或设备路径。第 3 节会完整说明这三种写法。

## 2. 先理解四个概念

### 2.1 控制器与设备（DEVICE）

一个 GPIO 控制器对应一个字符设备：

```text
/dev/gpio<CONTROLLER>_zsh
```

例如控制器编号为 `N` 时，设备记作 `/dev/gpioN_zsh`。`list` 用来发现系统中实际
存在的控制器，而不是靠猜测编号。

### 2.2 线与偏移（OFFSET）

`OFFSET` 是一条线在所属控制器内的从零开始编号。控制器编号和 offset 必须一起
才能唯一定位一条 GPIO 线。

### 2.3 目标（TARGET）

`TARGET` 是 CLI 对“一条 GPIO 线”的统一称呼。它可以是板级别名、通用 GPIO 名，
也可以是设备与 offset 的组合。三种写法最终都会解析为 `DEVICE + OFFSET`。

### 2.4 租约与进程生命周期

GPIO 线在使用前会被当前文件描述符独占租约。另一进程申请同一条线时会得到
`EBUSY`。文件描述符关闭、进程正常退出或被 `kill -9` 终止时，内核都会回收租约，
应用安全状态，并恢复租约前由系统保存的 IOPAD 状态。

这会直接影响命令选择：

- 一次性命令自行申请和释放租约，适合读取、定时输出和短任务。
- `shell`/`run` 可让 `acquire`、`value`、`release` 共享同一进程，适合跨多步保持
  输出状态。
- 一次性 `set` 不是后台常驻服务；命令结束后不会继续占有 GPIO。

## 3. TARGET 的三种写法

解析优先级依次为板级别名、直接设备路径、通用 GPIO 名。

| 写法 | 通用格式 | 信息来源 | 适用场景 |
|---|---|---|---|
| 板级别名 | `<ALIAS>` | 板级配置文件 | 推荐给人使用；可携带有效电平、PAD、物理引脚等元数据 |
| 通用 GPIO 名 | `GPIO<CONTROLLER>_<OFFSET>` | 名称本身 | 已明确控制器和 offset，且不需要板级元数据时 |
| 直接路径 | `/dev/gpio<CONTROLLER>_zsh:<OFFSET>` | 名称本身 | 自动化或底层排障 |

`GPIO<CONTROLLER>_<OFFSET>` 是通用语法，不是对某个引脚的硬编码。例如通用名中的
两个数字只分别表达控制器编号和 offset；它是否连接到排针、LED 或其他外设，必须
由当前开发板资料和板级配置确认。

在操作硬件前，先解析目标：

```text
gpioctl_zsh resolve TARGET
gpioctl_zsh info TARGET
```

- `resolve` 只显示解析结果及板级元数据。
- `info` 还会查询控制器能力、线路能力、安全状态和访问策略。
- 用通用名或直接路径时，未由配置提供的 PAD、物理引脚等字段可能为空。

### 3.1 板级配置的选择顺序

CLI 按以下顺序选择配置文件：

1. 命令行 `--config FILE`。
2. 环境变量 `GPIOCTL_ZSH_BOARD_CONFIG`。
3. 当前工作目录中的 `config/phytium-pi-v1.conf`。
4. 上一级目录中的 `config/phytium-pi-v1.conf`。
5. `/etc/gpioctl_zsh/board.conf`。

配置文件每个非注释行包含七个空白分隔字段：

```text
ALIAS DEVICE OFFSET ACTIVE_LOW PAD PHYSICAL_PIN DESCRIPTION
```

其中 `ACTIVE_LOW` 只能是 `0` 或 `1`。别名不得重复。配置文件不支持带空格的字段。

## 4. 值、方向和时间单位

### 4.1 逻辑值

CLI 接受的 GPIO 值只能是：

- `0`：逻辑无效（inactive）。
- `1`：逻辑有效（active）。

如果板级配置把线路标为低有效，CLI 会在逻辑值和物理电平之间转换。因此 `1`
始终表示“有效”，不一定表示引脚上是高电平。

### 4.2 方向

- `in`：输入。
- `out`：输出。

### 4.3 时间

- 命令中的 `HOLD_MS`、`ON_MS`、`OFF_MS`、`INTERVAL_MS`、`TIMEOUT_MS` 和
  `MILLISECONDS` 均为毫秒。
- `watch` 的 `DEBOUNCE_US` 为微秒。
- 单次持续时间和总重复持续时间上限为 24 小时，重复次数上限为 100000。

## 5. 第一次使用

先确认驱动已加载、设备存在，再选择并核对目标：

```text
gpioctl_zsh list
gpioctl_zsh resolve TARGET
gpioctl_zsh info TARGET
```

建议先用 `--dry-run` 验证会改变输出的命令：

```text
gpioctl_zsh --dry-run set TARGET 1 1000
```

对 `set`、`blink` 等控制命令，`--dry-run` 会解析并验证计划，但不会改变硬件或
实际等待。`list`、`info`、`stats`、`iopad-get` 是查询命令，仍会打开设备读取实际
信息。

设备默认权限为 `0660 root:gpio`。管理员可把普通用户加入 `gpio` 组：

```sh
sudo usermod -aG gpio "$USER"
```

重新登录后用 `id` 确认组成员身份。不要把 GPIO 设备改成 `0666`。即使能够打开
设备，内核仍会执行 allowlist、reserved line 和能力检查。

## 6. 命令总览

| 命令族 | 命令 | 用途 |
|---|---|---|
| 发现与检查 | `list`、`resolve`、`info`、`stats` | 查设备、映射、能力和统计 |
| 一次性 GPIO | `get`、`set`、`blink`、`pair-blink` | 读取或短时控制 GPIO |
| 事件 | `watch` | 等待输入边沿 |
| 同控制器批量操作 | `batch-set` | 一次申请并配置多条线 |
| IOPAD | `iopad-get`、`iopad` | 查询或修改复用、偏置和驱动档位 |
| 持久会话 | `shell`、`run`、`acquire`、`value`、`release`、`sleep` | 交互式和脚本化控制 |
| 事务块 | `transaction`、`tx-line`、`commit`、`abort` | 在会话中构造批量配置 |

## 7. 发现与检查

### 7.1 列出控制器

```text
gpioctl_zsh list
```

扫描 `/dev/gpio*_zsh`，显示控制器编号、线路数量和能力位。它列出字符设备，不列出
板级配置中的全部别名。

### 7.2 解析目标

```text
gpioctl_zsh resolve TARGET
```

显示别名、设备、offset、有效电平、PAD、物理引脚和描述。它不申请 GPIO 租约。

### 7.3 查询线路信息

```text
gpioctl_zsh info TARGET
```

在解析结果之外，查询控制器/线路能力、访问策略以及释放时采用的安全状态。

### 7.4 查询统计

```text
gpioctl_zsh stats TARGET
gpioctl_zsh stats DEVICE
```

显示操作数、错误数、拒绝数、租约冲突、事件、丢弃事件和活动租约数。传入
`TARGET` 时，CLI 会先解析出所属 `DEVICE`。

## 8. 一次性 GPIO 操作

### 8.1 读取

```text
gpioctl_zsh get TARGET
```

把目标申请为输入，读取一次逻辑值后释放。

### 8.2 设置并可选保持

```text
gpioctl_zsh set TARGET VALUE [HOLD_MS]
```

把目标申请为输出并设为 `VALUE`。如果给出 `HOLD_MS`，进程保持租约指定时长；
随后关闭文件描述符并恢复安全状态。省略时保持时间为 `0`。

### 8.3 单线闪烁

```text
gpioctl_zsh blink TARGET COUNT ON_MS OFF_MS
```

每轮先输出逻辑 `1` 并等待 `ON_MS`，再输出逻辑 `0` 并等待 `OFF_MS`，共执行
`COUNT` 轮，最后释放线路。

### 8.4 两线交替

```text
gpioctl_zsh pair-blink TARGET_A TARGET_B COUNT INTERVAL_MS
```

每轮先令 A/B 为 `1/0`，等待 `INTERVAL_MS`，再令 A/B 为 `0/1` 并再次等待。
结束前两线都写为 `0`。两条线可以来自不同控制器；此时写入按顺序发生，不承诺
跨控制器硬件原子或严格同时。

### 8.5 同控制器批量输出

```text
gpioctl_zsh batch-set DEVICE HOLD_MS OFFSET=VALUE [OFFSET=VALUE ...]
```

一次申请同一控制器上的多条线并提交批量配置。每个 `VALUE` 只能为 `0` 或 `1`。
提交失败时内核按提交前快照回滚；错误输出包含失败项索引和回滚错误。命令完成后
释放全部租约。该命令直接使用 `DEVICE + OFFSET`，不会读取板级别名中的
`ACTIVE_LOW`；如需低有效配置，使用事务块的 `active-low` 参数。

## 9. 边沿事件

```text
gpioctl_zsh watch TARGET rising|falling|both TIMEOUT_MS [COUNT] [DEBOUNCE_US]
```

- 边沿为 `rising`、`falling` 或 `both`。
- `TIMEOUT_MS` 是等待下一批事件的超时。
- `COUNT` 默认为 `1`；设为 `0` 时持续监听，直到出错、总预算耗尽或被终止。
- `DEBOUNCE_US` 默认为 `0`，表示不配置消抖。

每个事件包含目标、边沿、单调时钟时间戳、序号和标志。超时返回执行失败，而不是
伪造一个空事件。

## 10. IOPAD

### 10.1 查询

```text
gpioctl_zsh iopad-get TARGET
```

显示抽象的 `bias`、驱动档位和 `gpio/other` 复用状态。读取不需要修改特权，但
backend 和目标线路必须支持 IOPAD 查询。

### 10.2 修改

```text
gpioctl_zsh iopad TARGET [mux=gpio] [bias=none|up|down] [drive=0..15]
```

至少提供一个设置项。未提供的字段保持原值。CLI 不接受物理地址、寄存器 mask 或
任意 `funcN`，只允许受约束的抽象配置。修改通常需要 root 或 `CAP_SYS_RAWIO`：

```text
sudo gpioctl_zsh iopad TARGET mux=gpio bias=none drive=DRIVE_LEVEL
```

其中 `DRIVE_LEVEL` 为 `0..15`，它是平台定义的档位，不应未经数据手册确认就当作
毫安值。该命令的修改作用域是它临时持有的租约；命令关闭文件描述符时，系统会按
统一生命周期规则恢复租约前状态。正常 GPIO 租约会自动选择并在释放后恢复 GPIO
复用，一般不需要先执行一条持久化的 `iopad` 命令。

## 11. 交互式与脚本化控制

一次性命令各自运行在新进程中，不能把租约带给下一条 Shell 命令。要持续持有
GPIO，必须让相关命令运行在同一个 `shell` 或 `run` 进程中。

### 11.1 会话命令

```text
acquire TARGET in|out [INITIAL_VALUE]
value TARGET [VALUE]
release TARGET
sleep MILLISECONDS
```

- `acquire`：申请并保持租约；输出的 `INITIAL_VALUE` 默认为 `0`。
- `value TARGET`：读取已持有线路。
- `value TARGET VALUE`：写入已持有线路。
- `release`：释放指定线路。
- `sleep`：在不释放现有租约的前提下等待。

一个进程最多同时持有 32 条线。会话退出时仍持有的线路会自动清理。

### 11.2 交互模式

```text
gpioctl_zsh shell
```

进入后逐行输入会话命令。`help` 显示命令摘要，`quit` 或 `exit` 退出。例：

```text
gpioctl_zsh> acquire TARGET out 0
gpioctl_zsh> value TARGET 1
gpioctl_zsh> sleep 1000
gpioctl_zsh> value TARGET 0
gpioctl_zsh> release TARGET
gpioctl_zsh> quit
```

上面的 `TARGET` 仍是占位符，实际操作时替换为已经核对过的目标。

### 11.3 文件脚本和标准输入

脚本文件：

```text
# example.gpioctl
acquire TARGET out 0
value TARGET 1
sleep 1000
value TARGET 0
release TARGET
```

运行文件或从标准输入读取：

```text
gpioctl_zsh --strict run example.gpioctl
gpioctl_zsh --strict run - < example.gpioctl
```

脚本按空白切分 token，`#` 后为注释；不支持引号、变量展开、循环和条件。需要复杂
逻辑时由 Shell、Python 等通用语言生成命令流，或多次调用 CLI。

`--strict` 在第一条失败命令处停止。不加 `--strict` 时会继续执行后续行，但只要
有任一行失败，进程最终仍返回非零。错误会包含脚本来源和行号。

### 11.4 事务块

事务块只能在 `shell` 或 `run` 的同一进程内使用：

```text
transaction DEVICE
tx-line OFFSET in|out VALUE [active-low]
tx-line OFFSET in|out VALUE [active-low]
commit [HOLD_MS]
```

`transaction` 开始计划，`tx-line` 只在用户态收集操作，`commit` 才一次申请全部
offset 并发出一个 batch ioctl。也可用 `abort` 放弃计划。

事务只允许一个控制器；重复 offset、嵌套事务和空事务都会失败。文件结束或退出
时仍未 `commit`/`abort` 的事务会被放弃并令进程返回失败。批量提交提供失败回滚，
但不声明多条物理 GPIO 在完全相同的硬件时刻翻转。

## 12. 全局选项与自动化接口

全局选项必须写在命令之前：

| 选项 | 含义 |
|---|---|
| `--config FILE` | 使用指定板级配置；优先级最高 |
| `--dry-run` | 控制命令只解析和验证，不改硬件或等待；查询命令仍读取设备 |
| `--json` | 以 JSON Lines 输出，适合程序逐行解析 |
| `--strict` | 脚本遇到首个错误即停止；`list` 遇到设备检查错误也停止 |
| `--timeout MS` | 从进程启动计算的总时间预算，范围为 1 毫秒到 24 小时 |

正确位置：

```text
gpioctl_zsh --json --timeout 5000 info TARGET
```

### 12.1 JSON Lines

`--json` 下，stdout/stderr 的每个非空行都是一个独立、完整的 JSON object，并含
布尔字段 `ok`。多事件和多命令会输出多行，不能把整个输出一次性解析为单个 JSON。

### 12.2 总超时

`--timeout` 是整个进程的单调时钟预算，不会为脚本每一行或 `watch` 每个事件重置。
预算耗尽返回 `ETIMEDOUT`。

### 12.3 退出状态

| 状态码 | 含义 |
|---:|---|
| `0` | 所有请求成功 |
| `1` | 运行时或硬件操作失败 |
| `2` | 全局选项错误，或没有提供命令 |

命令名称、参数数量或参数值错误由当前实现按执行失败返回 `1`。

Shell 自动化必须检查退出状态，不应只匹配人类可读文本。

## 13. 飞腾派课程板示例

本节才使用当前课程板的具体别名。它们来自
`config/phytium-pi-v1.conf`，不是 CLI 通用语法的一部分，也不能直接套用到其他
开发板。

| 板级别名 | 解析结果 | PAD / 位置 | 当前课程接线 |
|---|---|---|---|
| `LED20` | `/dev/gpio1_zsh:8`，低有效 | E37 / 板载 | 板载 LED20 |
| `GPIO1_11` | `/dev/gpio1_zsh:11`，高有效 | BA49 / 物理 15 | 外接 LED 1 |
| `GPIO4_7` | `/dev/gpio4_zsh:7`，高有效 | W53 / 物理 19 | 外接 LED 2 |

外接 LED 必须各自串联 `330Ω..1kΩ` 限流电阻。GPIO 内部上拉不能替代限流电阻。
先核对映射和能力：

```sh
gpioctl_zsh resolve GPIO1_11
gpioctl_zsh info GPIO1_11
gpioctl_zsh iopad-get GPIO1_11
```

确认接线后，让 GPIO1_11 亮 1 秒再安全释放：

```sh
gpioctl_zsh set GPIO1_11 1 1000
```

让板载 LED20 闪烁三轮，每次亮、灭各 1 秒：

```sh
gpioctl_zsh blink LED20 3 1000 1000
```

让两盏外接 LED 交替三轮，每个状态保持 1 秒：

```sh
gpioctl_zsh pair-blink GPIO1_11 GPIO4_7 3 1000
```

用脚本持续持有 GPIO1_11：

```text
# gpio1_11-demo.gpioctl
acquire GPIO1_11 out 0
value GPIO1_11 1
sleep 1000
value GPIO1_11 0
release GPIO1_11
```

```sh
gpioctl_zsh --strict run gpio1_11-demo.gpioctl
```

## 14. 常见问题

| 现象 | 原因与处理 |
|---|---|
| `Device or resource busy` | 另一文件描述符持有独占租约；等待其释放或终止所有者 |
| `Permission denied` | 当前用户不在 `gpio` 组、线路不在 allowlist，或 IOPAD 修改缺少特权 |
| `Operation not permitted` | 线路被保留、未持有租约、input-only 或策略禁止输出 |
| `Operation not supported` | 当前 backend 或线路不支持请求的 bias、IRQ 或 IOPAD 能力 |
| `Input/output error` | 后端失败或写后读回不一致；检查内核日志、接线和复用 |
| `set` 返回后灯熄灭 | 这是关闭 FD 后恢复安全状态；需要持续输出时使用 `shell`/`run` 租约 |
| 通用 GPIO 名能解析但操作失败 | 名称语法正确不等于线路存在或策略允许；用 `list` 和 `info` 核对 |
| 脚本继续执行了后续行 | 默认收集多个错误；需要首错停止时加 `--strict` |

## 15. 相关文档

- [项目 README](../README.md)
- [部署指南](deployment-guide-zsh.md)
- [UAPI 说明](uapi-zsh.md)
- [开发者指南](developer-guide-zsh.md)
- [GPIO 设备树安全策略](device-tree-policy-zsh.md)

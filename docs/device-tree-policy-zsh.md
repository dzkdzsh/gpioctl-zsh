# GPIO 设备树安全策略

GPIO 线路的授权范围和释放状态属于板级事实，不写死在通用 core。飞腾派覆盖层在
对应 GPIO 控制器节点上使用以下属性：

```dts
gpioctl-zsh,line-policies =
	<offset flags safe-direction safe-physical-value safe-bias>, ...;
```

每条记录固定为 5 个 `u32` cell：

| cell | 含义 |
|---|---|
| `offset` | 控制器内线路编号，必须小于 `ngpio` 且不得重复 |
| `flags` | `0x1` 普通用户可租约；`0x2` 允许输出；`0x4` 保留线路 |
| `safe-direction` | `0` 输入，`1` 输出 |
| `safe-physical-value` | 释放后的物理电平，只能为 0 或 1，不受 `active_low` 解释影响 |
| `safe-bias` | `1` 禁用、`2` 上拉、`3` 下拉；必须显式声明 |

未出现的线路使用保守默认值：仅具有 `CAP_SYS_RAWIO` 的进程能够申请输入租约，
不允许切换为输出，释放时恢复输入并禁用 bias。`0x4` 保留线路对包括 root 在内的
所有调用者拒绝租约，避免管理员误操作绕过硬件保留边界。

core 会拒绝未知 flag、越界/重复 offset、非法枚举、保留 flag 与其他 flag 混用、
以及“安全输出但未声明允许输出”等矛盾配置。设备树解析或策略校验失败时，该
控制器不会注册成 `/dev/gpioN_zsh`，而不是带着部分策略继续运行。

## 飞腾派 40-pin 白名单

以下映射来自课程提供的飞腾派 GPIO 表；只开放表中能够确认的 GPIO。GPIO5 没有
经课程资料确认连接到 40-pin 排针，因此默认不开放。

| 控制器 | offset（物理引脚） |
|---|---|
| GPIO0 | 0（36）、14（22） |
| GPIO1 | 1（32）、5（18）、8（LED20）、11（15）、12（13） |
| GPIO2 | 1（12）、2（35）、5（40）、6（38）、8（33）、10（7） |
| GPIO3 | 0（8）、1（11）、2（16） |
| GPIO4 | 6（23）、7（19）、8（21）、9（24）、10（26）、11（29）、12（31）、13（37）、14（28）、15（27） |
| GPIO5 | 无 |

普通排针线路释放为输入且禁用 bias。三个已知 LED 夹具采用输出安全态：LED20
（GPIO1_8，低有效）释放为物理 1；GPIO1_11 和 GPIO4_7（高有效）释放为物理 0，
三者均为灭灯。

## 释放与 IOPAD fallback

每次显式 `release`、进程正常退出、异常关闭或租约申请回滚都会尝试应用同一安全
策略，然后无条件归还 GPIO descriptor 和租约位。输出安全态先恢复 bias 再以目标
电平切换输出；输入安全态先切输入再恢复 bias。

core 首先调用通用 GPIO 后端的 `set_bias`。只有它明确返回 `ENOTSUPP` 或
`EOPNOTSUPP` 时，才用 `hardware_key + offset` 调用匹配的 IOPAD provider；其他
真实错误直接返回，不能被 fallback 掩盖。飞腾板的 gpiolib 驱动不提供通用
pinconf bias，因此实际由受 MMIO spinlock、读回校验和失败回滚保护的飞腾 IOPAD
provider 完成。直接修改 mux/drive 的 IOPAD ioctl 仍要求 `CAP_SYS_RAWIO`。
只读 IOPAD 查询在同一 MMIO spinlock 下采样，并只输出 bias、0..15 驱动档位和
`gpio/other` 复用分类；它可用于独立核对配置读回，但不会暴露裸 `funcN` 或
寄存器内容。

监控节点 `/sys/class/gpioctl_zsh/gpioN_zsh/` 提供 `allowlisted_lines`、
`output_lines` 和 `reserved_lines` 计数，可用于确认覆盖层是否真正绑定到控制器。

# gpioctl_zsh 威胁模型

## 保护对象

- GPIO/IOPAD 电气安全：避免争用、错误复用、过流或把未知引脚驱动为输出。
- 内核完整性：避免越界、UAF、竞态、睡眠上下文错误和任意 MMIO。
- 所有权：一个进程不能控制另一个 FD 的 line。
- 可恢复性：进程崩溃、部分事务失败和模块生命周期不能遗留失控输出。
- 可观测性：事件丢失、权限拒绝和冲突必须可检测。

## 攻击者与信任边界

- 普通本地用户：可能属于或不属于 `gpio` 组，可构造任意 ioctl 字节和 CLI 输入。
- `gpio` 组用户：被允许操作明确 allowlist，但不可信任其参数或调用频率。
- root/`CAP_SYS_RAWIO`：可修改 IOPAD，但仍不能绕过 reserved、结构校验或资源所有权。
- 设备树、板级配置、内核和已签收模块属于管理员信任域；错误 DT 仍需 fail closed。
- 物理攻击、电源/电压不兼容和恶意内核模块超出本项目能完全防御的范围。

## 攻击面与控制

| 攻击面 | 主要风险 | 控制 |
|---|---|---|
| ioctl | 长度/数量溢出、非法枚举、坏指针 | 固定宽度、精确 size、上限、reserved、uaccess |
| 字符设备权限 | 任意用户驱动输出 | udev `0660 root:gpio` + 每线 DT 策略 |
| 租约 | 跨进程争用、退出泄漏 | 每 FD 独占、全局 bitmap、模块引用、close 同路清理 |
| batch | 校验后部分硬件修改、错误读回 | 修改前全验证、快照、写后读回、逆序回滚 |
| IRQ/event | IRQ 中睡眠、分配、静默覆盖 | threaded IRQ、预分配 ring、短 spinlock、overflow/drop |
| IOPAD | 任意地址/func、RMW 丢更新 | 无裸地址 UAPI、能力枚举、CAP、resource、spinlock、readback |
| sysfs | 绕过租约的第二控制面 | 只读监控，无方向/电平/复用写节点 |
| CLI/script | 超长 token、无限等待、非机器输出 | 长度/数字校验、总预算上限、JSON Lines、fuzz |
| 模块卸载 | 活动对象 UAF | backend/provider module ref、注销 busy、生命周期测试 |
| DT overlay | 重复移除泄漏 | 日常 unload 保留 overlay，完整卸载才显式删除 |
| raw lab | 与 gpiochip 重复映射、误驱动 | 默认不构建；独占 resource；双重隔离门；无任意地址接口 |

## Fail-closed 规则

- DT policy 记录非法、重复或矛盾时整个 controller 不注册。
- 未描述 line 默认仅特权输入且安全释放为输入/无偏置；reserved 对 root 也拒绝。
- 不支持能力返回 `EOPNOTSUPP`，不能静默假装成功。
- IOPAD resource 冲突返回 `EBUSY`，不能强行映射或解绑现有 gpiochip。
- 用户复制失败前后都不得留下半提交状态；任何错误保留明确 errno。

## 剩余风险

- 没有硬件 batch 寄存器时，多线变化存在顺序时间差；文档不承诺硬原子。
- threaded IRQ 忙时硬件/irqchip 可能合并边沿；驱动能报告自身 ring drop，但无法
  重建未送达 handler 的物理边沿。严格测量需外部信号源/逻辑分析仪。
- 当前运行内核未启用 KASAN、UBSAN、LOCKDEP、KUnit；源码工件和替代测试不能
  等同于这些动态检测，测试报告必须保留此限制。
- root 可加载其他模块或直接访问 `/dev/mem` 的系统不在本驱动权限模型内。
- raw lab 的 isolated overlay 未在专用启动镜像验证，故真实写测试保持 SKIP；冲突
  probe 只能证明当前安全 controller 的资源所有权门。
- PAD drive level `0..15` 没有可靠 mA 表，因此不对外伪造 mA；使用者须遵守板卡
  电气手册。

## 验证映射

非法 UAPI、CLI fuzz、权限、reserved、冲突、故障回滚、错误读回、事件 overflow、
强杀、活动租约卸载和模块循环均有自动化用例。静态分析、正式一小时负载和黑盒
benchmark 的版本/原始输出应随最终测试报告保存。

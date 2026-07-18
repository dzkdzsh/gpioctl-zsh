# gpioctl_zsh 构建与部署

## 已验证环境

- 飞腾派，AArch64
- Linux `6.6.63-phytium-embedded-v3.2`
- GCC 12.2、GNU Make、device-tree-compiler

Linux 6.6 是实现基线；其他内核只有完成实际构建和测试后才能标为已验证。
仓库不依赖固定工作目录；下文使用克隆后的仓库根目录。

## 从电脑传到板子

Release 源码解压后，先在电脑终端进入解压目录的上一级，再传输整个源码目录。以下
PowerShell 示例中的 `<BOARD_IP>` 必须替换为板子的实际 IP：

```powershell
scp -r .\gpioctl-zsh-0.1.0 zsh@<BOARD_IP>:~/
ssh zsh@<BOARD_IP>
```

内核模块必须针对板上运行的内核构建，所以后续命令均在板子上执行。

## 板上构建

```sh
cd ~/gpioctl-zsh-0.1.0

uname -r
test -f "/lib/modules/$(uname -r)/build/Makefile" || {
    echo "缺少当前运行内核的头文件/构建目录" >&2
    exit 1
}

sudo apt-get update
sudo apt-get install -y build-essential device-tree-compiler
make -j"$(nproc)"
make check
make kunit
```

`build-essential` 和 `device-tree-compiler` 只安装编译器、GNU Make 与 DTC，不会安装
飞腾厂商内核的匹配头文件。若上述检查失败，必须先为 `uname -r` 显示的内核准备
`/lib/modules/$(uname -r)/build`，不能直接继续 `make`。

`make check` 运行 UAPI 布局、同源纯逻辑和 CLI self-test。当前板内核未启用
`CONFIG_KUNIT`，因此 `make kunit` 明确显示 SKIP；KUnit 模块可以用当前头文件
编译，但不能声称在该运行内核执行过。

输出复制到 `build/kernel`、`build/userspace`、`build/dts`。内核 Kbuild 会在
`kernel/` 暂存中间对象，`make clean` 会删除它们。

## 安装与首次加载

```sh
sudo make install
sudo ./scripts/load_zsh.sh
ls -l /dev/gpio*_zsh
sudo gpioctl_zsh list
```

首次使用 `sudo gpioctl_zsh list` 可绕过用户组尚未重新登录的问题，而且该命令只读、
不改变 GPIO 电平。`load_zsh.sh` 显示 `overlay=applied`、`list` 能列出控制器且设备
节点存在，即完成最小部署验证。

安装内容：

| 路径 | 内容 |
|---|---|
| `/lib/modules/$(uname -r)/kernel/test/course_design_zsh/` | 四个 `.ko` |
| `/usr/local/bin/gpioctl_zsh` | CLI |
| `/usr/local/lib/libgpioctl_zsh.a` | C 静态库 |
| `/usr/local/include/` | 库头和 UAPI |
| `/etc/gpioctl_zsh/board.conf` | 飞腾派板级别名 |
| `/usr/lib/gpioctl_zsh/*.dtbo` | 安全策略/IOPAD overlay |
| `/etc/udev/rules.d/70-gpioctl-zsh.rules` | `root:gpio 0660` |

安装脚本执行 `depmod`、重载 udev 规则，并只创建系统组 `gpio`，不会自动扩大成员。

## 普通重载与 overlay

```sh
sudo ./scripts/unload_zsh.sh
sudo ./scripts/load_zsh.sh
```

普通 unload 保留 configfs 中已应用的 overlay。Linux 对动态删除覆盖到既有 DT 节点
的属性会报告不可回收内存风险，因此开发、mock 和压力循环不得反复删除/重建。
`load_zsh.sh` 会复用状态为 `applied` 的 overlay。

若有活动 FD/租约，backend 模块引用会使卸载失败；先结束客户端，不要强制 rmmod。

## 完整卸载

```sh
sudo make uninstall
```

完整卸载显式删除 overlay、模块、CLI、库、头、配置和 udev 规则，并运行 `depmod`。
动态 overlay 的一次性删除警告属于当前内核机制限制；计划继续使用时应保留 overlay，
而不是用 uninstall 做日常重载。

## 板级验收

```sh
sudo tests/integration/mock_smoke_zsh.sh
GPIOCTL_ZSH_CYCLES=1 GPIOCTL_ZSH_INTERVAL_MS=100 \
  tests/integration/phytium_led_smoke_zsh.sh
sudo GPIOCTL_ZSH_LIFECYCLE_CYCLES=20 \
  tests/stress/module_lifecycle_zsh.sh
make audit
```

发布前另需一小时 mixed stress、静态分析与 benchmark。测试结束检查：

```sh
for f in /sys/class/gpioctl_zsh/gpio*_zsh/active_leases; do
  printf '%s=%s\n' "$f" "$(cat "$f")"
done
sudo dmesg --level=err,warn | tail -n 100
```

所有活动租约必须为 0；预期故障注入日志必须与测试窗口和注入项对应，不能把未知
warning 当成通过。

`lab/raw-mmio-lab` 是不安装的独立工件。普通部署不得构建、复制或加载其中模块和
overlay；无写 `EBUSY` 冲突验证及专用隔离启动步骤只按该目录 README 执行。

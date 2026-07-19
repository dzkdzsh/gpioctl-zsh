# gpioctl-zsh 开机启动与重启恢复

本教程区分三个容易混淆的动作：安装、当前会话启动和开机自启动。安装只需执行
一次；板子重启后不需要重新编译或重新安装。

## 行为速查

| 目的 | 命令 |
|---|---|
| 重启后手动启动 | `sudo systemctl start gpioctl-zsh.service` |
| 停止当前服务 | `sudo systemctl stop gpioctl-zsh.service` |
| 启用以后每次开机启动 | `sudo gpioctl-zsh-autostart enable` |
| 取消以后开机启动 | `sudo gpioctl-zsh-autostart disable` |
| 查询当前状态 | `sudo gpioctl-zsh-autostart status` |
| 查看本次开机日志 | `sudo journalctl -b -u gpioctl-zsh.service --no-pager` |
| 持续跟踪日志 | `sudo journalctl -f -u gpioctl-zsh.service` |

`disable` 只取消未来的开机触发，不中断当前客户端。需要立即停止时另行执行
`systemctl stop`。

## 安装完成后的状态

```sh
sudo make install
systemctl is-active gpioctl-zsh.service
systemctl is-enabled gpioctl-zsh.service
```

预期输出分别为 `active` 和 `disabled`：安装器已为当前会话加载驱动，但没有擅自
修改开机策略。安装器还会打印下面的提示：

```text
boot autostart remains disabled; enable it with: sudo gpioctl-zsh-autostart enable
```

## 板子重启后手动启动

如果没有启用自启动，重启后执行：

```sh
sudo systemctl start gpioctl-zsh.service
sudo systemctl status gpioctl-zsh.service --no-pager -l
ls -l /dev/gpio*_zsh
gpioctl_zsh list
```

这不是重新安装。systemd 只是调用已经安装到
`/usr/libexec/gpioctl-zsh/load_zsh.sh` 的加载器，挂载/复用 configfs、应用设备树
overlay，并按 `core → gpiolib backend → Phytium backend` 的顺序加载模块。

## 设置板端开机自启动

只需设置一次：

```sh
sudo gpioctl-zsh-autostart enable
```

该命令等价于：

```sh
sudo systemctl reset-failed gpioctl-zsh.service
sudo systemctl enable --now gpioctl-zsh.service
```

以后每次进入 `multi-user.target` 时 systemd 都会启动该服务。它只恢复驱动、overlay
和 `/dev/gpioN_zsh`，不会保存或恢复任何引脚的高低电平。具体应用应在取得租约后
显式配置输出，避免重启后误驱动 LED、继电器或其他外设。

## 取消开机自启动

```sh
sudo gpioctl-zsh-autostart disable
```

底层等价于 `sudo systemctl disable gpioctl-zsh.service`。当前会话仍保持运行；如需
同时停止：

```sh
sudo systemctl stop gpioctl-zsh.service
```

停止前先关闭所有 GPIO 客户端。活动文件描述符或租约会阻止 backend 安全卸载，
不应使用强制 `rmmod` 绕过引用计数。

## 证明本次开机确实自动启动

重启板子后执行：

```sh
systemctl is-enabled gpioctl-zsh.service
systemctl is-active gpioctl-zsh.service
systemctl show gpioctl-zsh.service \
  -p ActiveEnterTimestamp -p ExecMainStatus -p Result
sudo journalctl -b -u gpioctl-zsh.service --no-pager
ls -l /dev/gpio*_zsh
gpioctl_zsh list
```

应看到 `enabled`、`active`、`Result=success`、本次开机中的
`gpioctl_zsh loaded; overlay=applied`，以及可访问的设备节点。

## 故障排查

### 内核升级后启动失败

内核模块必须与 `uname -r` 对应。服务不会在开机时偷偷编译。先检查：

```sh
uname -r
find "/lib/modules/$(uname -r)" -name 'gpioctl_*_zsh.ko' -print
sudo systemctl status gpioctl-zsh.service --no-pager -l
sudo journalctl -b -u gpioctl-zsh.service --no-pager
```

如果当前内核目录没有模块，或日志报告 `Exec format error` / `invalid module format`，
请准备与当前内核完全匹配的构建目录，在对应版本源码中重新运行：

```sh
make -j"$(nproc)"
make check
sudo make install
```

修复后执行：

```sh
sudo systemctl reset-failed gpioctl-zsh.service
sudo systemctl start gpioctl-zsh.service
```

### 服务 active 但没有设备节点

```sh
cat /sys/kernel/config/device-tree/overlays/gpioctl_zsh/status
lsmod | grep gpioctl
sudo udevadm trigger --subsystem-match=gpioctl_zsh
sudo journalctl -b -u gpioctl-zsh.service --no-pager
```

不要通过循环卸载/重建 overlay 处理普通权限问题；先检查 `gpio` 组、udev 规则和
`/etc/gpioctl_zsh/board.conf`。

## English quick reference

Installation is a one-time operation. After a reboot, either start the already
installed stack manually with `sudo systemctl start gpioctl-zsh.service`, or
enable boot persistence once with `sudo gpioctl-zsh-autostart enable`. Inspect
the current boot with `sudo journalctl -b -u gpioctl-zsh.service`. Autostart
restores only the driver, Device Tree overlay, and character devices; it never
restores previous GPIO output values.

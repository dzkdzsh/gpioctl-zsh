# raw-MMIO 隔离实验

本目录不进入顶层 `make all`、安装包或生产加载路径。生产结论只来自
`gpioctl_backend_gpiolib_zsh`；这里用于验证计划中的资源所有权硬门，数据必须在
报告中单列。

## 两个 overlay

- `conflict-probe-zsh.dts` 不禁用 GPIO4，只声明同一 `0x28038000..0x28038fff`
  资源。安全 gpiochip 正常占用时，lab probe 必须因独占资源申请失败而返回
  `EBUSY`。此模式不执行任何寄存器写。
- `isolated-gpio4-zsh.dts` 禁用安全 GPIO4 节点，另建带
  `zsh,isolation-confirmed` 的实验节点。它只能随专用测试启动环境加载；不得在当前
  运行系统动态套用，不能自动解绑 gpiochip，也不能用于接着外设的线路。

## 安全门

`devm_platform_ioremap_resource()` 同时申请独占 memory resource；失败直接终止 probe，
代码没有强制映射后备路径。默认 `allow_write=0`，即使隔离节点成功 probe 也不写。

只有专用启动环境、确认 GPIO4 所有引脚断开后，才可人工加载：

```sh
sudo insmod gpioctl_raw_lab_zsh.ko allow_write=1 selftest_mask=0x1 \
  selftest_value=0x1
```

自检只接受低 16 位 mask/value。spinlock 下先把 mask 对应 DDR 位清零为输入，再对
DR 做掩码 RMW 和读回，最后无条件恢复原 DR、DDR 并读回完成 posted write。它不
提供字符设备、ioctl、sysfs 写入口或任意地址参数。

当前课程板连接了 GPIO4_7 LED，且没有独立重启测试镜像证明 GPIO4 完全隔离，因此
只允许构建和执行 conflict/`EBUSY` 测试；`isolated-gpio4-zsh` 真实写测试应记录为
`SKIP (isolation not proven)`，不能用生产 IOPAD provider 的结果冒充。

无写冲突测试可复现为：

```sh
make -C lab/raw-mmio-lab all dts
sudo lab/raw-mmio-lab/test_conflict_zsh.sh
```

脚本只加载 `allow_write=0`，验证 lab driver 未绑定、日志 errno 为 `EBUSY` 且
`/dev/gpio4_zsh` 仍存在；退出时删除本次新建节点并卸载 lab 模块。

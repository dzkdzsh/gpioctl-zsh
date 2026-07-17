# Dependency and originality statement

This project is an independent implementation. It does not copy, compile,
link, load, or call libgpiod. libgpiod may only be executed as an external
black-box baseline by benchmark scripts.

Required build dependencies are:

- Linux kernel headers for the target kernel
- GCC and GNU Make
- libc development headers for the userspace library and CLI
- device-tree-compiler when building optional overlays

The kernel modules use public, exported Linux GPIO, pinctrl, character-device,
IRQ, synchronization, and uaccess interfaces. The userspace component talks
only to the original `gpioctl_zsh` UAPI defined in this repository.


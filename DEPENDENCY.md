# Dependency and originality statement

This project is an independent implementation. Production and default-build
artifacts do not copy, compile, link, load, or call libgpiod. An optional,
explicitly built benchmark-only executable links only libgpiod's public API so
that both implementations can be measured in persistent processes without
including process startup time. That executable is not installed and is never
used by the product library, CLI, kernel modules, tests, or deployment scripts.

Required build dependencies are:

- Linux kernel headers for the target kernel
- GCC and GNU Make
- libc development headers for the userspace library and CLI
- device-tree-compiler when building optional overlays

The kernel modules use public, exported Linux GPIO, pinctrl, character-device,
IRQ, synchronization, and uaccess interfaces. The userspace component talks
only to the original `gpioctl_zsh` UAPI defined in this repository.

# Security Policy

`gpioctl-zsh` operates in kernel space and can change GPIO and IOPAD state.
Please do not publish exploit details, privileged crash reproducers, or unsafe
MMIO procedures in a public Issue before a fix is available.

## Supported versions

| Version | Security fixes |
|---|---|
| `0.1.x` | Supported |
| Unreleased development snapshots | Best effort |

## Reporting a vulnerability

Use the repository's **Security → Report a vulnerability** flow to submit a
private GitHub Security Advisory. Include:

- affected commit or version;
- kernel, architecture, backend, and board information;
- required privileges and whether physical access is needed;
- a minimal reproducer and expected/actual behavior;
- relevant kernel logs with credentials and personal paths removed;
- your assessment of impact and any suggested mitigation.

The maintainer will acknowledge a complete report when available, validate it
against the documented threat model, and coordinate disclosure after a fix or
mitigation exists. Do not attach secrets, production data, or uncontrolled raw
MMIO payloads.

## 中文说明

安全问题请通过 GitHub 私有漏洞报告提交，不要先创建公开 Issue。报告应包含受影响
版本、内核和硬件环境、所需权限、最小复现、去除凭据后的日志，以及影响分析。
涉及任意地址 MMIO、越权 ioctl、用户指针、租约绕过、竞态、释放后使用或内核崩溃
的问题均按安全问题处理。

// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "uapi/gpioctl_zsh.h"

static void fail_zsh(const char *what)
{
	perror(what);
	exit(EXIT_FAILURE);
}

static void expect_errno_zsh(int fd, unsigned long command, void *argument,
			     int expected, const char *what)
{
	int ret;

	errno = 0;
	ret = ioctl(fd, command, argument);
	if (ret != -1 || errno != expected) {
		fprintf(stderr,
			"uapi_invalid_zsh: %s: ret=%d errno=%d expected=%d\n",
			what, ret, errno, expected);
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	struct gpioctl_zsh_line_caps caps = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(caps),
	};
	struct gpioctl_zsh_lease lease = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(lease),
	};
	struct gpioctl_zsh_line_config config = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(config),
		.offset = 1,
		.direction = GPIOCTL_ZSH_DIRECTION_INPUT,
		.bias = GPIOCTL_ZSH_BIAS_AS_IS,
	};
	struct gpioctl_zsh_values values = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(values),
	};
	struct gpioctl_zsh_batch batch = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(batch),
	};
	struct gpioctl_zsh_event_config event = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(event),
		.offset = 1,
	};
	struct gpioctl_zsh_iopad_config iopad = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(iopad),
		.offset = 1,
	};
	struct gpioctl_zsh_event record;
	int fd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /dev/gpioN_zsh\n", argv[0]);
		return EXIT_FAILURE;
	}
	fd = open(argv[1], O_RDWR | O_CLOEXEC | O_NONBLOCK);
	if (fd < 0)
		fail_zsh("open");

	expect_errno_zsh(fd, _IO(GPIOCTL_ZSH_IOC_MAGIC, 0x7f), NULL,
		ENOTTY, "unknown ioctl");
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_GET_ABI, (void *)1,
		EFAULT, "bad output pointer");
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_LINE_CONFIG, (void *)1,
		EFAULT, "bad input pointer");

	caps.abi_version++;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_GET_LINE_CAPS, &caps,
		EPROTO, "line caps ABI version");
	caps.abi_version = GPIOCTL_ZSH_ABI_VERSION;
	caps.struct_size--;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_GET_LINE_CAPS, &caps,
		EINVAL, "line caps size");
	caps.struct_size = sizeof(caps);
	caps.reserved[0] = 1;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_GET_LINE_CAPS, &caps,
		EINVAL, "line caps reserved");
	caps.reserved[0] = 0;
	caps.offset = UINT32_MAX;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_GET_LINE_CAPS, &caps,
		EINVAL, "line caps offset");

	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_LEASE_REQUEST, &lease,
		EINVAL, "zero lease count");
	lease.count = GPIOCTL_ZSH_MAX_LINES + 1U;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_LEASE_REQUEST, &lease,
		EINVAL, "oversized lease count");
	lease.count = 1;
	lease.flags = UINT32_MAX;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_LEASE_REQUEST, &lease,
		EINVAL, "lease flags");
	lease.flags = 0;
	lease.count = 2;
	lease.offsets[0] = 1;
	lease.offsets[1] = 1;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_LEASE_REQUEST, &lease,
		EINVAL, "duplicate lease offsets");
	lease.count = 1;
	lease.offsets[0] = 15;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_LEASE_REQUEST, &lease,
		EPERM, "reserved lease offset");

	config.flags = UINT32_MAX;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_LINE_CONFIG, &config,
		EINVAL, "line config flags");
	config.flags = 0;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_LINE_CONFIG, &config,
		EPERM, "line config without lease");

	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_VALUES_GET, &values,
		EINVAL, "zero value count");
	values.count = 2;
	values.offsets[0] = 1;
	values.offsets[1] = 1;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_VALUES_SET, &values,
		EINVAL, "duplicate value offsets");

	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_BATCH_EXEC, &batch,
		EINVAL, "zero batch count");
	batch.count = 1;
	batch.ops[0].opcode = UINT32_MAX;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_BATCH_EXEC, &batch,
		EINVAL, "unknown batch opcode");

	event.edge = GPIOCTL_ZSH_EDGE_BOTH + 1U;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_EVENT_CONFIG, &event,
		EINVAL, "invalid event edge");

	iopad.flags = UINT32_MAX;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_IOPAD_CONFIG, &iopad,
		EINVAL, "invalid IOPAD flags");
	memset(&iopad, 0, sizeof(iopad));
	iopad.abi_version = GPIOCTL_ZSH_ABI_VERSION;
	iopad.struct_size = sizeof(iopad);
	iopad.offset = 1;
	iopad.bias = GPIOCTL_ZSH_BIAS_PULL_UP;
	expect_errno_zsh(fd, GPIOCTL_ZSH_IOC_IOPAD_GET_CONFIG, &iopad,
		EINVAL, "nonzero IOPAD query field");

	errno = 0;
	if (read(fd, &record, sizeof(record) - 1U) != -1 || errno != EINVAL) {
		fprintf(stderr, "uapi_invalid_zsh: short event read mismatch\n");
		return EXIT_FAILURE;
	}
	errno = 0;
	if (read(fd, &record, sizeof(record)) != -1 || errno != EAGAIN) {
		fprintf(stderr, "uapi_invalid_zsh: empty nonblocking read mismatch\n");
		return EXIT_FAILURE;
	}

	if (close(fd))
		fail_zsh("close");
	puts("uapi_invalid_zsh: PASS");
	return EXIT_SUCCESS;
}

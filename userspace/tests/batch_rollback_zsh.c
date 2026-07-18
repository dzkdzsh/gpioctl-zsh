// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "uapi/gpioctl_zsh.h"

#define FAIL_AFTER_PATH_ZSH \
	"/sys/module/gpioctl_mock_zsh/parameters/fail_after_operations"

static void fail_zsh(const char *what)
{
	perror(what);
	exit(EXIT_FAILURE);
}

static void set_fail_after_zsh(const char *value)
{
	int fd = open(FAIL_AFTER_PATH_ZSH, O_WRONLY | O_CLOEXEC);
	ssize_t length = (ssize_t)strlen(value);

	if (fd < 0)
		fail_zsh("open fail_after_operations");
	if (write(fd, value, (size_t)length) != length) {
		int saved_errno = errno;

		close(fd);
		errno = saved_errno;
		fail_zsh("write fail_after_operations");
	}
	if (close(fd))
		fail_zsh("close fail_after_operations");
}

static void configure_output_zsh(int fd, uint32_t offset)
{
	struct gpioctl_zsh_line_config config = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(config),
		.offset = offset,
		.direction = GPIOCTL_ZSH_DIRECTION_OUTPUT,
		.value = 0,
		.bias = GPIOCTL_ZSH_BIAS_AS_IS,
	};

	if (ioctl(fd, GPIOCTL_ZSH_IOC_LINE_CONFIG, &config))
		fail_zsh("configure output");
}

int main(int argc, char **argv)
{
	struct gpioctl_zsh_lease lease = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(lease),
		.count = 2,
		.offsets = { 1, 2 },
	};
	struct gpioctl_zsh_batch batch = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(batch),
		.count = 2,
		.ops = {
			{
				.opcode = GPIOCTL_ZSH_BATCH_SET,
				.offset = 1,
				.arg0 = 1,
			},
			{
				.opcode = GPIOCTL_ZSH_BATCH_SET,
				.offset = 2,
				.arg0 = 1,
			},
		},
		.failed_index = -1,
	};
	int fd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s /dev/gpioN_zsh\n", argv[0]);
		return EXIT_FAILURE;
	}
	fd = open(argv[1], O_RDWR | O_CLOEXEC);
	if (fd < 0)
		fail_zsh("open controller");
	if (ioctl(fd, GPIOCTL_ZSH_IOC_LEASE_REQUEST, &lease))
		fail_zsh("lease lines");
	configure_output_zsh(fd, 1);
	configure_output_zsh(fd, 2);

	/* SET(1) and its readback succeed; SET(2) and rollback then fail. */
	set_fail_after_zsh("2\n");
	errno = 0;
	if (ioctl(fd, GPIOCTL_ZSH_IOC_BATCH_EXEC, &batch) != -1 || errno != EIO) {
		fprintf(stderr, "batch result errno=%d, expected EIO\n", errno);
		set_fail_after_zsh("-1\n");
		return EXIT_FAILURE;
	}
	set_fail_after_zsh("-1\n");
	if (batch.failed_index != 1 || batch.rollback_error != -EIO) {
		fprintf(stderr,
			"batch metadata failed_index=%d rollback_error=%d\n",
			batch.failed_index, batch.rollback_error);
		return EXIT_FAILURE;
	}
	if (close(fd))
		fail_zsh("close controller");
	puts("batch_rollback_zsh: PASS failed_index=1 rollback_error=-EIO");
	return EXIT_SUCCESS;
}

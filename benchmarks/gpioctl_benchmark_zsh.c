// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gpioctl_zsh.h"

#define BENCH_MAX_LINES_ZSH GPIOCTL_ZSH_MAX_BATCH_OPS

static void fail_zsh(const char *what)
{
	perror(what);
	exit(EXIT_FAILURE);
}

static uint32_t parse_u32_zsh(const char *text, const char *what)
{
	char *end = NULL;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 0);
	if (errno || !end || *end || value > UINT32_MAX) {
		fprintf(stderr, "invalid %s: %s\n", what, text);
		exit(EXIT_FAILURE);
	}
	return (uint32_t)value;
}

static uint64_t now_ns_zsh(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &now))
		fail_zsh("clock_gettime");
	return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
		(uint64_t)now.tv_nsec;
}

static void emit_zsh(const char *metric, uint32_t line_count,
		     uint32_t iteration, uint64_t elapsed_ns)
{
	const char *workers = getenv("GPIOCTL_ZSH_BENCH_WORKERS");
	const char *worker = getenv("GPIOCTL_ZSH_BENCH_WORKER");

	printf("gpioctl_zsh,%s,%" PRIu32 ",%s,%s,%" PRIu32 ",%" PRIu64 "\n",
	       metric, line_count, workers ?: "1", worker ?: "0", iteration,
	       elapsed_ns);
}

static void configure_zsh(struct gpioctl_zsh_handle *handle,
			  const uint32_t *offsets, uint32_t count,
			  uint32_t direction)
{
	uint32_t i;

	for (i = 0; i < count; i++)
		if (gpioctl_zsh_config(handle, offsets[i], direction, 0,
				       GPIOCTL_ZSH_BIAS_AS_IS, 0, 0))
			fail_zsh("gpioctl_zsh_config");
}

static void run_set_or_get_zsh(const char *metric, const char *device,
			       const uint32_t *offsets, uint32_t iterations)
{
	struct gpioctl_zsh_handle *handle = gpioctl_zsh_open(device);
	uint32_t value = 0;
	uint32_t i;

	if (!handle)
		fail_zsh("gpioctl_zsh_open");
	if (gpioctl_zsh_lease(handle, offsets, 1, 0))
		fail_zsh("gpioctl_zsh_lease");
	configure_zsh(handle, offsets, 1,
		      !strcmp(metric, "set") ? GPIOCTL_ZSH_DIRECTION_OUTPUT :
		      GPIOCTL_ZSH_DIRECTION_INPUT);
	for (i = 0; i < iterations; i++) {
		uint64_t start = now_ns_zsh();

		if (!strcmp(metric, "set")) {
			value = i & 1U;
			if (gpioctl_zsh_set_values(handle, offsets, 1, value))
				fail_zsh("gpioctl_zsh_set_values");
		} else if (gpioctl_zsh_get_values(handle, offsets, 1, &value)) {
			fail_zsh("gpioctl_zsh_get_values");
		}
		emit_zsh(metric, 1, i, now_ns_zsh() - start);
	}
	gpioctl_zsh_close(handle);
}

static void run_lease_zsh(const char *device, const uint32_t *offsets,
			  uint32_t iterations)
{
	struct gpioctl_zsh_handle *handle = gpioctl_zsh_open(device);
	uint32_t i;

	if (!handle)
		fail_zsh("gpioctl_zsh_open");
	for (i = 0; i < iterations; i++) {
		uint64_t start = now_ns_zsh();

		if (gpioctl_zsh_lease(handle, offsets, 1, 0) ||
		    gpioctl_zsh_config(handle, offsets[0],
				       GPIOCTL_ZSH_DIRECTION_INPUT, 0,
				       GPIOCTL_ZSH_BIAS_AS_IS, 0, 0) ||
		    gpioctl_zsh_release(handle, offsets, 1))
			fail_zsh("gpioctl_zsh lease/config/release");
		emit_zsh("lease-release", 1, i, now_ns_zsh() - start);
	}
	gpioctl_zsh_close(handle);
}

static void run_batch_zsh(const char *device, const uint32_t *offsets,
			  uint32_t count, uint32_t iterations)
{
	struct gpioctl_zsh_handle *handle = gpioctl_zsh_open(device);
	struct gpioctl_zsh_batch batch = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(batch),
		.count = count,
		.failed_index = -1,
	};
	uint32_t i, j;

	if (!handle)
		fail_zsh("gpioctl_zsh_open");
	if (gpioctl_zsh_lease(handle, offsets, count, 0))
		fail_zsh("gpioctl_zsh_lease");
	configure_zsh(handle, offsets, count, GPIOCTL_ZSH_DIRECTION_OUTPUT);
	for (j = 0; j < count; j++) {
		batch.ops[j].opcode = GPIOCTL_ZSH_BATCH_SET;
		batch.ops[j].offset = offsets[j];
	}
	for (i = 0; i < iterations; i++) {
		uint64_t start;

		for (j = 0; j < count; j++)
			batch.ops[j].arg0 = (i + j) & 1U;
		start = now_ns_zsh();
		if (gpioctl_zsh_batch(handle, &batch))
			fail_zsh("gpioctl_zsh_batch");
		emit_zsh("batch-set", count, i, now_ns_zsh() - start);
	}
	gpioctl_zsh_close(handle);
}

int main(int argc, char **argv)
{
	uint32_t offsets[BENCH_MAX_LINES_ZSH];
	uint32_t iterations, count, i;
	const char *metric;

	if (argc < 5) {
		fprintf(stderr,
			"usage: %s set|get|lease|batch DEVICE ITERATIONS OFFSET...\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	metric = argv[1];
	iterations = parse_u32_zsh(argv[3], "iteration count");
	count = (uint32_t)(argc - 4);
	if (!iterations || count > BENCH_MAX_LINES_ZSH ||
	    (strcmp(metric, "batch") && count != 1U) ||
	    (strcmp(metric, "set") && strcmp(metric, "get") &&
	     strcmp(metric, "lease") && strcmp(metric, "batch"))) {
		errno = EINVAL;
		fail_zsh("benchmark arguments");
	}
	for (i = 0; i < count; i++)
		offsets[i] = parse_u32_zsh(argv[i + 4], "offset");

	if (!strcmp(metric, "set") || !strcmp(metric, "get"))
		run_set_or_get_zsh(metric, argv[2], offsets, iterations);
	else if (!strcmp(metric, "lease"))
		run_lease_zsh(argv[2], offsets, iterations);
	else
		run_batch_zsh(argv[2], offsets, count, iterations);
	return EXIT_SUCCESS;
}

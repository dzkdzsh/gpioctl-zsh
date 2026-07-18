// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <gpiod.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BENCH_MAX_LINES_ZSH 32U
#define CONSUMER_ZSH "gpioctl-black-box-benchmark-zsh"

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

	printf("libgpiod,%s,%" PRIu32 ",%s,%s,%" PRIu32 ",%" PRIu64 "\n",
	       metric, line_count, workers ?: "1", worker ?: "0", iteration,
	       elapsed_ns);
}

static void release_as_input_zsh(struct gpiod_line_bulk *bulk)
{
	gpiod_line_release_bulk(bulk);
	if (gpiod_line_request_bulk_input(bulk, CONSUMER_ZSH))
		fail_zsh("gpiod cleanup request input");
	gpiod_line_release_bulk(bulk);
}

static void run_set_or_get_zsh(const char *metric, struct gpiod_line *line,
			       uint32_t iterations)
{
	uint32_t i;

	if (!strcmp(metric, "set")) {
		if (gpiod_line_request_output(line, CONSUMER_ZSH, 0))
			fail_zsh("gpiod_line_request_output");
	} else if (gpiod_line_request_input(line, CONSUMER_ZSH)) {
		fail_zsh("gpiod_line_request_input");
	}
	for (i = 0; i < iterations; i++) {
		uint64_t start = now_ns_zsh();
		int ret;

		if (!strcmp(metric, "set"))
			ret = gpiod_line_set_value(line, (int)(i & 1U));
		else
			ret = gpiod_line_get_value(line);
		if (ret < 0)
			fail_zsh("libgpiod line operation");
		emit_zsh(metric, 1, i, now_ns_zsh() - start);
	}
	gpiod_line_release(line);
	if (gpiod_line_request_input(line, CONSUMER_ZSH))
		fail_zsh("gpiod cleanup request input");
	gpiod_line_release(line);
}

static void run_lease_zsh(struct gpiod_line *line, uint32_t iterations)
{
	uint32_t i;

	for (i = 0; i < iterations; i++) {
		uint64_t start = now_ns_zsh();

		if (gpiod_line_request_input(line, CONSUMER_ZSH))
			fail_zsh("gpiod lease request");
		gpiod_line_release(line);
		emit_zsh("lease-release", 1, i, now_ns_zsh() - start);
	}
	if (gpiod_line_request_input(line, CONSUMER_ZSH))
		fail_zsh("gpiod cleanup request input");
	gpiod_line_release(line);
}

static void run_batch_zsh(struct gpiod_line_bulk *bulk, uint32_t count,
			  uint32_t iterations)
{
	int values[BENCH_MAX_LINES_ZSH] = { 0 };
	uint32_t i, j;

	if (gpiod_line_request_bulk_output(bulk, CONSUMER_ZSH, values))
		fail_zsh("gpiod_line_request_bulk_output");
	for (i = 0; i < iterations; i++) {
		uint64_t start;

		for (j = 0; j < count; j++)
			values[j] = (int)((i + j) & 1U);
		start = now_ns_zsh();
		if (gpiod_line_set_value_bulk(bulk, values))
			fail_zsh("gpiod_line_set_value_bulk");
		emit_zsh("batch-set", count, i, now_ns_zsh() - start);
	}
	release_as_input_zsh(bulk);
}

int main(int argc, char **argv)
{
	struct gpiod_line_bulk bulk;
	struct gpiod_chip *chip;
	struct gpiod_line *lines[BENCH_MAX_LINES_ZSH];
	uint32_t iterations, count, i;
	const char *metric;

	if (argc < 5) {
		fprintf(stderr,
			"usage: %s set|get|lease|batch CHIP ITERATIONS OFFSET...\n",
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
	chip = gpiod_chip_open(argv[2]);
	if (!chip)
		fail_zsh("gpiod_chip_open");
	gpiod_line_bulk_init(&bulk);
	for (i = 0; i < count; i++) {
		uint32_t offset = parse_u32_zsh(argv[i + 4], "offset");

		lines[i] = gpiod_chip_get_line(chip, offset);
		if (!lines[i])
			fail_zsh("gpiod_chip_get_line");
		gpiod_line_bulk_add(&bulk, lines[i]);
	}

	if (!strcmp(metric, "set") || !strcmp(metric, "get"))
		run_set_or_get_zsh(metric, lines[0], iterations);
	else if (!strcmp(metric, "lease"))
		run_lease_zsh(lines[0], iterations);
	else
		run_batch_zsh(&bulk, count, iterations);
	gpiod_chip_close(chip);
	return EXIT_SUCCESS;
}

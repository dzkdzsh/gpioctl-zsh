// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gpioctl_zsh.h"

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

static uint64_t clock_ns_zsh(clockid_t clock_id)
{
	struct timespec now;

	if (clock_gettime(clock_id, &now))
		fail_zsh("clock_gettime");
	return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
		(uint64_t)now.tv_nsec;
}

static void emit_zsh(const char *metric, uint32_t iteration, uint64_t value)
{
	printf("gpioctl_zsh,%s,1,1,0,%" PRIu32 ",%" PRIu64 "\n",
	       metric, iteration, value);
}

int main(int argc, char **argv)
{
	struct gpioctl_zsh_handle *handle;
	struct gpioctl_zsh_event event;
	struct gpioctl_zsh_stats before, after;
	struct pollfd waiter;
	uint32_t offset, iterations, i;
	char inject_text[16];
	int inject_fd, inject_length;

	if (argc != 5) {
		fprintf(stderr,
			"usage: %s DEVICE OFFSET ITERATIONS INJECT_PARAMETER\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	offset = parse_u32_zsh(argv[2], "offset");
	iterations = parse_u32_zsh(argv[3], "iteration count");
	if (!iterations) {
		errno = EINVAL;
		fail_zsh("iteration count");
	}
	inject_length = snprintf(inject_text, sizeof(inject_text), "%" PRIu32 "\n",
				 offset);
	if (inject_length <= 0 || (size_t)inject_length >= sizeof(inject_text)) {
		errno = EOVERFLOW;
		fail_zsh("format offset");
	}
	handle = gpioctl_zsh_open(argv[1]);
	if (!handle || gpioctl_zsh_lease(handle, &offset, 1, 0) ||
	    gpioctl_zsh_config(handle, offset, GPIOCTL_ZSH_DIRECTION_INPUT, 0,
			       GPIOCTL_ZSH_BIAS_AS_IS, 0, 0) ||
	    gpioctl_zsh_event_config(handle, offset, GPIOCTL_ZSH_EDGE_BOTH, 0) ||
	    gpioctl_zsh_get_stats(handle, &before))
		fail_zsh("event setup");
	inject_fd = open(argv[4], O_WRONLY | O_CLOEXEC);
	if (inject_fd < 0)
		fail_zsh("open inject parameter");
	waiter.fd = gpioctl_zsh_fd(handle);
	waiter.events = POLLIN;

	for (i = 0; i < iterations; i++) {
		uint64_t start_raw = clock_ns_zsh(CLOCK_MONOTONIC_RAW);
		uint64_t end_raw, end_monotonic;
		ssize_t bytes;

		if (lseek(inject_fd, 0, SEEK_SET) < 0 ||
		    write(inject_fd, inject_text, (size_t)inject_length) !=
			    inject_length)
			fail_zsh("inject event");
		if (poll(&waiter, 1, 1000) != 1 || !(waiter.revents & POLLIN)) {
			errno = ETIMEDOUT;
			fail_zsh("poll event");
		}
		bytes = gpioctl_zsh_read_events(handle, &event, 1);
		end_raw = clock_ns_zsh(CLOCK_MONOTONIC_RAW);
		end_monotonic = clock_ns_zsh(CLOCK_MONOTONIC);
		if (bytes != (ssize_t)sizeof(event) || event.offset != offset ||
		    !event.timestamp_ns || end_monotonic < event.timestamp_ns) {
			errno = EPROTO;
			fail_zsh("event record");
		}
		emit_zsh("event-roundtrip", i, end_raw - start_raw);
		emit_zsh("event-delivery", i,
			 end_monotonic - event.timestamp_ns);
	}
	if (gpioctl_zsh_get_stats(handle, &after))
		fail_zsh("event stats");
	if (after.event_drops != before.event_drops ||
	    after.events - before.events != iterations) {
		fprintf(stderr,
			"event counters events=%" PRIu64 " drops=%" PRIu64 "\n",
			(uint64_t)(after.events - before.events),
			(uint64_t)(after.event_drops - before.event_drops));
		return EXIT_FAILURE;
	}
	if (close(inject_fd))
		fail_zsh("close inject parameter");
	gpioctl_zsh_close(handle);
	return EXIT_SUCCESS;
}

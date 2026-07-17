// SPDX-License-Identifier: GPL-2.0-only
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

#include "gpioctl_zsh.h"

static int parse_u32_zsh(const char *text, uint32_t *value)
{
	char *end = NULL;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno || !end || *end || parsed > UINT32_MAX)
		return -1;
	*value = (uint32_t)parsed;
	return 0;
}

static void sleep_ms_zsh(uint32_t milliseconds)
{
	struct timespec delay = {
		.tv_sec = (time_t)(milliseconds / 1000U),
		.tv_nsec = (long)(milliseconds % 1000U) * 1000000L,
	};

	while (nanosleep(&delay, &delay) && errno == EINTR)
		;
}

int main(int argc, char **argv)
{
	struct gpioctl_zsh_event events[GPIOCTL_ZSH_EVENT_QUEUE_SIZE];
	struct gpioctl_zsh_handle *handle = NULL;
	struct gpioctl_zsh_stats stats;
	struct epoll_event registration = { .events = EPOLLIN };
	struct epoll_event ready;
	uint32_t offset, delay_ms, minimum_events, require_overflow;
	uint64_t previous_sequence = 0;
	ssize_t bytes;
	size_t count, i, overflow_count = 0;
	int epoll_fd = -1;
	int ret = EXIT_FAILURE;

	if (argc != 6 || parse_u32_zsh(argv[2], &offset) ||
	    parse_u32_zsh(argv[3], &delay_ms) ||
	    parse_u32_zsh(argv[4], &minimum_events) ||
	    parse_u32_zsh(argv[5], &require_overflow) ||
	    !minimum_events || minimum_events > GPIOCTL_ZSH_EVENT_QUEUE_SIZE ||
	    require_overflow > 1U) {
		fprintf(stderr,
			"usage: %s DEVICE OFFSET PRE_READ_MS MIN_EVENTS "
			"REQUIRE_OVERFLOW\n", argv[0]);
		return EXIT_FAILURE;
	}
	handle = gpioctl_zsh_open(argv[1]);
	if (!handle || gpioctl_zsh_lease(handle, &offset, 1, 0) ||
	    gpioctl_zsh_config(handle, offset, GPIOCTL_ZSH_DIRECTION_INPUT, 0,
			       GPIOCTL_ZSH_BIAS_AS_IS, 0, 0) ||
	    gpioctl_zsh_event_config(handle, offset, GPIOCTL_ZSH_EDGE_BOTH, 0)) {
		perror("event setup");
		goto out;
	}
	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd < 0) {
		perror("epoll_create1");
		goto out;
	}
	registration.data.fd = gpioctl_zsh_fd(handle);
	if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, registration.data.fd,
		      &registration)) {
		perror("epoll_ctl");
		goto out;
	}
	puts("READY");
	fflush(stdout);
	sleep_ms_zsh(delay_ms);
	if (epoll_wait(epoll_fd, &ready, 1, 1000) != 1 ||
	    !(ready.events & EPOLLIN)) {
		fprintf(stderr, "epoll did not report readable events\n");
		goto out;
	}
	bytes = gpioctl_zsh_read_events(handle, events,
					GPIOCTL_ZSH_EVENT_QUEUE_SIZE);
	if (bytes < 0 || (size_t)bytes % sizeof(events[0])) {
		perror("read events");
		goto out;
	}
	count = (size_t)bytes / sizeof(events[0]);
	if (count < minimum_events) {
		fprintf(stderr, "only %zu events, expected at least %" PRIu32 "\n",
			count, minimum_events);
		goto out;
	}
	for (i = 0; i < count; i++) {
		if (events[i].abi_version != GPIOCTL_ZSH_ABI_VERSION ||
		    events[i].struct_size != sizeof(events[i]) ||
		    events[i].offset != offset || !events[i].timestamp_ns ||
		    (previous_sequence &&
		     events[i].sequence != previous_sequence + 1U) ||
		    (events[i].edge != GPIOCTL_ZSH_EDGE_RISING &&
		     events[i].edge != GPIOCTL_ZSH_EDGE_FALLING)) {
			fprintf(stderr, "invalid event record at index %zu\n", i);
			goto out;
		}
		previous_sequence = events[i].sequence;
		if (events[i].flags & GPIOCTL_ZSH_EVENT_OVERFLOW)
			overflow_count++;
	}
	if (gpioctl_zsh_get_stats(handle, &stats)) {
		perror("get stats");
		goto out;
	}
	if (require_overflow && (!overflow_count || !stats.event_drops)) {
		fprintf(stderr, "overflow was not reported\n");
		goto out;
	}
	printf("event_epoll_zsh: PASS events=%zu overflow=%zu drops=%" PRIu64
	       " first_sequence=%" PRIu64 " last_sequence=%" PRIu64 "\n",
	       count, overflow_count, (uint64_t)stats.event_drops,
	       (uint64_t)events[0].sequence,
	       (uint64_t)events[count - 1U].sequence);
	ret = EXIT_SUCCESS;
out:
	if (epoll_fd >= 0)
		(void)close(epoll_fd);
	gpioctl_zsh_close(handle);
	return ret;
}

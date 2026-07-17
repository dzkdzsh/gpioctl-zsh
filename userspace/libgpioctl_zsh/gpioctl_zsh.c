// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "gpioctl_zsh.h"

struct gpioctl_zsh_handle {
	int fd;
};

static int gpioctl_zsh_copy_offsets(uint32_t *destination,
				    const uint32_t *source, size_t count)
{
	if (!source || !count || count > GPIOCTL_ZSH_MAX_LINES) {
		errno = EINVAL;
		return -1;
	}
	memcpy(destination, source, count * sizeof(*source));
	return 0;
}

struct gpioctl_zsh_handle *gpioctl_zsh_open(const char *device_path)
{
	struct gpioctl_zsh_handle *handle;
	int fd;

	if (!device_path) {
		errno = EINVAL;
		return NULL;
	}
	fd = open(device_path, O_RDWR | O_CLOEXEC);
	if (fd < 0)
		return NULL;
	handle = calloc(1, sizeof(*handle));
	if (!handle) {
		int saved_errno = errno;

		close(fd);
		errno = saved_errno;
		return NULL;
	}
	handle->fd = fd;
	return handle;
}

void gpioctl_zsh_close(struct gpioctl_zsh_handle *handle)
{
	if (!handle)
		return;
	if (handle->fd >= 0)
		close(handle->fd);
	handle->fd = -1;
	free(handle);
}

int gpioctl_zsh_fd(const struct gpioctl_zsh_handle *handle)
{
	if (!handle) {
		errno = EINVAL;
		return -1;
	}
	return handle->fd;
}

int gpioctl_zsh_get_abi(struct gpioctl_zsh_handle *handle,
			struct gpioctl_zsh_abi_info *info)
{
	if (!handle || !info) {
		errno = EINVAL;
		return -1;
	}
	memset(info, 0, sizeof(*info));
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_GET_ABI, info);
}

int gpioctl_zsh_get_caps(struct gpioctl_zsh_handle *handle,
			 struct gpioctl_zsh_caps *caps)
{
	if (!handle || !caps) {
		errno = EINVAL;
		return -1;
	}
	memset(caps, 0, sizeof(*caps));
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_GET_CAPS, caps);
}

int gpioctl_zsh_get_line_caps(struct gpioctl_zsh_handle *handle,
			      uint32_t offset,
			      struct gpioctl_zsh_line_caps *caps)
{
	if (!handle || !caps) {
		errno = EINVAL;
		return -1;
	}
	memset(caps, 0, sizeof(*caps));
	caps->abi_version = GPIOCTL_ZSH_ABI_VERSION;
	caps->struct_size = (uint32_t)sizeof(*caps);
	caps->offset = offset;
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_GET_LINE_CAPS, caps);
}

int gpioctl_zsh_lease(struct gpioctl_zsh_handle *handle,
		      const uint32_t *offsets, size_t count, uint32_t flags)
{
	struct gpioctl_zsh_lease request = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(request),
		.flags = flags,
		.count = (uint32_t)count,
	};

	if (!handle || gpioctl_zsh_copy_offsets(request.offsets, offsets, count)) {
		if (!handle)
			errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_LEASE_REQUEST, &request);
}

int gpioctl_zsh_release(struct gpioctl_zsh_handle *handle,
			const uint32_t *offsets, size_t count)
{
	struct gpioctl_zsh_lease request = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(request),
		.count = (uint32_t)count,
	};

	if (!handle || gpioctl_zsh_copy_offsets(request.offsets, offsets, count)) {
		if (!handle)
			errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_LEASE_RELEASE, &request);
}

int gpioctl_zsh_config(struct gpioctl_zsh_handle *handle, uint32_t offset,
		       uint32_t direction, uint32_t value, uint32_t bias,
		       uint32_t flags, uint32_t debounce_us)
{
	struct gpioctl_zsh_line_config config = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(config),
		.offset = offset,
		.direction = direction,
		.value = value,
		.bias = bias,
		.flags = flags,
		.debounce_us = debounce_us,
	};

	if (!handle) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_LINE_CONFIG, &config);
}

int gpioctl_zsh_get_values(struct gpioctl_zsh_handle *handle,
			   const uint32_t *offsets, size_t count,
			   uint32_t *values)
{
	struct gpioctl_zsh_values request = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(request),
		.count = (uint32_t)count,
	};

	if (!handle || !values ||
	    gpioctl_zsh_copy_offsets(request.offsets, offsets, count)) {
		if (!handle || !values)
			errno = EINVAL;
		return -1;
	}
	if (ioctl(handle->fd, GPIOCTL_ZSH_IOC_VALUES_GET, &request))
		return -1;
	*values = request.values;
	return 0;
}

int gpioctl_zsh_set_values(struct gpioctl_zsh_handle *handle,
			   const uint32_t *offsets, size_t count,
			   uint32_t values)
{
	struct gpioctl_zsh_values request = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(request),
		.count = (uint32_t)count,
		.values = values,
	};

	if (!handle || gpioctl_zsh_copy_offsets(request.offsets, offsets, count)) {
		if (!handle)
			errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_VALUES_SET, &request);
}

int gpioctl_zsh_batch(struct gpioctl_zsh_handle *handle,
		      struct gpioctl_zsh_batch *batch)
{
	if (!handle || !batch) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_BATCH_EXEC, batch);
}

int gpioctl_zsh_event_config(struct gpioctl_zsh_handle *handle,
			     uint32_t offset, uint32_t edge,
			     uint32_t debounce_us)
{
	struct gpioctl_zsh_event_config config = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(config),
		.offset = offset,
		.edge = edge,
		.debounce_us = debounce_us,
	};

	if (!handle) {
		errno = EINVAL;
		return -1;
	}
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_EVENT_CONFIG, &config);
}

ssize_t gpioctl_zsh_read_events(struct gpioctl_zsh_handle *handle,
				struct gpioctl_zsh_event *events,
				size_t event_count)
{
	size_t bytes;

	if (!handle || !events || !event_count ||
	    event_count > SIZE_MAX / sizeof(*events)) {
		errno = EINVAL;
		return -1;
	}
	bytes = event_count * sizeof(*events);
	return read(handle->fd, events, bytes);
}

int gpioctl_zsh_get_stats(struct gpioctl_zsh_handle *handle,
			  struct gpioctl_zsh_stats *stats)
{
	if (!handle || !stats) {
		errno = EINVAL;
		return -1;
	}
	memset(stats, 0, sizeof(*stats));
	return ioctl(handle->fd, GPIOCTL_ZSH_IOC_GET_STATS, stats);
}

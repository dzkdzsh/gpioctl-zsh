/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef LIBGPIOCTL_ZSH_H
#define LIBGPIOCTL_ZSH_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#include "uapi/gpioctl_zsh.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gpioctl_zsh_handle;

struct gpioctl_zsh_handle *gpioctl_zsh_open(const char *device_path);
void gpioctl_zsh_close(struct gpioctl_zsh_handle *handle);
int gpioctl_zsh_fd(const struct gpioctl_zsh_handle *handle);

int gpioctl_zsh_get_abi(struct gpioctl_zsh_handle *handle,
			struct gpioctl_zsh_abi_info *info);
int gpioctl_zsh_get_caps(struct gpioctl_zsh_handle *handle,
			 struct gpioctl_zsh_caps *caps);
int gpioctl_zsh_get_line_caps(struct gpioctl_zsh_handle *handle,
			      uint32_t offset,
			      struct gpioctl_zsh_line_caps *caps);
int gpioctl_zsh_get_line_policy(struct gpioctl_zsh_handle *handle,
				uint32_t offset,
				struct gpioctl_zsh_line_policy *policy);
int gpioctl_zsh_lease(struct gpioctl_zsh_handle *handle,
		      const uint32_t *offsets, size_t count, uint32_t flags);
int gpioctl_zsh_release(struct gpioctl_zsh_handle *handle,
			const uint32_t *offsets, size_t count);
int gpioctl_zsh_config(struct gpioctl_zsh_handle *handle, uint32_t offset,
		       uint32_t direction, uint32_t value, uint32_t bias,
		       uint32_t flags, uint32_t debounce_us);
int gpioctl_zsh_get_values(struct gpioctl_zsh_handle *handle,
			   const uint32_t *offsets, size_t count,
			   uint32_t *values);
int gpioctl_zsh_set_values(struct gpioctl_zsh_handle *handle,
			   const uint32_t *offsets, size_t count,
			   uint32_t values);
int gpioctl_zsh_batch(struct gpioctl_zsh_handle *handle,
		      struct gpioctl_zsh_batch *batch);
int gpioctl_zsh_event_config(struct gpioctl_zsh_handle *handle,
			     uint32_t offset, uint32_t edge,
			     uint32_t debounce_us);
int gpioctl_zsh_iopad_config(struct gpioctl_zsh_handle *handle,
			     uint32_t offset, uint32_t bias,
			     uint32_t drive_level, uint32_t mux_state,
			     uint32_t flags);
ssize_t gpioctl_zsh_read_events(struct gpioctl_zsh_handle *handle,
				struct gpioctl_zsh_event *events,
				size_t event_count);
int gpioctl_zsh_get_stats(struct gpioctl_zsh_handle *handle,
			  struct gpioctl_zsh_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* LIBGPIOCTL_ZSH_H */

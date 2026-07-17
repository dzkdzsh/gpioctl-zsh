/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GPIOCTL_ZSH_UAPI_H
#define GPIOCTL_ZSH_UAPI_H

#include <linux/ioctl.h>
#include <linux/types.h>

#define GPIOCTL_ZSH_ABI_VERSION       1U
#define GPIOCTL_ZSH_IOC_MAGIC         0xB7
#define GPIOCTL_ZSH_MAX_LINES         32U
#define GPIOCTL_ZSH_MAX_BATCH_OPS     32U
#define GPIOCTL_ZSH_EVENT_QUEUE_SIZE  256U

#define GPIOCTL_ZSH_CAP_INPUT         (1ULL << 0)
#define GPIOCTL_ZSH_CAP_OUTPUT        (1ULL << 1)
#define GPIOCTL_ZSH_CAP_BIAS_DISABLE  (1ULL << 2)
#define GPIOCTL_ZSH_CAP_BIAS_PULL_UP  (1ULL << 3)
#define GPIOCTL_ZSH_CAP_BIAS_PULL_DOWN (1ULL << 4)
#define GPIOCTL_ZSH_CAP_EDGE_RISING   (1ULL << 5)
#define GPIOCTL_ZSH_CAP_EDGE_FALLING  (1ULL << 6)
#define GPIOCTL_ZSH_CAP_DEBOUNCE      (1ULL << 7)
#define GPIOCTL_ZSH_CAP_BATCH         (1ULL << 8)
#define GPIOCTL_ZSH_CAP_IOPAD         (1ULL << 9)

#define GPIOCTL_ZSH_LINE_ACTIVE_LOW   (1U << 0)

#define GPIOCTL_ZSH_LEASE_INPUT_ONLY  (1U << 0)

#define GPIOCTL_ZSH_EVENT_OVERFLOW    (1U << 0)
#define GPIOCTL_ZSH_EVENT_DEVICE_GONE (1U << 1)

enum gpioctl_zsh_direction {
	GPIOCTL_ZSH_DIRECTION_INPUT = 0,
	GPIOCTL_ZSH_DIRECTION_OUTPUT = 1,
};

enum gpioctl_zsh_bias {
	GPIOCTL_ZSH_BIAS_AS_IS = 0,
	GPIOCTL_ZSH_BIAS_DISABLE = 1,
	GPIOCTL_ZSH_BIAS_PULL_UP = 2,
	GPIOCTL_ZSH_BIAS_PULL_DOWN = 3,
};

enum gpioctl_zsh_edge {
	GPIOCTL_ZSH_EDGE_NONE = 0,
	GPIOCTL_ZSH_EDGE_RISING = 1,
	GPIOCTL_ZSH_EDGE_FALLING = 2,
	GPIOCTL_ZSH_EDGE_BOTH = 3,
};

enum gpioctl_zsh_batch_opcode {
	GPIOCTL_ZSH_BATCH_CONFIG = 1,
	GPIOCTL_ZSH_BATCH_SET = 2,
};

struct gpioctl_zsh_abi_info {
	__u32 abi_version;
	__u32 struct_size;
	__u32 max_lines_per_request;
	__u32 max_batch_ops;
	__u32 event_record_size;
	__u32 reserved[3];
};

struct gpioctl_zsh_caps {
	__u32 abi_version;
	__u32 struct_size;
	__u32 controller_id;
	__u32 line_count;
	__u64 capabilities;
	__u32 event_queue_size;
	__u32 reserved[5];
};

struct gpioctl_zsh_line_caps {
	__u32 abi_version;
	__u32 struct_size;
	__u32 offset;
	__u32 reserved0;
	__u64 capabilities;
	__u32 drive_level_min;
	__u32 drive_level_max;
	__u32 reserved[4];
};

struct gpioctl_zsh_lease {
	__u32 abi_version;
	__u32 struct_size;
	__u32 flags;
	__u32 count;
	__u32 offsets[GPIOCTL_ZSH_MAX_LINES];
	__u32 reserved[4];
};

struct gpioctl_zsh_line_config {
	__u32 abi_version;
	__u32 struct_size;
	__u32 offset;
	__u32 direction;
	__u32 value;
	__u32 bias;
	__u32 flags;
	__u32 debounce_us;
	__u32 reserved[4];
};

struct gpioctl_zsh_values {
	__u32 abi_version;
	__u32 struct_size;
	__u32 count;
	__u32 flags;
	__u32 offsets[GPIOCTL_ZSH_MAX_LINES];
	__u32 values;
	__u32 reserved[3];
};

struct gpioctl_zsh_batch_op {
	__u32 opcode;
	__u32 offset;
	__u32 arg0;
	__u32 arg1;
	__u32 flags;
	__u32 reserved[3];
};

struct gpioctl_zsh_batch {
	__u32 abi_version;
	__u32 struct_size;
	__u32 count;
	__u32 flags;
	struct gpioctl_zsh_batch_op ops[GPIOCTL_ZSH_MAX_BATCH_OPS];
	__s32 failed_index;
	__s32 rollback_error;
	__u32 reserved[4];
};

struct gpioctl_zsh_event_config {
	__u32 abi_version;
	__u32 struct_size;
	__u32 offset;
	__u32 edge;
	__u32 debounce_us;
	__u32 flags;
	__u32 reserved[4];
};

struct gpioctl_zsh_event {
	__u32 abi_version;
	__u32 struct_size;
	__u32 offset;
	__u32 edge;
	__u64 timestamp_ns;
	__u64 sequence;
	__u32 flags;
	__u32 reserved[3];
};

struct gpioctl_zsh_iopad_config {
	__u32 abi_version;
	__u32 struct_size;
	__u32 offset;
	__u32 bias;
	__u32 drive_level;
	__u32 mux_state;
	__u32 flags;
	__u32 reserved[5];
};

struct gpioctl_zsh_stats {
	__u32 abi_version;
	__u32 struct_size;
	__u64 operations;
	__u64 errors;
	__u64 denials;
	__u64 lease_conflicts;
	__u64 events;
	__u64 event_drops;
	__u32 active_leases;
	__u32 reserved[5];
};

#define GPIOCTL_ZSH_IOC_GET_ABI \
	_IOR(GPIOCTL_ZSH_IOC_MAGIC, 0x00, struct gpioctl_zsh_abi_info)
#define GPIOCTL_ZSH_IOC_GET_CAPS \
	_IOR(GPIOCTL_ZSH_IOC_MAGIC, 0x01, struct gpioctl_zsh_caps)
#define GPIOCTL_ZSH_IOC_GET_LINE_CAPS \
	_IOWR(GPIOCTL_ZSH_IOC_MAGIC, 0x02, struct gpioctl_zsh_line_caps)
#define GPIOCTL_ZSH_IOC_LEASE_REQUEST \
	_IOW(GPIOCTL_ZSH_IOC_MAGIC, 0x10, struct gpioctl_zsh_lease)
#define GPIOCTL_ZSH_IOC_LEASE_RELEASE \
	_IOW(GPIOCTL_ZSH_IOC_MAGIC, 0x11, struct gpioctl_zsh_lease)
#define GPIOCTL_ZSH_IOC_LINE_CONFIG \
	_IOW(GPIOCTL_ZSH_IOC_MAGIC, 0x20, struct gpioctl_zsh_line_config)
#define GPIOCTL_ZSH_IOC_VALUES_GET \
	_IOWR(GPIOCTL_ZSH_IOC_MAGIC, 0x21, struct gpioctl_zsh_values)
#define GPIOCTL_ZSH_IOC_VALUES_SET \
	_IOW(GPIOCTL_ZSH_IOC_MAGIC, 0x22, struct gpioctl_zsh_values)
#define GPIOCTL_ZSH_IOC_BATCH_EXEC \
	_IOWR(GPIOCTL_ZSH_IOC_MAGIC, 0x23, struct gpioctl_zsh_batch)
#define GPIOCTL_ZSH_IOC_EVENT_CONFIG \
	_IOW(GPIOCTL_ZSH_IOC_MAGIC, 0x30, struct gpioctl_zsh_event_config)
#define GPIOCTL_ZSH_IOC_IOPAD_CONFIG \
	_IOW(GPIOCTL_ZSH_IOC_MAGIC, 0x40, struct gpioctl_zsh_iopad_config)
#define GPIOCTL_ZSH_IOC_GET_STATS \
	_IOR(GPIOCTL_ZSH_IOC_MAGIC, 0x50, struct gpioctl_zsh_stats)

#endif /* GPIOCTL_ZSH_UAPI_H */


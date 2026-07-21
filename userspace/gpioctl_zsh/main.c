// SPDX-License-Identifier: GPL-2.0-only
/*
 * Command-line frontend shared by one-shot, REPL, and script execution.
 *
 * All three modes dispatch through the same parser and executor.  This keeps
 * validation, deadlines, dry-run behavior, JSON output, and line ownership
 * identical instead of maintaining subtly different command implementations.
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <errno.h>
#include <glob.h>
#include <inttypes.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "gpioctl_zsh.h"

#define GPIOCTL_CLI_MAX_PATH 256
#define GPIOCTL_CLI_MAX_NAME 64
#define GPIOCTL_CLI_MAX_TOKENS 64
#define GPIOCTL_CLI_MAX_HELD 32
#define GPIOCTL_CLI_MAX_DURATION_MS 86400000U
#define GPIOCTL_CLI_MAX_CYCLES 100000U

struct cli_options_zsh {
	bool json;
	bool strict;
	bool dry_run;
	bool timeout_set;
	uint32_t timeout_ms;
	uint64_t deadline_ns;
	const char *config_path;
};

struct target_zsh {
	char alias[GPIOCTL_CLI_MAX_NAME];
	char device[GPIOCTL_CLI_MAX_PATH];
	uint32_t offset;
	uint32_t line_flags;
	char pad[GPIOCTL_CLI_MAX_NAME];
	char physical_pin[GPIOCTL_CLI_MAX_NAME];
	char description[GPIOCTL_CLI_MAX_NAME];
};

struct held_line_zsh {
	bool used;
	struct target_zsh target;
	struct gpioctl_zsh_handle *handle;
};

struct transaction_zsh {
	bool active;
	char device[GPIOCTL_CLI_MAX_PATH];
	struct gpioctl_zsh_batch batch;
};

struct runtime_zsh {
	struct cli_options_zsh options;
	struct held_line_zsh held[GPIOCTL_CLI_MAX_HELD];
	struct transaction_zsh transaction;
};

static bool error_reported_zsh;

static void usage_zsh(FILE *stream)
{
	fputs(
		"Usage: gpioctl_zsh [--json] [--strict] [--dry-run] [--timeout MS] "
		"[--config FILE] COMMAND ...\n"
		"Commands:\n"
		"  list\n"
		"  resolve TARGET\n"
		"  info TARGET\n"
		"  get TARGET\n"
		"  set TARGET VALUE [HOLD_MS]\n"
		"  blink TARGET COUNT ON_MS OFF_MS\n"
		"  pair-blink TARGET_A TARGET_B COUNT INTERVAL_MS\n"
		"  batch-set DEVICE HOLD_MS OFFSET=VALUE ...\n"
		"  transaction DEVICE\n"
		"  tx-line OFFSET in|out VALUE [active-low]\n"
		"  commit [HOLD_MS]\n"
		"  abort\n"
		"  watch TARGET rising|falling|both TIMEOUT_MS [COUNT] [DEBOUNCE_US]\n"
		"  iopad-get TARGET\n"
		"  iopad TARGET [mux=gpio] [bias=none|up|down] [drive=0..15]\n"
		"  stats TARGET|DEVICE\n"
		"  acquire TARGET in|out [INITIAL_VALUE]\n"
		"  value TARGET [VALUE]\n"
		"  release TARGET\n"
		"  sleep MILLISECONDS\n"
		"  run FILE|-\n"
		"  shell\n",
		stream);
}

static int parse_u32_zsh(const char *text, uint32_t *value)
{
	char *end = NULL;
	unsigned long parsed;

	if (!text || !*text || !value)
		return -1;
	errno = 0;
	parsed = strtoul(text, &end, 0);
	if (errno || !end || *end || parsed > UINT32_MAX)
		return -1;
	*value = (uint32_t)parsed;
	return 0;
}

static void json_string_zsh(FILE *stream, const char *text)
{
	const unsigned char *cursor = (const unsigned char *)(text ?: "");

	fputc('"', stream);
	for (; *cursor; cursor++) {
		switch (*cursor) {
		case '"':
			fputs("\\\"", stream);
			break;
		case '\\':
			fputs("\\\\", stream);
			break;
		case '\b':
			fputs("\\b", stream);
			break;
		case '\f':
			fputs("\\f", stream);
			break;
		case '\n':
			fputs("\\n", stream);
			break;
		case '\r':
			fputs("\\r", stream);
			break;
		case '\t':
			fputs("\\t", stream);
			break;
		default:
			if (*cursor < 0x20U)
				fprintf(stream, "\\u%04x", *cursor);
			else
				fputc(*cursor, stream);
			break;
		}
	}
	fputc('"', stream);
}

static int monotonic_ns_zsh(uint64_t *nanoseconds)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now))
		return -1;
	*nanoseconds = (uint64_t)now.tv_sec * 1000000000ULL +
		(uint64_t)now.tv_nsec;
	return 0;
}

static int cap_duration_zsh(const struct cli_options_zsh *options,
			    uint32_t requested_ms, uint32_t *allowed_ms)
{
	uint64_t now_ns, remaining_ns, remaining_ms;

	if (requested_ms > GPIOCTL_CLI_MAX_DURATION_MS) {
		errno = ERANGE;
		return -1;
	}
	*allowed_ms = requested_ms;
	if (!options->timeout_set)
		return 0;
	if (monotonic_ns_zsh(&now_ns))
		return -1;
	if (now_ns >= options->deadline_ns) {
		errno = ETIMEDOUT;
		return -1;
	}
	remaining_ns = options->deadline_ns - now_ns;
	remaining_ms = (remaining_ns + 999999ULL) / 1000000ULL;
	if (remaining_ms < *allowed_ms)
		*allowed_ms = (uint32_t)remaining_ms;
	return 0;
}

static int sleep_ms_zsh(const struct cli_options_zsh *options,
			uint32_t milliseconds)
{
	uint32_t allowed_ms;
	struct timespec request = {
		.tv_sec = 0,
		.tv_nsec = 0,
	};
	bool truncated;

	if (cap_duration_zsh(options, milliseconds, &allowed_ms))
		return -1;
	truncated = allowed_ms < milliseconds;
	request.tv_sec = (time_t)(allowed_ms / 1000U);
	request.tv_nsec = (long)(allowed_ms % 1000U) * 1000000L;
	while (nanosleep(&request, &request)) {
		if (errno != EINTR)
			return -1;
	}
	if (truncated) {
		errno = ETIMEDOUT;
		return -1;
	}
	return 0;
}

static int validate_repeated_duration_zsh(uint32_t count, uint32_t first_ms,
					  uint32_t second_ms)
{
	uint64_t per_cycle = (uint64_t)first_ms + second_ms;

	if (!count || count > GPIOCTL_CLI_MAX_CYCLES ||
	    first_ms > GPIOCTL_CLI_MAX_DURATION_MS ||
	    second_ms > GPIOCTL_CLI_MAX_DURATION_MS ||
	    per_cycle * count > GPIOCTL_CLI_MAX_DURATION_MS) {
		errno = ERANGE;
		return -1;
	}
	return 0;
}

static const char *default_config_path_zsh(void)
{
	const char *environment = getenv("GPIOCTL_ZSH_BOARD_CONFIG");

	if (environment && *environment)
		return environment;
	if (!access("config/phytium-pi-v1.conf", R_OK))
		return "config/phytium-pi-v1.conf";
	if (!access("../config/phytium-pi-v1.conf", R_OK))
		return "../config/phytium-pi-v1.conf";
	return "/etc/gpioctl_zsh/board.conf";
}

static int parse_generic_gpio_name_zsh(const char *name,
				       struct target_zsh *target)
{
	unsigned int controller, offset;
	char trailing;

	if (sscanf(name, "GPIO%u_%u%c", &controller, &offset, &trailing) != 2 ||
	    controller > 255U || offset > 255U)
		return -1;
	if (snprintf(target->device, sizeof(target->device),
		     "/dev/gpio%u_zsh", controller) >= (int)sizeof(target->device))
		return -1;
	target->offset = offset;
	(void)snprintf(target->alias, sizeof(target->alias), "%s", name);
	return 0;
}

static int parse_direct_target_zsh(const char *text, struct target_zsh *target)
{
	const char *colon = strrchr(text, ':');
	uint32_t offset;
	size_t device_length;

	if (!colon || colon == text || parse_u32_zsh(colon + 1, &offset))
		return -1;
	device_length = (size_t)(colon - text);
	if (device_length >= sizeof(target->device))
		return -1;
	memcpy(target->device, text, device_length);
	target->device[device_length] = '\0';
	target->offset = offset;
	(void)snprintf(target->alias, sizeof(target->alias), "%s", text);
	return 0;
}

static int resolve_from_file_zsh(const struct cli_options_zsh *options,
				 const char *path, const char *name,
				 struct target_zsh *target)
{
	FILE *stream;
	char line[512];
	unsigned int line_number = 0;
	bool found = false;

	stream = fopen(path, "r");
	if (!stream)
		return -1;
	while (fgets(line, sizeof(line), stream)) {
		struct target_zsh candidate = {0};
		unsigned int active_low;
		unsigned int offset;
		int fields;

		line_number++;
		if (line[0] == '#' || isspace((unsigned char)line[0]))
			continue;
		fields = sscanf(line, "%63s %255s %u %u %63s %63s %63s",
				candidate.alias, candidate.device, &offset,
				&active_low, candidate.pad, candidate.physical_pin,
				candidate.description);
		if (fields != 7 || active_low > 1U) {
			errno = EINVAL;
			if (options->json) {
				fputs("{\"ok\":false,\"type\":\"config-error\","
				      "\"source\":", stderr);
				json_string_zsh(stderr, path);
				fprintf(stderr, ",\"line\":%u,\"errno\":%d,"
					"\"error\":\"invalid board mapping\"}\n",
					line_number, errno);
			} else
				fprintf(stderr, "%s:%u: invalid board mapping\n", path,
					line_number);
			fclose(stream);
			return -1;
		}
		if (strcmp(candidate.alias, name))
			continue;
		if (found) {
			errno = EEXIST;
			if (options->json) {
				fputs("{\"ok\":false,\"type\":\"config-error\","
				      "\"source\":", stderr);
				json_string_zsh(stderr, path);
				fprintf(stderr, ",\"line\":%u,\"errno\":%d,"
					"\"error\":\"duplicate alias\"}\n",
					line_number, errno);
			} else
				fprintf(stderr, "%s:%u: duplicate alias %s\n", path,
					line_number, name);
			fclose(stream);
			return -1;
		}
		candidate.offset = offset;
		candidate.line_flags = active_low ? GPIOCTL_ZSH_LINE_ACTIVE_LOW : 0U;
		*target = candidate;
		found = true;
	}
	fclose(stream);
	if (found)
		return 0;
	errno = ENOENT;
	return -1;
}

static int resolve_target_zsh(const struct cli_options_zsh *options,
			      const char *name, struct target_zsh *target)
{
	const char *config_path = options->config_path ?: default_config_path_zsh();

	memset(target, 0, sizeof(*target));
	if (!resolve_from_file_zsh(options, config_path, name, target))
		return 0;
	if (errno != ENOENT)
		return -1;
	if (!strncmp(name, "/dev/", 5))
		return parse_direct_target_zsh(name, target);
	if (!strncmp(name, "GPIO", 4))
		return parse_generic_gpio_name_zsh(name, target);
	errno = ENOENT;
	return -1;
}

static void print_error_zsh(const struct cli_options_zsh *options,
			    const char *operation, const char *subject)
{
	error_reported_zsh = true;
	if (options->json) {
		fputs("{\"ok\":false,\"operation\":", stderr);
		json_string_zsh(stderr, operation);
		fputs(",\"subject\":", stderr);
		json_string_zsh(stderr, subject);
		fprintf(stderr, ",\"errno\":%d,\"error\":", errno);
		json_string_zsh(stderr, strerror(errno));
		fputs("}\n", stderr);
	}
	else
		fprintf(stderr, "%s %s failed: %s\n", operation,
			subject ?: "", strerror(errno));
}

static void print_batch_error_zsh(const struct cli_options_zsh *options,
				  const char *operation, const char *subject,
				  const struct gpioctl_zsh_batch *batch)
{
	int saved_errno = errno;

	error_reported_zsh = true;
	if (options->json) {
		fputs("{\"ok\":false,\"operation\":", stderr);
		json_string_zsh(stderr, operation);
		fputs(",\"subject\":", stderr);
		json_string_zsh(stderr, subject);
		fprintf(stderr,
			",\"errno\":%d,\"error\":", saved_errno);
		json_string_zsh(stderr, strerror(saved_errno));
		fprintf(stderr,
			",\"failed_index\":%" PRId32
			",\"rollback_error\":%" PRId32 "}\n",
			batch->failed_index, batch->rollback_error);
	} else {
		fprintf(stderr,
			"%s %s failed: %s (failed_index=%" PRId32
			", rollback_error=%" PRId32 ")\n",
			operation, subject ?: "", strerror(saved_errno),
			batch->failed_index, batch->rollback_error);
	}
	errno = saved_errno;
}

static int command_sleep_zsh(const struct cli_options_zsh *options,
			     uint32_t milliseconds)
{
	if (sleep_ms_zsh(options, milliseconds)) {
		print_error_zsh(options, "sleep", "");
		return -1;
	}
	if (options->json)
		printf("{\"ok\":true,\"operation\":\"sleep\","
		       "\"milliseconds\":%" PRIu32 "}\n", milliseconds);
	return 0;
}

static void print_target_zsh(const struct cli_options_zsh *options,
			     const struct target_zsh *target)
{
	if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"resolve\",\"alias\":", stdout);
		json_string_zsh(stdout, target->alias);
		fputs(",\"device\":", stdout);
		json_string_zsh(stdout, target->device);
		fprintf(stdout, ",\"offset\":%" PRIu32 ",\"active_low\":%s,"
			"\"pad\":", target->offset,
			target->line_flags ? "true" : "false");
		json_string_zsh(stdout, target->pad);
		fputs(",\"physical_pin\":", stdout);
		json_string_zsh(stdout, target->physical_pin);
		fputs(",\"description\":", stdout);
		json_string_zsh(stdout, target->description);
		fputs("}\n", stdout);
	}
	else
		printf("alias=%s device=%s offset=%" PRIu32
		       " active_low=%u pad=%s physical_pin=%s description=%s\n",
		       target->alias, target->device, target->offset,
		       target->line_flags ? 1U : 0U, target->pad,
		       target->physical_pin, target->description);
}

static struct gpioctl_zsh_handle *
open_target_zsh(const struct cli_options_zsh *options,
		const struct target_zsh *target, uint32_t direction,
		uint32_t initial_value)
{
	struct gpioctl_zsh_handle *handle;
	uint32_t offset = target->offset;

	if (options->dry_run)
		return (struct gpioctl_zsh_handle *)(uintptr_t)1U;
	handle = gpioctl_zsh_open(target->device);
	if (!handle)
		return NULL;
	if (gpioctl_zsh_lease(handle, &offset, 1,
			direction == GPIOCTL_ZSH_DIRECTION_INPUT ?
			GPIOCTL_ZSH_LEASE_INPUT_ONLY : 0) ||
	    gpioctl_zsh_config(handle, offset, direction, initial_value,
			       GPIOCTL_ZSH_BIAS_AS_IS, target->line_flags, 0)) {
		int saved_errno = errno;

		gpioctl_zsh_close(handle);
		errno = saved_errno;
		return NULL;
	}
	return handle;
}

static void close_target_zsh(const struct cli_options_zsh *options,
			     struct gpioctl_zsh_handle *handle)
{
	if (!options->dry_run)
		gpioctl_zsh_close(handle);
}

static int command_list_zsh(const struct cli_options_zsh *options)
{
	glob_t matches = {0};
	size_t i;
	int ret;

	ret = glob("/dev/gpio*_zsh", 0, NULL, &matches);
	if (ret == GLOB_NOMATCH) {
		errno = ENODEV;
		print_error_zsh(options, "list", "/dev/gpio*_zsh");
		return -1;
	}
	if (ret) {
		errno = EIO;
		return -1;
	}
	for (i = 0; i < matches.gl_pathc; i++) {
		struct gpioctl_zsh_handle *handle = gpioctl_zsh_open(matches.gl_pathv[i]);
		struct gpioctl_zsh_caps caps;

		if (!handle || gpioctl_zsh_get_caps(handle, &caps)) {
			print_error_zsh(options, "inspect", matches.gl_pathv[i]);
			gpioctl_zsh_close(handle);
			if (options->strict) {
				globfree(&matches);
				return -1;
			}
			continue;
		}
		if (options->json) {
			fputs("{\"ok\":true,\"operation\":\"list\",\"device\":",
			      stdout);
			json_string_zsh(stdout, matches.gl_pathv[i]);
			fprintf(stdout, ",\"controller\":%" PRIu32
				",\"lines\":%" PRIu32 ",\"caps\":%" PRIu64 "}\n",
				caps.controller_id, caps.line_count,
				(uint64_t)caps.capabilities);
		}
		else
			printf("%s controller=%" PRIu32 " lines=%" PRIu32
			       " caps=0x%016" PRIx64 "\n",
			       matches.gl_pathv[i], caps.controller_id,
			       caps.line_count, (uint64_t)caps.capabilities);
		gpioctl_zsh_close(handle);
	}
	globfree(&matches);
	return 0;
}

static int command_info_zsh(const struct cli_options_zsh *options,
			    const char *name)
{
	struct target_zsh target;
	struct gpioctl_zsh_handle *handle;
	struct gpioctl_zsh_caps caps;
	struct gpioctl_zsh_line_caps line_caps;
	struct gpioctl_zsh_line_policy policy;

	if (resolve_target_zsh(options, name, &target)) {
		print_error_zsh(options, "resolve", name);
		return -1;
	}
	handle = gpioctl_zsh_open(target.device);
	if (!handle || gpioctl_zsh_get_caps(handle, &caps) ||
	    gpioctl_zsh_get_line_caps(handle, target.offset, &line_caps) ||
	    gpioctl_zsh_get_line_policy(handle, target.offset, &policy)) {
		print_error_zsh(options, "info", name);
		gpioctl_zsh_close(handle);
		return -1;
	}
	if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"info\",\"alias\":", stdout);
		json_string_zsh(stdout, target.alias);
		fputs(",\"device\":", stdout);
		json_string_zsh(stdout, target.device);
		fprintf(stdout, ",\"offset\":%" PRIu32 ",\"active_low\":%s,"
			"\"pad\":", target.offset,
			target.line_flags ? "true" : "false");
		json_string_zsh(stdout, target.pad);
		fputs(",\"physical_pin\":", stdout);
		json_string_zsh(stdout, target.physical_pin);
		fputs(",\"description\":", stdout);
		json_string_zsh(stdout, target.description);
		fprintf(stdout, ",\"controller_caps\":%" PRIu64
			",\"line_caps\":%" PRIu64 ",\"drive_min\":%" PRIu32
			",\"drive_max\":%" PRIu32 ",\"policy_flags\":%" PRIu32
			",\"safe_direction\":%" PRIu32
			",\"safe_value\":%" PRIu32 ",\"safe_bias\":%" PRIu32
			"}\n", (uint64_t)caps.capabilities,
			(uint64_t)line_caps.capabilities, line_caps.drive_level_min,
			line_caps.drive_level_max, policy.flags,
			policy.safe_direction, policy.safe_value, policy.safe_bias);
	} else {
		print_target_zsh(options, &target);
		printf("controller_caps=0x%016" PRIx64
		       " line_caps=0x%016" PRIx64 " drive=%" PRIu32 "..%" PRIu32
		       " policy=0x%08" PRIx32 " safe=%s:%" PRIu32
		       " bias=%" PRIu32 "\n", (uint64_t)caps.capabilities,
		       (uint64_t)line_caps.capabilities, line_caps.drive_level_min,
		       line_caps.drive_level_max, policy.flags,
		       policy.safe_direction == GPIOCTL_ZSH_DIRECTION_OUTPUT ?
		       "out" : "in", policy.safe_value, policy.safe_bias);
	}
	gpioctl_zsh_close(handle);
	return 0;
}

static int command_get_zsh(const struct cli_options_zsh *options,
			   const char *name)
{
	struct target_zsh target;
	struct gpioctl_zsh_handle *handle;
	uint32_t value, offset;
	int ret;

	if (resolve_target_zsh(options, name, &target)) {
		print_error_zsh(options, "resolve", name);
		return -1;
	}
	handle = open_target_zsh(options, &target, GPIOCTL_ZSH_DIRECTION_INPUT, 0);
	if (!handle) {
		print_error_zsh(options, "get", name);
		return -1;
	}
	offset = target.offset;
	ret = options->dry_run ? 0 : gpioctl_zsh_get_values(handle, &offset, 1, &value);
	if (ret)
		print_error_zsh(options, "get", name);
	else if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"get\",\"target\":", stdout);
		json_string_zsh(stdout, name);
		fprintf(stdout, ",\"value\":%" PRIu32 ",\"dry_run\":%s}\n",
			options->dry_run ? 0U : value & 1U,
			options->dry_run ? "true" : "false");
	}
	else
		printf("%s=%" PRIu32 "%s\n", name,
		       options->dry_run ? 0U : value & 1U,
		       options->dry_run ? " dry-run" : "");
	close_target_zsh(options, handle);
	return ret;
}

static int command_set_zsh(const struct cli_options_zsh *options,
			   const char *name, uint32_t value, uint32_t hold_ms)
{
	struct target_zsh target;
	struct gpioctl_zsh_handle *handle;

	if (value > 1U || hold_ms > GPIOCTL_CLI_MAX_DURATION_MS) {
		errno = EINVAL;
		return -1;
	}
	if (resolve_target_zsh(options, name, &target)) {
		print_error_zsh(options, "resolve", name);
		return -1;
	}
	handle = open_target_zsh(options, &target, GPIOCTL_ZSH_DIRECTION_OUTPUT,
				 value);
	if (!handle) {
		print_error_zsh(options, "set", name);
		return -1;
	}
	if (!options->dry_run && hold_ms && sleep_ms_zsh(options, hold_ms)) {
		print_error_zsh(options, "set", name);
		close_target_zsh(options, handle);
		return -1;
	}
	if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"set\",\"target\":", stdout);
		json_string_zsh(stdout, name);
		fprintf(stdout, ",\"value\":%" PRIu32 ",\"hold_ms\":%" PRIu32
			",\"dry_run\":%s}\n", value, hold_ms,
			options->dry_run ? "true" : "false");
	}
	else
		printf("%s=%" PRIu32 " hold_ms=%" PRIu32 "%s\n", name, value,
		       hold_ms, options->dry_run ? " dry-run" : "");
	close_target_zsh(options, handle);
	return 0;
}

static int command_blink_zsh(const struct cli_options_zsh *options,
			     const char *name, uint32_t count,
			     uint32_t on_ms, uint32_t off_ms)
{
	struct target_zsh target;
	struct gpioctl_zsh_handle *handle;
	uint32_t offset, cycle;
	int ret = 0;

	if (validate_repeated_duration_zsh(count, on_ms, off_ms) ||
	    resolve_target_zsh(options, name, &target)) {
		if (!errno)
			errno = EINVAL;
		print_error_zsh(options, "blink", name);
		return -1;
	}
	handle = open_target_zsh(options, &target, GPIOCTL_ZSH_DIRECTION_OUTPUT, 0);
	if (!handle) {
		print_error_zsh(options, "blink", name);
		return -1;
	}
	offset = target.offset;
	for (cycle = 0; cycle < count; cycle++) {
		if (!options->dry_run && gpioctl_zsh_set_values(handle, &offset, 1, 1U)) {
			ret = -1;
			break;
		}
		if (!options->dry_run && sleep_ms_zsh(options, on_ms)) {
			ret = -1;
			break;
		}
		if (!options->dry_run && gpioctl_zsh_set_values(handle, &offset, 1, 0U)) {
			ret = -1;
			break;
		}
		if (!options->dry_run && sleep_ms_zsh(options, off_ms)) {
			ret = -1;
			break;
		}
	}
	if (ret)
		print_error_zsh(options, "blink", name);
	else if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"blink\",\"target\":", stdout);
		json_string_zsh(stdout, name);
		fprintf(stdout, ",\"cycles\":%" PRIu32 ",\"dry_run\":%s}\n",
			count, options->dry_run ? "true" : "false");
	}
	else
		printf("blink target=%s cycles=%" PRIu32 " final=0\n", name, count);
	close_target_zsh(options, handle);
	return ret;
}

static int command_pair_blink_zsh(const struct cli_options_zsh *options,
				  const char *name_a, const char *name_b,
				  uint32_t count, uint32_t interval_ms)
{
	struct target_zsh targets[2];
	struct gpioctl_zsh_handle *handles[2] = {NULL, NULL};
	uint32_t offsets[2];
	uint32_t cycle;
	int ret = -1;

	if (validate_repeated_duration_zsh(count, interval_ms, interval_ms) ||
	    resolve_target_zsh(options, name_a, &targets[0]) ||
	    resolve_target_zsh(options, name_b, &targets[1])) {
		if (!errno)
			errno = EINVAL;
		goto out;
	}
	handles[0] = open_target_zsh(options, &targets[0],
				     GPIOCTL_ZSH_DIRECTION_OUTPUT, 0);
	if (!handles[0])
		goto out;
	handles[1] = open_target_zsh(options, &targets[1],
				     GPIOCTL_ZSH_DIRECTION_OUTPUT, 0);
	if (!handles[1])
		goto out;
	offsets[0] = targets[0].offset;
	offsets[1] = targets[1].offset;
	for (cycle = 0; cycle < count; cycle++) {
		if (!options->dry_run &&
		    (gpioctl_zsh_set_values(handles[0], &offsets[0], 1, 1U) ||
		     gpioctl_zsh_set_values(handles[1], &offsets[1], 1, 0U)))
			goto out;
		if (!options->dry_run && sleep_ms_zsh(options, interval_ms))
			goto out;
		if (!options->dry_run &&
		    (gpioctl_zsh_set_values(handles[0], &offsets[0], 1, 0U) ||
		     gpioctl_zsh_set_values(handles[1], &offsets[1], 1, 1U)))
			goto out;
		if (!options->dry_run && sleep_ms_zsh(options, interval_ms))
			goto out;
	}
	if (!options->dry_run) {
		(void)gpioctl_zsh_set_values(handles[0], &offsets[0], 1, 0U);
		(void)gpioctl_zsh_set_values(handles[1], &offsets[1], 1, 0U);
	}
	ret = 0;
out:
	if (ret)
		print_error_zsh(options, "pair-blink", name_a);
	else if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"pair-blink\",\"a\":",
		      stdout);
		json_string_zsh(stdout, name_a);
		fputs(",\"b\":", stdout);
		json_string_zsh(stdout, name_b);
		fprintf(stdout, ",\"cycles\":%" PRIu32 ",\"dry_run\":%s}\n",
			count, options->dry_run ? "true" : "false");
	}
	else
		printf("pair-blink a=%s b=%s cycles=%" PRIu32 " final=0,0\n",
		       name_a, name_b, count);
	close_target_zsh(options, handles[1]);
	close_target_zsh(options, handles[0]);
	return ret;
}

static int parse_iopad_option_zsh(const char *option,
				   struct gpioctl_zsh_iopad_config *config)
{
	uint32_t value;

	if (!strncmp(option, "mux=", 4)) {
		if (config->flags & GPIOCTL_ZSH_IOPAD_APPLY_MUX)
			return -1;
		if (strcmp(option + 4, "gpio"))
			return -1;
		config->mux_state = GPIOCTL_ZSH_MUX_GPIO;
		config->flags |= GPIOCTL_ZSH_IOPAD_APPLY_MUX;
		return 0;
	}
	if (!strncmp(option, "bias=", 5)) {
		if (config->flags & GPIOCTL_ZSH_IOPAD_APPLY_BIAS)
			return -1;
		if (!strcmp(option + 5, "none"))
			config->bias = GPIOCTL_ZSH_BIAS_DISABLE;
		else if (!strcmp(option + 5, "up"))
			config->bias = GPIOCTL_ZSH_BIAS_PULL_UP;
		else if (!strcmp(option + 5, "down"))
			config->bias = GPIOCTL_ZSH_BIAS_PULL_DOWN;
		else
			return -1;
		config->flags |= GPIOCTL_ZSH_IOPAD_APPLY_BIAS;
		return 0;
	}
	if (!strncmp(option, "drive=", 6)) {
		if ((config->flags & GPIOCTL_ZSH_IOPAD_APPLY_DRIVE) ||
		    parse_u32_zsh(option + 6, &value) || value > 15U)
			return -1;
		config->drive_level = value;
		config->flags |= GPIOCTL_ZSH_IOPAD_APPLY_DRIVE;
		return 0;
	}
	return -1;
}

static int command_iopad_zsh(const struct cli_options_zsh *options,
			     const char *name, int option_count, char **option_values)
{
	struct gpioctl_zsh_iopad_config config = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(config),
	};
	struct gpioctl_zsh_handle *handle = NULL;
	struct target_zsh target;
	uint32_t offset;
	int i, ret = -1;

	if (resolve_target_zsh(options, name, &target))
		goto out;
	for (i = 0; i < option_count; i++)
		if (parse_iopad_option_zsh(option_values[i], &config)) {
			errno = EINVAL;
			goto out;
		}
	if (!config.flags) {
		errno = EINVAL;
		goto out;
	}
	config.offset = target.offset;
	if (!options->dry_run) {
		offset = target.offset;
		handle = gpioctl_zsh_open(target.device);
		if (!handle || gpioctl_zsh_lease(handle, &offset, 1,
					 GPIOCTL_ZSH_LEASE_INPUT_ONLY) ||
		    gpioctl_zsh_iopad_config(handle, config.offset, config.bias,
					    config.drive_level, config.mux_state,
					    config.flags))
			goto out;
	}
	if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"iopad\",\"target\":", stdout);
		json_string_zsh(stdout, name);
		fprintf(stdout, ",\"flags\":%" PRIu32 ",\"bias\":%" PRIu32
			",\"drive\":%" PRIu32 ",\"mux\":%" PRIu32
			",\"dry_run\":%s}\n", config.flags, config.bias,
			config.drive_level, config.mux_state,
			options->dry_run ? "true" : "false");
	}
	else
		printf("iopad target=%s flags=0x%08" PRIx32
		       " bias=%" PRIu32 " drive=%" PRIu32 " mux=%" PRIu32 "%s\n",
		       name, config.flags, config.bias, config.drive_level,
		       config.mux_state, options->dry_run ? " dry-run" : "");
	ret = 0;
out:
	if (ret)
		print_error_zsh(options, "iopad", name);
	gpioctl_zsh_close(handle);
	return ret;
}

static const char *iopad_bias_name_zsh(uint32_t bias)
{
	switch (bias) {
	case GPIOCTL_ZSH_BIAS_DISABLE:
		return "none";
	case GPIOCTL_ZSH_BIAS_PULL_UP:
		return "up";
	case GPIOCTL_ZSH_BIAS_PULL_DOWN:
		return "down";
	default:
		return "unknown";
	}
}

static const char *iopad_mux_name_zsh(uint32_t mux_state)
{
	switch (mux_state) {
	case GPIOCTL_ZSH_MUX_GPIO:
		return "gpio";
	case GPIOCTL_ZSH_MUX_OTHER:
		return "other";
	default:
		return "unknown";
	}
}

static int command_iopad_get_zsh(const struct cli_options_zsh *options,
				 const char *name)
{
	struct gpioctl_zsh_iopad_config config;
	struct gpioctl_zsh_handle *handle = NULL;
	struct target_zsh target;
	int ret = -1;

	if (resolve_target_zsh(options, name, &target))
		goto out;
	handle = gpioctl_zsh_open(target.device);
	if (!handle || gpioctl_zsh_iopad_get_config(handle, target.offset,
						    &config))
		goto out;
	if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"iopad-get\",\"target\":",
		      stdout);
		json_string_zsh(stdout, name);
		fprintf(stdout, ",\"flags\":%" PRIu32 ",\"bias\":%" PRIu32
			",\"bias_name\":", config.flags, config.bias);
		json_string_zsh(stdout, iopad_bias_name_zsh(config.bias));
		fprintf(stdout, ",\"drive\":%" PRIu32 ",\"mux\":%" PRIu32
			",\"mux_name\":", config.drive_level, config.mux_state);
		json_string_zsh(stdout, iopad_mux_name_zsh(config.mux_state));
		fputs("}\n", stdout);
	}
	else
		printf("iopad target=%s flags=0x%08" PRIx32
		       " bias=%s drive=%" PRIu32 " mux=%s\n",
		       name, config.flags, iopad_bias_name_zsh(config.bias),
		       config.drive_level, iopad_mux_name_zsh(config.mux_state));
	ret = 0;
out:
	if (ret)
		print_error_zsh(options, "iopad-get", name);
	gpioctl_zsh_close(handle);
	return ret;
}

static int command_stats_zsh(const struct cli_options_zsh *options,
			     const char *name)
{
	struct target_zsh target;
	struct gpioctl_zsh_handle *handle;
	struct gpioctl_zsh_stats stats;
	const char *device = name;

	if (strncmp(name, "/dev/", 5)) {
		if (resolve_target_zsh(options, name, &target)) {
			print_error_zsh(options, "resolve", name);
			return -1;
		}
		device = target.device;
	}
	handle = gpioctl_zsh_open(device);
	if (!handle || gpioctl_zsh_get_stats(handle, &stats)) {
		print_error_zsh(options, "stats", device);
		gpioctl_zsh_close(handle);
		return -1;
	}
	if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"stats\",\"device\":", stdout);
		json_string_zsh(stdout, device);
		fprintf(stdout, ",\"operations\":%" PRIu64
			",\"errors\":%" PRIu64 ",\"denials\":%" PRIu64
			",\"conflicts\":%" PRIu64 ",\"events\":%" PRIu64
			",\"drops\":%" PRIu64 ",\"active_leases\":%" PRIu32
			"}\n", (uint64_t)stats.operations, (uint64_t)stats.errors,
			(uint64_t)stats.denials, (uint64_t)stats.lease_conflicts,
			(uint64_t)stats.events, (uint64_t)stats.event_drops,
			stats.active_leases);
	}
	else
		printf("device=%s operations=%" PRIu64 " errors=%" PRIu64
		       " denials=%" PRIu64 " conflicts=%" PRIu64
		       " events=%" PRIu64 " drops=%" PRIu64
		       " active_leases=%" PRIu32 "\n", device,
		       (uint64_t)stats.operations, (uint64_t)stats.errors,
		       (uint64_t)stats.denials, (uint64_t)stats.lease_conflicts,
		       (uint64_t)stats.events, (uint64_t)stats.event_drops,
		       stats.active_leases);
	gpioctl_zsh_close(handle);
	return 0;
}

static struct held_line_zsh *find_held_zsh(struct runtime_zsh *runtime,
					  const struct target_zsh *target)
{
	size_t i;

	for (i = 0; i < GPIOCTL_CLI_MAX_HELD; i++)
		if (runtime->held[i].used &&
		    runtime->held[i].target.offset == target->offset &&
		    !strcmp(runtime->held[i].target.device, target->device))
			return &runtime->held[i];
	return NULL;
}

static int command_acquire_zsh(struct runtime_zsh *runtime, const char *name,
			       uint32_t direction, uint32_t initial_value)
{
	struct target_zsh target;
	struct held_line_zsh *slot = NULL;
	size_t i;

	if (resolve_target_zsh(&runtime->options, name, &target))
		return -1;
	if (find_held_zsh(runtime, &target)) {
		errno = EALREADY;
		return -1;
	}
	for (i = 0; i < GPIOCTL_CLI_MAX_HELD; i++)
		if (!runtime->held[i].used) {
			slot = &runtime->held[i];
			break;
		}
	if (!slot) {
		errno = ENOSPC;
		return -1;
	}
	slot->handle = open_target_zsh(&runtime->options, &target, direction,
				       initial_value);
	if (!slot->handle)
		return -1;
	slot->target = target;
	slot->used = true;
	if (runtime->options.json) {
		fputs("{\"ok\":true,\"operation\":\"acquire\",\"target\":", stdout);
		json_string_zsh(stdout, name);
		fprintf(stdout, ",\"direction\":\"%s\",\"initial\":%" PRIu32
			",\"dry_run\":%s}\n",
			direction == GPIOCTL_ZSH_DIRECTION_OUTPUT ? "out" : "in",
			initial_value, runtime->options.dry_run ? "true" : "false");
	} else
		printf("acquired %s direction=%s initial=%" PRIu32 "%s\n", name,
		       direction == GPIOCTL_ZSH_DIRECTION_OUTPUT ? "out" : "in",
		       initial_value, runtime->options.dry_run ? " dry-run" : "");
	return 0;
}

static int command_value_zsh(struct runtime_zsh *runtime, const char *name,
			     const uint32_t *new_value)
{
	struct target_zsh target;
	struct held_line_zsh *slot;
	uint32_t value = 0;
	int ret;

	if (resolve_target_zsh(&runtime->options, name, &target))
		return -1;
	slot = find_held_zsh(runtime, &target);
	if (!slot) {
		errno = ENOENT;
		return -1;
	}
	if (runtime->options.dry_run)
		ret = 0;
	else if (new_value)
		ret = gpioctl_zsh_set_values(slot->handle, &target.offset, 1,
					     *new_value);
	else
		ret = gpioctl_zsh_get_values(slot->handle, &target.offset, 1, &value);
	if (ret)
		return -1;
	if (runtime->options.json) {
		fputs("{\"ok\":true,\"operation\":\"value\",\"target\":", stdout);
		json_string_zsh(stdout, name);
		fprintf(stdout, ",\"value\":%" PRIu32 ",\"write\":%s,"
			"\"dry_run\":%s}\n",
			new_value ? *new_value : value & 1U,
			new_value ? "true" : "false",
			runtime->options.dry_run ? "true" : "false");
	} else
		printf("%s=%" PRIu32 "%s\n", name,
		       new_value ? *new_value : value & 1U,
		       runtime->options.dry_run ? " dry-run" : "");
	return 0;
}

static int command_release_zsh(struct runtime_zsh *runtime, const char *name)
{
	struct target_zsh target;
	struct held_line_zsh *slot;

	if (resolve_target_zsh(&runtime->options, name, &target))
		return -1;
	slot = find_held_zsh(runtime, &target);
	if (!slot) {
		errno = ENOENT;
		return -1;
	}
	close_target_zsh(&runtime->options, slot->handle);
	memset(slot, 0, sizeof(*slot));
	if (runtime->options.json) {
		fputs("{\"ok\":true,\"operation\":\"release\",\"target\":", stdout);
		json_string_zsh(stdout, name);
		fputs("}\n", stdout);
	} else
		printf("released %s\n", name);
	return 0;
}

static void cleanup_runtime_zsh(struct runtime_zsh *runtime)
{
	size_t i;

	for (i = 0; i < GPIOCTL_CLI_MAX_HELD; i++)
		if (runtime->held[i].used) {
			close_target_zsh(&runtime->options, runtime->held[i].handle);
			memset(&runtime->held[i], 0, sizeof(runtime->held[i]));
		}
	memset(&runtime->transaction, 0, sizeof(runtime->transaction));
}

static int parse_edge_zsh(const char *text, uint32_t *edge)
{
	if (!strcmp(text, "rising"))
		*edge = GPIOCTL_ZSH_EDGE_RISING;
	else if (!strcmp(text, "falling"))
		*edge = GPIOCTL_ZSH_EDGE_FALLING;
	else if (!strcmp(text, "both"))
		*edge = GPIOCTL_ZSH_EDGE_BOTH;
	else
		return -1;
	return 0;
}

static int command_watch_zsh(const struct cli_options_zsh *options,
			     const char *name, uint32_t edge,
			     uint32_t timeout_ms, uint32_t wanted,
			     uint32_t debounce_us)
{
	struct target_zsh target;
	struct gpioctl_zsh_handle *handle;
	struct pollfd pollfd;
	uint32_t received = 0;
	int ret = -1;

	if (resolve_target_zsh(options, name, &target))
		return -1;
	handle = open_target_zsh(options, &target, GPIOCTL_ZSH_DIRECTION_INPUT, 0);
	if (!handle)
		return -1;
	if (options->dry_run) {
		if (options->json) {
			fputs("{\"ok\":true,\"operation\":\"watch\",\"target\":",
			      stdout);
			json_string_zsh(stdout, name);
			fputs(",\"dry_run\":true}\n", stdout);
		}
		else
			printf("watch %s dry-run\n", name);
		ret = 0;
		goto out;
	}
	if (gpioctl_zsh_event_config(handle, target.offset, edge, debounce_us))
		goto out;
	pollfd.fd = gpioctl_zsh_fd(handle);
	pollfd.events = POLLIN;
	while (!wanted || received < wanted) {
		struct gpioctl_zsh_event events[8];
		ssize_t bytes;
		size_t i, count;
		uint32_t allowed_ms;
		int poll_timeout;
		int poll_ret;

		if (cap_duration_zsh(options, timeout_ms, &allowed_ms))
			goto out;
		poll_timeout = allowed_ms > (uint32_t)INT32_MAX ? INT32_MAX :
			(int)allowed_ms;
		poll_ret = poll(&pollfd, 1, poll_timeout);

		if (!poll_ret) {
			errno = ETIMEDOUT;
			break;
		}
		if (poll_ret < 0)
			goto out;
		bytes = gpioctl_zsh_read_events(handle, events, 8);
		if (bytes < 0)
			goto out;
		count = (size_t)bytes / sizeof(events[0]);
		for (i = 0; i < count; i++) {
			if (options->json) {
				fputs("{\"ok\":true,\"operation\":\"watch\","
				      "\"type\":\"event\",\"target\":", stdout);
				json_string_zsh(stdout, name);
				fprintf(stdout, ",\"edge\":%" PRIu32
					",\"timestamp_ns\":%" PRIu64
					",\"sequence\":%" PRIu64 ",\"flags\":%" PRIu32
					"}\n", events[i].edge,
					(uint64_t)events[i].timestamp_ns,
					(uint64_t)events[i].sequence, events[i].flags);
			}
			else
				printf("event target=%s edge=%" PRIu32
				       " timestamp_ns=%" PRIu64 " sequence=%" PRIu64
				       " flags=0x%" PRIx32 "\n", name, events[i].edge,
				       (uint64_t)events[i].timestamp_ns,
				       (uint64_t)events[i].sequence, events[i].flags);
			received++;
		}
	}
	if (wanted && received >= wanted)
		ret = 0;
out:
	if (ret)
		print_error_zsh(options, "watch", name);
	close_target_zsh(options, handle);
	return ret;
}

static int command_batch_set_zsh(const struct cli_options_zsh *options,
				 const char *device, uint32_t hold_ms,
				 int assignment_count, char **assignments)
{
	struct gpioctl_zsh_handle *handle = NULL;
	struct gpioctl_zsh_batch batch = {
		.abi_version = GPIOCTL_ZSH_ABI_VERSION,
		.struct_size = sizeof(batch),
		.count = (uint32_t)assignment_count,
		.failed_index = -1,
	};
	uint32_t offsets[GPIOCTL_ZSH_MAX_BATCH_OPS];
	bool batch_error_reported = false;
	int i, ret = -1;

	if (assignment_count <= 0 ||
	    assignment_count > (int)GPIOCTL_ZSH_MAX_BATCH_OPS ||
	    hold_ms > GPIOCTL_CLI_MAX_DURATION_MS) {
		errno = EINVAL;
		return -1;
	}
	for (i = 0; i < assignment_count; i++) {
		char *equals = strchr(assignments[i], '=');
		uint32_t value;

		if (!equals || equals == assignments[i]) {
			errno = EINVAL;
			return -1;
		}
		*equals = '\0';
		if (parse_u32_zsh(assignments[i], &offsets[i]) ||
		    parse_u32_zsh(equals + 1, &value) || value > 1U) {
			*equals = '=';
			errno = EINVAL;
			return -1;
		}
		*equals = '=';
		batch.ops[i].opcode = GPIOCTL_ZSH_BATCH_CONFIG;
		batch.ops[i].offset = offsets[i];
		batch.ops[i].arg0 = GPIOCTL_ZSH_DIRECTION_OUTPUT;
		batch.ops[i].arg1 = value;
	}
	if (options->dry_run) {
		if (options->json) {
			fputs("{\"ok\":true,\"operation\":\"batch-set\","
			      "\"device\":", stdout);
			json_string_zsh(stdout, device);
			fprintf(stdout, ",\"count\":%d,\"hold_ms\":%" PRIu32
				",\"dry_run\":true}\n", assignment_count, hold_ms);
		} else
			printf("batch-set device=%s count=%d hold_ms=%" PRIu32
			       " dry-run\n", device, assignment_count, hold_ms);
		return 0;
	}
	handle = gpioctl_zsh_open(device);
	if (!handle || gpioctl_zsh_lease(handle, offsets,
					 (size_t)assignment_count, 0))
		goto out;
	if (gpioctl_zsh_batch(handle, &batch)) {
		print_batch_error_zsh(options, "batch-set", device, &batch);
		batch_error_reported = true;
		goto out;
	}
	if (hold_ms && sleep_ms_zsh(options, hold_ms))
		goto out;
	if (options->json) {
		fputs("{\"ok\":true,\"operation\":\"batch-set\",\"device\":",
		      stdout);
		json_string_zsh(stdout, device);
		fprintf(stdout, ",\"count\":%d,\"hold_ms\":%" PRIu32
			",\"dry_run\":false}\n", assignment_count, hold_ms);
	} else
		printf("batch-set device=%s count=%d hold_ms=%" PRIu32 "\n",
		       device, assignment_count, hold_ms);
	ret = 0;
out:
	if (ret && !batch_error_reported)
		print_error_zsh(options, "batch-set", device);
	gpioctl_zsh_close(handle);
	return ret;
}

static int command_transaction_begin_zsh(struct runtime_zsh *runtime,
					 const char *device)
{
	struct transaction_zsh *transaction = &runtime->transaction;

	if (transaction->active || strncmp(device, "/dev/", 5) ||
	    snprintf(transaction->device, sizeof(transaction->device), "%s",
		     device) >= (int)sizeof(transaction->device)) {
		errno = transaction->active ? EBUSY : EINVAL;
		return -1;
	}
	transaction->active = true;
	transaction->batch.abi_version = GPIOCTL_ZSH_ABI_VERSION;
	transaction->batch.struct_size = sizeof(transaction->batch);
	transaction->batch.failed_index = -1;
	if (runtime->options.json) {
		fputs("{\"ok\":true,\"operation\":\"transaction\","
		      "\"device\":", stdout);
		json_string_zsh(stdout, device);
		fputs("}\n", stdout);
	} else
		printf("transaction device=%s begun\n", device);
	return 0;
}

static int command_transaction_line_zsh(struct runtime_zsh *runtime,
					uint32_t offset, uint32_t direction,
					uint32_t value, uint32_t flags)
{
	struct transaction_zsh *transaction = &runtime->transaction;
	struct gpioctl_zsh_batch_op *operation;
	uint32_t i;

	if (!transaction->active || value > 1U ||
	    direction > GPIOCTL_ZSH_DIRECTION_OUTPUT ||
	    flags & ~GPIOCTL_ZSH_LINE_ACTIVE_LOW ||
	    transaction->batch.count >= GPIOCTL_ZSH_MAX_BATCH_OPS) {
		errno = EINVAL;
		return -1;
	}
	for (i = 0; i < transaction->batch.count; i++)
		if (transaction->batch.ops[i].offset == offset) {
			errno = EEXIST;
			return -1;
		}
	operation = &transaction->batch.ops[transaction->batch.count++];
	operation->opcode = GPIOCTL_ZSH_BATCH_CONFIG;
	operation->offset = offset;
	operation->arg0 = direction;
	operation->arg1 = value;
	operation->flags = flags;
	if (runtime->options.json)
		printf("{\"ok\":true,\"operation\":\"tx-line\","
		       "\"offset\":%" PRIu32 ",\"direction\":%" PRIu32 ","
		       "\"value\":%" PRIu32 ",\"flags\":%" PRIu32 "}\n",
		       offset, direction, value, flags);
	else
		printf("tx-line offset=%" PRIu32 " direction=%" PRIu32
		       " value=%" PRIu32 " flags=0x%08" PRIx32 "\n",
		       offset, direction, value, flags);
	return 0;
}

static int command_transaction_commit_zsh(struct runtime_zsh *runtime,
					  uint32_t hold_ms)
{
	struct transaction_zsh *transaction = &runtime->transaction;
	struct gpioctl_zsh_handle *handle = NULL;
	uint32_t offsets[GPIOCTL_ZSH_MAX_BATCH_OPS];
	uint32_t count, i;
	bool batch_error_reported = false;
	int ret = -1;

	if (!transaction->active || !transaction->batch.count ||
	    hold_ms > GPIOCTL_CLI_MAX_DURATION_MS) {
		errno = EINVAL;
		return -1;
	}
	count = transaction->batch.count;
	for (i = 0; i < count; i++)
		offsets[i] = transaction->batch.ops[i].offset;
	if (!runtime->options.dry_run) {
		handle = gpioctl_zsh_open(transaction->device);
		if (!handle || gpioctl_zsh_lease(handle, offsets, count, 0))
			goto out;
		if (gpioctl_zsh_batch(handle, &transaction->batch)) {
			print_batch_error_zsh(&runtime->options, "commit",
					      transaction->device,
					      &transaction->batch);
			batch_error_reported = true;
			goto out;
		}
	}
	if (!runtime->options.dry_run && hold_ms &&
	    sleep_ms_zsh(&runtime->options, hold_ms))
		goto out;
	if (runtime->options.json) {
		fputs("{\"ok\":true,\"operation\":\"commit\",\"device\":", stdout);
		json_string_zsh(stdout, transaction->device);
		fprintf(stdout, ",\"count\":%" PRIu32 ",\"hold_ms\":%" PRIu32
			",\"dry_run\":%s}\n", count, hold_ms,
			runtime->options.dry_run ? "true" : "false");
	}
	else
		printf("commit device=%s count=%" PRIu32 " hold_ms=%" PRIu32
		       "%s\n", transaction->device, count, hold_ms,
		       runtime->options.dry_run ? " dry-run" : "");
	ret = 0;
out:
	if (ret && !batch_error_reported)
		print_error_zsh(&runtime->options, "commit", transaction->device);
	gpioctl_zsh_close(handle);
	memset(transaction, 0, sizeof(*transaction));
	return ret;
}

static int command_transaction_abort_zsh(struct runtime_zsh *runtime)
{
	if (!runtime->transaction.active) {
		errno = EINVAL;
		return -1;
	}
	memset(&runtime->transaction, 0, sizeof(runtime->transaction));
	if (runtime->options.json)
		puts("{\"ok\":true,\"operation\":\"abort\"}");
	else
		puts("transaction aborted");
	return 0;
}

static int execute_tokens_zsh(struct runtime_zsh *runtime, int argc, char **argv)
{
	uint32_t a, b, c, d;
	uint32_t unused_budget;

	if (!argc)
		return 0;
	error_reported_zsh = false;
	if (cap_duration_zsh(&runtime->options, 0, &unused_budget)) {
		print_error_zsh(&runtime->options, "timeout", argv[0]);
		return -1;
	}
	if (!strcmp(argv[0], "list") && argc == 1)
		return command_list_zsh(&runtime->options);
	if (!strcmp(argv[0], "resolve") && argc == 2) {
		struct target_zsh target;

		if (resolve_target_zsh(&runtime->options, argv[1], &target)) {
			print_error_zsh(&runtime->options, "resolve", argv[1]);
			return -1;
		}
		print_target_zsh(&runtime->options, &target);
		return 0;
	}
	if (!strcmp(argv[0], "info") && argc == 2)
		return command_info_zsh(&runtime->options, argv[1]);
	if (!strcmp(argv[0], "get") && argc == 2)
		return command_get_zsh(&runtime->options, argv[1]);
	if (!strcmp(argv[0], "set") && (argc == 3 || argc == 4) &&
	    !parse_u32_zsh(argv[2], &a) &&
	    (argc == 3 || !parse_u32_zsh(argv[3], &b)))
		return command_set_zsh(&runtime->options, argv[1], a,
				       argc == 4 ? b : 0U);
	if (!strcmp(argv[0], "blink") && argc == 5 &&
	    !parse_u32_zsh(argv[2], &a) && !parse_u32_zsh(argv[3], &b) &&
	    !parse_u32_zsh(argv[4], &c))
		return command_blink_zsh(&runtime->options, argv[1], a, b, c);
	if (!strcmp(argv[0], "pair-blink") && argc == 5 &&
	    !parse_u32_zsh(argv[3], &a) && !parse_u32_zsh(argv[4], &b))
		return command_pair_blink_zsh(&runtime->options, argv[1], argv[2],
					      a, b);
	if (!strcmp(argv[0], "stats") && argc == 2)
		return command_stats_zsh(&runtime->options, argv[1]);
	if (!strcmp(argv[0], "sleep") && argc == 2 &&
	    !parse_u32_zsh(argv[1], &a))
		return command_sleep_zsh(&runtime->options, a);
	if (!strcmp(argv[0], "acquire") && (argc == 3 || argc == 4)) {
		uint32_t direction;

		if (!strcmp(argv[2], "in"))
			direction = GPIOCTL_ZSH_DIRECTION_INPUT;
		else if (!strcmp(argv[2], "out"))
			direction = GPIOCTL_ZSH_DIRECTION_OUTPUT;
		else
			goto invalid;
		a = 0;
		if (argc == 4 && parse_u32_zsh(argv[3], &a))
			goto invalid;
		if (a > 1U)
			goto invalid;
		return command_acquire_zsh(runtime, argv[1], direction, a);
	}
	if (!strcmp(argv[0], "value") && (argc == 2 || argc == 3)) {
		if (argc == 3 && (parse_u32_zsh(argv[2], &a) || a > 1U))
			goto invalid;
		return command_value_zsh(runtime, argv[1], argc == 3 ? &a : NULL);
	}
	if (!strcmp(argv[0], "release") && argc == 2)
		return command_release_zsh(runtime, argv[1]);
	if (!strcmp(argv[0], "watch") && argc >= 4 && argc <= 6 &&
	    !parse_edge_zsh(argv[2], &a) && !parse_u32_zsh(argv[3], &b)) {
		c = 1;
		d = 0;
		if (argc >= 5 && parse_u32_zsh(argv[4], &c))
			goto invalid;
		if (argc == 6 && parse_u32_zsh(argv[5], &d))
			goto invalid;
		return command_watch_zsh(&runtime->options, argv[1], a, b, c, d);
	}
	if (!strcmp(argv[0], "iopad-get") && argc == 2)
		return command_iopad_get_zsh(&runtime->options, argv[1]);
	if (!strcmp(argv[0], "iopad") && argc >= 3)
		return command_iopad_zsh(&runtime->options, argv[1], argc - 2,
					  &argv[2]);
	if (!strcmp(argv[0], "batch-set") && argc >= 4 &&
	    !parse_u32_zsh(argv[2], &a))
		return command_batch_set_zsh(&runtime->options, argv[1], a,
					     argc - 3, &argv[3]);
	if (!strcmp(argv[0], "transaction") && argc == 2)
		return command_transaction_begin_zsh(runtime, argv[1]);
	if (!strcmp(argv[0], "tx-line") && (argc == 4 || argc == 5) &&
	    !parse_u32_zsh(argv[1], &a) && !parse_u32_zsh(argv[3], &b) &&
	    b <= 1U) {
		if (!strcmp(argv[2], "in"))
			c = GPIOCTL_ZSH_DIRECTION_INPUT;
		else if (!strcmp(argv[2], "out"))
			c = GPIOCTL_ZSH_DIRECTION_OUTPUT;
		else
			goto invalid;
		d = 0;
		if (argc == 5) {
			if (strcmp(argv[4], "active-low"))
				goto invalid;
			d = GPIOCTL_ZSH_LINE_ACTIVE_LOW;
		}
		return command_transaction_line_zsh(runtime, a, c, b, d);
	}
	if (!strcmp(argv[0], "commit") && (argc == 1 || argc == 2)) {
		a = 0;
		if (argc == 2 && parse_u32_zsh(argv[1], &a))
			goto invalid;
		return command_transaction_commit_zsh(runtime, a);
	}
	if (!strcmp(argv[0], "abort") && argc == 1)
		return command_transaction_abort_zsh(runtime);

invalid:
	errno = EINVAL;
	print_error_zsh(&runtime->options, "command", argv[0]);
	return -1;
}

static int tokenize_zsh(char *line, char **tokens, int max_tokens)
{
	char *save = NULL;
	char *token;
	int count = 0;

	for (token = strtok_r(line, " \t\r\n", &save); token;
	     token = strtok_r(NULL, " \t\r\n", &save)) {
		if (token[0] == '#')
			break;
		if (count == max_tokens) {
			errno = E2BIG;
			return -1;
		}
		tokens[count++] = token;
	}
	return count;
}

static int run_stream_zsh(struct runtime_zsh *runtime, FILE *stream,
			  const char *source, bool interactive)
{
	char line[1024];
	unsigned int line_number = 0;
	int overall = 0;

	while (true) {
		char *tokens[GPIOCTL_CLI_MAX_TOKENS];
		int token_count, ret;

		if (interactive && runtime->options.timeout_set) {
			struct pollfd input = {
				.fd = fileno(stream),
				.events = POLLIN,
			};
			uint32_t allowed_ms;
			int poll_ret;

			if (cap_duration_zsh(&runtime->options,
					     GPIOCTL_CLI_MAX_DURATION_MS,
					     &allowed_ms)) {
				overall = -1;
				break;
			}
			poll_ret = poll(&input, 1, (int)allowed_ms);
			if (poll_ret <= 0) {
				if (!poll_ret)
					errno = ETIMEDOUT;
				print_error_zsh(&runtime->options, "shell", "stdin");
				overall = -1;
				break;
			}
		}
		if (interactive && !runtime->options.json) {
			fputs("gpioctl_zsh> ", stdout);
			fflush(stdout);
		}
		if (!fgets(line, sizeof(line), stream))
			break;
		line_number++;
		if (!strchr(line, '\n') && !feof(stream)) {
			errno = E2BIG;
			if (runtime->options.json) {
				fputs("{\"ok\":false,\"type\":\"script-error\","
				      "\"source\":", stderr);
				json_string_zsh(stderr, source);
				fprintf(stderr, ",\"line\":%u,\"errno\":%d,"
					"\"error\":", line_number, errno);
				json_string_zsh(stderr, "line too long");
				fputs("}\n", stderr);
			} else
				fprintf(stderr, "%s:%u: line too long\n", source,
					line_number);
			overall = -1;
			if (runtime->options.strict)
				break;
			continue;
		}
		token_count = tokenize_zsh(line, tokens, GPIOCTL_CLI_MAX_TOKENS);
		if (token_count < 0) {
			if (runtime->options.json) {
				fputs("{\"ok\":false,\"type\":\"script-error\","
				      "\"source\":", stderr);
				json_string_zsh(stderr, source);
				fprintf(stderr, ",\"line\":%u,\"errno\":%d,"
					"\"error\":", line_number, errno);
				json_string_zsh(stderr, strerror(errno));
				fputs("}\n", stderr);
			} else
				fprintf(stderr, "%s:%u: tokenize failed: %s\n", source,
					line_number, strerror(errno));
			overall = -1;
			if (runtime->options.strict)
				break;
			continue;
		}
		if (!token_count)
			continue;
		if (!strcmp(tokens[0], "exit") || !strcmp(tokens[0], "quit")) {
			if (runtime->options.json)
				puts("{\"ok\":true,\"operation\":\"quit\"}");
			break;
		}
		if (!strcmp(tokens[0], "help")) {
			if (runtime->options.json)
				puts("{\"ok\":true,\"operation\":\"help\","
				     "\"commands\":[\"list\",\"resolve\",\"info\","
				     "\"get\",\"set\",\"blink\",\"pair-blink\","
				     "\"batch-set\",\"transaction\",\"tx-line\","
				     "\"commit\",\"abort\",\"watch\",\"iopad-get\","
				     "\"iopad\",\"stats\",\"acquire\",\"value\","
				     "\"release\",\"sleep\"]}");
			else
				usage_zsh(stdout);
			continue;
		}
		ret = execute_tokens_zsh(runtime, token_count, tokens);
		if (ret) {
			if (runtime->options.json) {
				fputs("{\"ok\":false,\"type\":\"script-error\","
				      "\"source\":", stderr);
				json_string_zsh(stderr, source);
				fprintf(stderr, ",\"line\":%u,\"errno\":%d,"
					"\"error\":", line_number, errno);
				json_string_zsh(stderr, strerror(errno));
				fputs("}\n", stderr);
			}
			else
				fprintf(stderr, "%s:%u: command failed: %s\n",
					source, line_number, strerror(errno));
			overall = -1;
			if (runtime->options.strict)
				break;
		}
	}
	if (runtime->transaction.active) {
		memset(&runtime->transaction, 0, sizeof(runtime->transaction));
		if (!overall) {
			errno = ECANCELED;
			if (runtime->options.json) {
				fputs("{\"ok\":false,\"type\":\"transaction-abort\","
				      "\"source\":", stderr);
				json_string_zsh(stderr, source);
				fprintf(stderr, ",\"line\":%u,\"errno\":%d,"
					"\"error\":", line_number, errno);
				json_string_zsh(stderr, strerror(errno));
				fputs("}\n", stderr);
			}
			else
				fprintf(stderr,
					"%s:%u: uncommitted transaction aborted: %s\n",
					source, line_number, strerror(errno));
			overall = -1;
		}
	}
	return overall;
}

static int self_test_zsh(void)
{
	struct target_zsh target;
	struct cli_options_zsh options = {
		.config_path = !access("config/phytium-pi-v1.conf", R_OK) ?
			"config/phytium-pi-v1.conf" : "../config/phytium-pi-v1.conf",
	};
	uint32_t value;

	if (parse_u32_zsh("4294967295", &value) || value != UINT32_MAX)
		return 1;
	if (!parse_u32_zsh("4294967296", &value))
		return 1;
	if (parse_generic_gpio_name_zsh("GPIO4_7", &target) ||
	    strcmp(target.device, "/dev/gpio4_zsh") || target.offset != 7U)
		return 1;
	if (resolve_target_zsh(&options, "GPIO1_11", &target) ||
	    strcmp(target.pad, "BA49") || target.offset != 11U)
		return 1;
	puts("gpioctl_zsh self-test: PASS");
	return 0;
}

int main(int argc, char **argv)
{
	struct runtime_zsh runtime = {0};
	int index = 1;
	int ret;

	if (argc == 2 && !strcmp(argv[1], "--self-test"))
		return self_test_zsh();
	while (index < argc && !strncmp(argv[index], "--", 2)) {
		if (!strcmp(argv[index], "--json"))
			runtime.options.json = true;
		else if (!strcmp(argv[index], "--strict"))
			runtime.options.strict = true;
		else if (!strcmp(argv[index], "--dry-run"))
			runtime.options.dry_run = true;
		else if (!strcmp(argv[index], "--timeout") && index + 1 < argc) {
			uint32_t timeout_ms;

			if (runtime.options.timeout_set ||
			    parse_u32_zsh(argv[++index], &timeout_ms) || !timeout_ms ||
			    timeout_ms > GPIOCTL_CLI_MAX_DURATION_MS) {
				usage_zsh(stderr);
				return 2;
			}
			runtime.options.timeout_set = true;
			runtime.options.timeout_ms = timeout_ms;
		}
		else if (!strcmp(argv[index], "--config") && index + 1 < argc)
			runtime.options.config_path = argv[++index];
		else {
			usage_zsh(stderr);
			return 2;
		}
		index++;
	}
	if (index >= argc) {
		usage_zsh(stderr);
		return 2;
	}
	if (runtime.options.timeout_set) {
		uint64_t now_ns;

		if (monotonic_ns_zsh(&now_ns)) {
			perror("clock_gettime");
			return 1;
		}
		runtime.options.deadline_ns = now_ns +
			(uint64_t)runtime.options.timeout_ms * 1000000ULL;
	}
	if (!strcmp(argv[index], "run") && index + 1 == argc - 1) {
		FILE *stream = !strcmp(argv[index + 1], "-") ? stdin :
			fopen(argv[index + 1], "r");

		if (!stream) {
			print_error_zsh(&runtime.options, "open-script",
					argv[index + 1]);
			return 1;
		}
		ret = run_stream_zsh(&runtime, stream, argv[index + 1], false);
		if (stream != stdin)
			fclose(stream);
	} else if (!strcmp(argv[index], "shell") && index + 1 == argc) {
		ret = run_stream_zsh(&runtime, stdin, "stdin", true);
	} else {
		ret = execute_tokens_zsh(&runtime, argc - index, &argv[index]);
		if (ret && !error_reported_zsh)
			print_error_zsh(&runtime.options, "command", argv[index]);
	}
	cleanup_runtime_zsh(&runtime);
	return ret ? 1 : 0;
}

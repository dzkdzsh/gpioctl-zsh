CC ?= cc
AR ?= ar
BUILD_DIR ?= ../build/userspace
CPPFLAGS += -I../include
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Werror -Wformat=2 -Wconversion \
	-Wshadow -Wstrict-prototypes -fstack-protector-strong \
	-D_FORTIFY_SOURCE=2
LDFLAGS += -Wl,-z,relro,-z,now

LIB_OBJ := $(BUILD_DIR)/gpioctl_zsh_lib.o
CLI_OBJ := $(BUILD_DIR)/gpioctl_zsh_cli.o
LIB_STATIC := $(BUILD_DIR)/libgpioctl_zsh.a
CLI := $(BUILD_DIR)/gpioctl_zsh
LAYOUT_TEST := $(BUILD_DIR)/uapi_layout_zsh
EVENT_EPOLL_TEST := $(BUILD_DIR)/event_epoll_zsh
POLICY_PROBE_TEST := $(BUILD_DIR)/policy_probe_zsh
LOGIC_TEST := $(BUILD_DIR)/logic_zsh
UAPI_INVALID_TEST := $(BUILD_DIR)/uapi_invalid_zsh
BATCH_ROLLBACK_TEST := $(BUILD_DIR)/batch_rollback_zsh
BENCHMARK := $(BUILD_DIR)/gpioctl_benchmark_zsh
LIBGPIOD_BASELINE := $(BUILD_DIR)/libgpiod_baseline_zsh
EVENT_BENCHMARK := $(BUILD_DIR)/event_benchmark_zsh
LIBGPIOD_CFLAGS ?= $(shell pkg-config --cflags libgpiod 2>/dev/null)
LIBGPIOD_LIBS ?= $(shell pkg-config --libs libgpiod 2>/dev/null)

.PHONY: all benchmark benchmark-libgpiod clean check

all: $(CLI) $(LAYOUT_TEST) $(EVENT_EPOLL_TEST) $(POLICY_PROBE_TEST) \
	$(LOGIC_TEST) $(UAPI_INVALID_TEST) $(BATCH_ROLLBACK_TEST)

$(BUILD_DIR):
	mkdir -p $@

$(LIB_OBJ): libgpioctl_zsh/gpioctl_zsh.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(CLI_OBJ): gpioctl_zsh/main.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB_STATIC): $(LIB_OBJ)
	$(AR) rcs $@ $^

$(CLI): $(CLI_OBJ) $(LIB_STATIC)
	$(CC) $(LDFLAGS) $^ -o $@

$(LAYOUT_TEST): tests/uapi_layout_zsh.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(EVENT_EPOLL_TEST): tests/event_epoll_zsh.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) -o $@

$(POLICY_PROBE_TEST): tests/policy_probe_zsh.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) -o $@

$(LOGIC_TEST): tests/logic_zsh.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(UAPI_INVALID_TEST): tests/uapi_invalid_zsh.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BATCH_ROLLBACK_TEST): tests/batch_rollback_zsh.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BENCHMARK): ../benchmarks/gpioctl_benchmark_zsh.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) -o $@

$(EVENT_BENCHMARK): ../benchmarks/event_benchmark_zsh.c $(LIB_STATIC) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_STATIC) $(LDFLAGS) -o $@

benchmark: $(BENCHMARK) $(EVENT_BENCHMARK)

$(LIBGPIOD_BASELINE): ../benchmarks/libgpiod_baseline_zsh.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIBGPIOD_CFLAGS) $< \
		$(LDFLAGS) $(LIBGPIOD_LIBS) -o $@

benchmark-libgpiod: $(LIBGPIOD_BASELINE)

check: all
	$(LAYOUT_TEST)
	$(LOGIC_TEST)
	$(CLI) --self-test

clean:
	$(RM) -r $(BUILD_DIR)

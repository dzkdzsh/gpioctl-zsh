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

.PHONY: all clean check

all: $(CLI) $(LAYOUT_TEST) $(EVENT_EPOLL_TEST)

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

check: all
	$(LAYOUT_TEST)
	$(CLI) --self-test

clean:
	$(RM) -r $(BUILD_DIR)

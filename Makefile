KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR ?= $(CURDIR)/build

.PHONY: all kernel userspace dts clean check install uninstall

all: kernel userspace dts

kernel:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel modules
	@mkdir -p $(BUILD_DIR)/kernel
	@cp kernel/*.ko $(BUILD_DIR)/kernel/

userspace:
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace

dts:
	$(MAKE) -C dts BUILD_DIR=$(BUILD_DIR)/dts

check:
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace check

install: all
	./scripts/install_zsh.sh

uninstall:
	./scripts/uninstall_zsh.sh

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel clean
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace clean
	$(MAKE) -C dts BUILD_DIR=$(BUILD_DIR)/dts clean
	$(RM) -r $(BUILD_DIR)

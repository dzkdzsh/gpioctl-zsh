KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR ?= $(CURDIR)/build

.PHONY: all kernel userspace clean check

all: kernel userspace

kernel:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel modules
	@mkdir -p $(BUILD_DIR)/kernel
	@cp kernel/*.ko $(BUILD_DIR)/kernel/

userspace:
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace

check:
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace check

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel clean
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace clean
	$(RM) -r $(BUILD_DIR)


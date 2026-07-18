KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR ?= $(CURDIR)/build

.PHONY: all kernel userspace dts benchmark benchmark-libgpiod clean check kunit static-analysis audit install uninstall

all: kernel userspace dts

kernel:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel modules
	@mkdir -p $(BUILD_DIR)/kernel
	@cp kernel/*.ko $(BUILD_DIR)/kernel/

userspace:
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace

benchmark:
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace benchmark

benchmark-libgpiod:
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace benchmark-libgpiod

dts:
	$(MAKE) -C dts BUILD_DIR=$(BUILD_DIR)/dts

check:
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace check

kunit:
	./scripts/run_kunit_zsh.sh

static-analysis:
	./scripts/static_analysis_zsh.sh

audit:
	./scripts/release_audit_zsh.sh

install: all
	./scripts/install_zsh.sh

uninstall:
	./scripts/uninstall_zsh.sh

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR)/kernel clean
	$(MAKE) -C userspace BUILD_DIR=$(BUILD_DIR)/userspace clean
	$(MAKE) -C dts BUILD_DIR=$(BUILD_DIR)/dts clean
	$(RM) -r $(BUILD_DIR)

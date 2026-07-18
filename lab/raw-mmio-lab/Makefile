obj-m += gpioctl_raw_lab_zsh.o

.PHONY: all clean dts

KDIR ?= /lib/modules/$(shell uname -r)/build
BUILD_DIR ?= ../../build/raw-mmio-lab

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

dts:
	mkdir -p $(BUILD_DIR)
	dtc -@ -Wno-reg_format -Wno-avoid_default_addr_size \
		-I dts -O dtb -o $(BUILD_DIR)/conflict-probe-zsh.dtbo \
		conflict-probe-zsh.dts
	dtc -@ -Wno-reg_format -Wno-avoid_default_addr_size \
		-I dts -O dtb -o $(BUILD_DIR)/isolated-gpio4-zsh.dtbo \
		isolated-gpio4-zsh.dts

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean
	$(RM) -r $(BUILD_DIR)

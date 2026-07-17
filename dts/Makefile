DTC ?= dtc
BUILD_DIR ?= ../build/dts

.PHONY: all clean

all: $(BUILD_DIR)/phytium-pi-gpioctl-zsh.dtbo

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/%.dtbo: %.dts | $(BUILD_DIR)
	$(DTC) -@ -I dts -O dtb -o $@ $<

clean:
	$(RM) -r $(BUILD_DIR)


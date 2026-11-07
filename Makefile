DOCKER ?= docker
IMAGE ?= hp21xx-firmware:1.1
# STM32 core 2.11.0's STM32Tools package has no Linux ARM64 release.
export DOCKER_DEFAULT_PLATFORM := linux/amd64
VERSION := 1.1
FQBN := STMicroelectronics:stm32:GenF1:pnum=BLUEPILL_F103C8,opt=osstd,rtlib=nano,usb=none,xserial=generic,xusb=FS,dbg=none,upload_method=OpenOCDSTLink
SDFAT_FLAGS := -DSDFAT_FILE_TYPE=1 -DFAT12_SUPPORT=1 -DUSE_UTF8_LONG_NAMES=1 -DUSE_BLOCK_DEVICE_INTERFACE=1
RUN = $(DOCKER) run --rm --user "$$(id -u):$$(id -g)" -e HOME=/tmp -v "$(CURDIR):/workspace" -w /workspace $(IMAGE)
FIRMWARE := build/firmware/hp21xx_papertape_emulator.ino.bin
RELEASE := binaries/hp21xx_papertape_emulator.ino.v$(VERSION).bin
REFERENCE := binaries/hp21xx_papertape_emulator.ino.v1.00.bin

.PHONY: all setup image firmware release compare probe program bridge
all: firmware

setup: build/docker-image.stamp
image:
	$(DOCKER) build -t $(IMAGE) .

build/docker-image.stamp: Dockerfile scripts/arduino_setup.py .dockerignore
	$(MAKE) image
	@mkdir -p build
	@touch $@

firmware: build/docker-image.stamp
	@mkdir -p build/docker-compile build/firmware
	$(RUN) arduino-cli --config-file /opt/arduino/arduino-cli.yaml compile --fqbn '$(FQBN)' --build-property 'compiler.c.extra_flags=$(SDFAT_FLAGS)' --build-property 'compiler.cpp.extra_flags=$(SDFAT_FLAGS)' --build-path /workspace/build/docker-compile --output-dir /workspace/build/firmware src/hp21xx_papertape_emulator

release: firmware
	cp "$(FIRMWARE)" "$(RELEASE)"
	chmod 644 "$(RELEASE)"
	$(RUN) sha256sum "$(RELEASE)"

compare: firmware
	$(RUN) sha256sum "$(REFERENCE)" "$(FIRMWARE)"
	$(RUN) cmp "$(REFERENCE)" "$(FIRMWARE)"

# macOS needs a native USB bridge; Linux uses direct USB.
# PROGRAM_HOST selects an existing remote OpenOCD bridge.
probe: build/docker-image.stamp
	DOCKER="$(DOCKER)" IMAGE="$(IMAGE)" sh scripts/stlink.sh probe

program: firmware
	DOCKER="$(DOCKER)" IMAGE="$(IMAGE)" sh scripts/stlink.sh program

bridge:
	sh scripts/stlink.sh bridge

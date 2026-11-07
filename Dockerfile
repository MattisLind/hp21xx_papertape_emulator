FROM debian:bookworm-slim
ARG TARGETARCH
RUN test "$TARGETARCH" = amd64 || { echo 'Build with --platform linux/amd64 (STM32Tools requires it).' >&2; exit 1; }
ARG ARDUINO_CLI_VERSION=1.1.1
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl python3 make openocd gdb-multiarch \
    && rm -rf /var/lib/apt/lists/*
RUN case "$TARGETARCH" in arm64) cli_arch=ARM64 ;; amd64) cli_arch=64bit ;; *) exit 1 ;; esac \
    && curl -fL "https://downloads.arduino.cc/arduino-cli/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_${cli_arch}.tar.gz" \
       | tar -xz -C /usr/local/bin arduino-cli
ENV ARDUINO_ENV_ROOT=/opt/arduino
COPY scripts/arduino_setup.py /opt/setup/arduino_setup.py
RUN python3 /opt/setup/arduino_setup.py /usr/local/bin/arduino-cli
WORKDIR /workspace

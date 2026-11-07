# Build and programming scripts

The three scripts below are source files required by the project's Docker and
Make workflow. Track them in Git together with this README, the root Makefile,
Dockerfile, and `.dockerignore`.

| File | Purpose | Used by |
| --- | --- | --- |
| [arduino_setup.py](arduino_setup.py) | Installs STM32 core 2.11.0 and the pinned firmware libraries, including the separate TinyUSB STM32 port. Writes an isolated Arduino CLI configuration and downloads its dependencies. | The Dockerfile, when `make setup` builds the image. |
| [stlink.sh](stlink.sh) | Detects and programs the STM32 through ST-Link. On macOS, downloads a checksum-verified OpenOCD USB bridge into `build/stlink/` and connects the container's debugger to it. On Linux, accesses USB from the container directly. Also supports an existing bridge through `PROGRAM_HOST`. | `make probe`, `make program`, and `make bridge`. |
| [program.gdb](program.gdb) | Connects to OpenOCD, halts and resets the target, saves its existing 64 KiB flash to a timestamped backup, loads the firmware ELF, verifies the loaded sections, and resets the target to run. A verification failure stops the script. | GDB inside the container, launched by `stlink.sh` for `make program`. |

Use the Make targets from the repository root rather than invoking these scripts
directly. See the root [README](../README.md#building-and-programming-with-docker-and-make)
for the build and programming commands.

## Dependency setup details

`arduino_setup.py` takes the Arduino CLI executable path as its argument.
`ARDUINO_ENV_ROOT` selects the dependency directory: the Dockerfile sets it to
`/opt/arduino`; otherwise it defaults to `build/arduino/` in the repository.
It also retains support for building Arduino's prototype generator natively
when run outside Docker on an Apple Silicon Mac. The normal Docker workflow
does not use that fallback.

## Generated files

Do not track `__pycache__/` or `.pyc` files: Python generates these caches, and
the root `.gitignore` already excludes them. Downloaded tools, dependency
archives, build outputs, flash backups, and programming logs belong under
`build/`, which is also ignored. Release binaries deliberately saved in
`binaries/` are separate versioned artifacts.

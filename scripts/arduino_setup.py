#!/usr/bin/env python3
"""Install pinned Arduino dependencies inside build/, without changing the IDE."""
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("ARDUINO_ENV_ROOT", ROOT / "build" / "arduino"))
PORT_COMMIT = "d722701738f48234fd9fb6a4110e7885ef2393da"
SDFAT_COMMIT = "cda057318bec196183d4cc92b01bc1dd64bbfb02"  # upstream 2.3.1
LIBRARIES = {
    "U8g2": "2.36.19",
    "Adafruit TinyUSB Library": "3.7.7",
}


def install_archive(repository, commit, subdirectory, destination):
    archive = BUILD / "downloads" / f"{repository.split('/')[-1]}-{commit}.zip"
    if not archive.exists():
        subprocess.run(["curl", "-L", "--fail", "--silent", "--show-error",
                        f"https://codeload.github.com/{repository}/zip/{commit}",
                        "-o", str(archive)], check=True)
    prefix = f"{repository.split('/')[-1]}-{commit}/{subdirectory}"
    with zipfile.ZipFile(archive) as z:
        files = [(entry, Path(entry.filename[len(prefix):])) for entry in z.infolist()
                 if entry.filename.startswith(prefix) and not entry.is_dir()]
        if not files or any(p.is_absolute() or ".." in p.parts for _, p in files):
            raise ValueError(f"Unexpected archive layout: {archive}")
        if destination.exists():
            shutil.rmtree(destination)
        for entry, relative in files:
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(z.read(entry))


def native_ctags():
    """Arduino's Intel-only ctags needs a native build on Apple Silicon."""
    if platform.system() != "Darwin" or platform.machine() != "arm64":
        return
    commit = "abc8fca7499f44c725122881cd380a88c37abe0e"  # 5.8-arduino11
    source = BUILD / "tools" / f"ctags-{commit}"
    executable = source / "ctags"
    if not executable.exists():
        archive = BUILD / "downloads/ctags-source.zip"
        if not archive.exists():
            subprocess.run(["curl", "-L", "--fail", "--silent", "--show-error",
                            f"https://codeload.github.com/arduino/ctags/zip/{commit}",
                            "-o", str(archive)], check=True)
        with zipfile.ZipFile(archive) as z:
            z.extractall(BUILD / "tools")
        # The old macro collides with __unused__ in modern macOS SDK headers.
        for path in list(source.glob("*.c")) + list(source.glob("*.h")):
            path.write_bytes(path.read_bytes().replace(b"__unused__", b"CTAGS_UNUSED"))
        subprocess.run(["bash", "configure", "CFLAGS=-O2 -Wno-error=implicit-function-declaration"],
                       cwd=source, check=True)
        subprocess.run(["make", "-j8"], cwd=source, check=True)
    destination = BUILD / "data/packages/builtin/tools/ctags/5.8-arduino11/ctags"
    shutil.copy2(executable, destination)


def main():
    for name in ("data", "downloads", "user/libraries"):
        (BUILD / name).mkdir(parents=True, exist_ok=True)
    config = BUILD / "arduino-cli.yaml"
    config.write_text(
        "board_manager:\n  additional_urls:\n"
        "    - https://github.com/stm32duino/BoardManagerFiles/raw/main/package_stmicroelectronics_index.json\n"
        "directories:\n"
        + "".join(f"  {key}: {json.dumps(str(BUILD / value))}\n" for key, value in
                  (("data", "data"), ("downloads", "downloads"), ("user", "user")))
    )
    cli = [sys.argv[1], "--config-file", str(config)]
    subprocess.run(cli + ["core", "update-index"], check=True)
    subprocess.run(cli + ["core", "install", "STMicroelectronics:stm32@2.11.0"], check=True)
    native_ctags()
    for name, version in LIBRARIES.items():
        subprocess.run(cli + ["lib", "install", "--no-deps", f"{name}@{version}"], check=True)

    install_archive("greiman/SdFat", SDFAT_COMMIT, "", BUILD / "user/libraries/SdFat")
    # Replace the bundled STM32 port as described in the port's README.
    install_archive("code-fiasco/TinyUSB-Arduino-STM32", PORT_COMMIT, "stm32/",
                    BUILD / "user/libraries/Adafruit_TinyUSB_Library/src/arduino/ports/stm32")
    print("Arduino dependencies ready. Run make.")


if __name__ == "__main__":
    main()

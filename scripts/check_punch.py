#!/usr/bin/env python3
"""Check both reader and punch data polarity settings."""

import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SKETCH = ROOT / "src/hp21xx_papertape_emulator/hp21xx_papertape_emulator.ino"


def sketch_function(source, name):
    match = re.search(
        rf"^static [^\n]+\b{re.escape(name)}\([^\n]*\) \{{\n.*?^\}}",
        source,
        re.MULTILINE | re.DOTALL,
    )
    if match is None:
        raise RuntimeError(f"Cannot find sketch function {name}")
    return match.group()


HARNESS = r"""
#include <cassert>
#include <cstdint>
#include <cstdio>
constexpr int HIGH=1, LOW=0;
constexpr uint8_t DATA_PINS[]={0,1,2,3,4,5,6,7};
static unsigned gpioByte;
static int digitalRead(uint8_t pin) { return (gpioByte >> pin) & 1; }
static void digitalWrite(uint8_t pin, int value) {
  gpioByte = (gpioByte & ~(1U << pin)) | (unsigned(value) << pin);
}
// SKETCH_FUNCTIONS
int main() {
  for (bool activeHigh : {false,true}) {
    for (unsigned byte=0; byte<256; ++byte) {
      gpioByte=activeHigh ? byte : (byte ^ 255);
      assert(readDataBus(activeHigh)==byte);
      writeDataBus(byte,activeHigh);
      assert(gpioByte==(activeHigh ? (byte ^ 255) : byte));
    }
  }
  std::puts("Reader and punch polarity checks passed: all 256 bytes, HIGH and LOW.");
}
"""


def main():
    source = SKETCH.read_text()
    function = sketch_function(source, "writeDataBus") + "\n" + sketch_function(source, "readDataBus")
    with tempfile.TemporaryDirectory(prefix="hp21xx-punch-") as temporary:
        directory = Path(temporary)
        cpp = directory / "check.cpp"
        binary = directory / "check"
        cpp.write_text("#include <initializer_list>\n" + HARNESS.replace("// SKETCH_FUNCTIONS", function))
        compiler = shlex.split(os.environ.get("CXX", "c++"))
        subprocess.run([*compiler, "-std=c++11", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()

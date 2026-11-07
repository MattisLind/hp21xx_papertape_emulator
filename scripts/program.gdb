set pagination off
set confirm off
set remotetimeout 30
python
import os
gdb.execute("target extended-remote " + os.environ["PROGRAM_HOST"] + ":3333")
end
monitor reset halt
# Retain the pre-update 64 KiB flash contents for recovery.
python
from datetime import datetime
backup = "build/stlink/flash-before-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f") + ".bin"
gdb.execute("dump binary memory " + backup + " 0x08000000 0x08010000")
gdb.write("Saved existing flash to " + backup + "\n")
end
load
python
result = gdb.execute("compare-sections", to_string=True)
gdb.write(result)
if "MIS-MATCHED" in result or "warning:" in result.lower():
    raise gdb.GdbError("Flash verification failed")
end
monitor reset run
disconnect
quit

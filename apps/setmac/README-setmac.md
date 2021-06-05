## SETMAC.SYSTEM

`SETMAC.SYSTEM` is a simple utility for setting the MAC address on an Uthernet-II card
in an Apple //e or GS.

It is intended to be placed in the top level directory of a ProDOS boot disk (ie: in
the "volume directory"). 

### Configuration File

`SETMAC.SYSTEM` looks for a configuration file called `SETMAC.CFG` in the directory
from which it was started.  If this file is not found, then `SETMAC.SYSTEM` will
fall back to default values of assuming an Uthernet-II in slot 5 and using the
MAC `00:08:0d:10:20:30`.  You will almost certainly want to change this.

`SETMAC.CFG` is an extremely simple text file, which you can create in `EDIT.SYSTEM`
or any ProDOS text editor that can save plain text files.  It consists of a single
digit representing the slot where the Uthernet-II is installed, followed by a
space, then the six byte MAC address in hex, with colons separating the octets.  A
sample `SETMAC.CFG` file for an Uthernet-II in slot 3 could look like this:

```
3 01:02:03:aa:bb:cc
```

The parser is very simple minded so please stick to the formatting of the file as
specified here, or you may have surprises!

If the config file does not exist or is unreadble, `SETMAC.SYSTEM` will display
a message explaining that it could not open the file so it is using default
parameters and will pause awaiting a keypress to acknowledge this status.

### Chaining `.SYSTEM` Files at Startup

When ProDOS starts, it will automatically run the first `.SYSTEM` file it finds in
the volume directory. This is often a file such as `BASIC.SYSTEM` but it can be
any valid `.SYSTEM` file.

Before `SETMAC.SYSTEM` terminates, it searches the volume directory to find the entry
matching itself, and then scans forward looking for a `.SYSTEM` file. If it finds
one it loads it into memory and starts it executing.

If for example, one had the following files in order in the ProDOS volume directory
on the boot volume:
```
PRODOS
SETMAC.SYSTEM
BASIC.SYSTEM
```
Then ProDOS would start `SETMAC.SYSTEM` on boot and `SETMAC.SYSTEM` would, in turn,
start `BASIC.SYSTEM`.  Longer chains are possible, with other compliant programs
such as the No Slot Clock Driver (`NS.CLOCK.SYSTEM`).

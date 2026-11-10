#!/usr/bin/env python3
"""
Fake Paradox PRT3 module (ASCII mode) on a pseudo-terminal.

The daemon opens LINK (a symlink to the pty slave) as if it were the real
serial adapter. The fake answers the requests the daemon issues and emits
panel events.

Side channels for the test runner:
  CMD_LOG  - every command received from the daemon, one per line.
  CTL_FIFO - write lines here to drive the panel:
               ZONE <n> O|C   open/close a zone (emits G001/G000 event)
               RAW <text>     send raw text (CR appended)
"""
import os
import select
import sys
import tty

LINK = "/tmp/prt3"
CMD_LOG = "/tmp/prt3_cmds.log"
CTL_FIFO = "/tmp/prt3_ctl"
CR = b"\r"

area_status = {1: "D"}  # D=disarmed, A=armed, S=stay armed
zone_status = {n: "C" for n in range(1, 97)}


def log(msg):
    print(f"[FAKE-PRT3] {msg}", flush=True)


def label(text):
    return text.ljust(16)[:16]


class Prt3:
    def __init__(self):
        self.master, self.slave = os.openpty()
        tty.setraw(self.slave)
        name = os.ttyname(self.slave)
        if os.path.lexists(LINK):
            os.unlink(LINK)
        os.symlink(name, LINK)
        log(f"serial at {LINK} -> {name}")

        open(CMD_LOG, "w").close()
        if not os.path.exists(CTL_FIFO):
            os.mkfifo(CTL_FIFO)
        # O_RDWR keeps the FIFO from returning EOF when writers go away.
        self.ctl = os.open(CTL_FIFO, os.O_RDWR | os.O_NONBLOCK)
        self.rx = b""
        self.ctl_buf = b""

    def send(self, text):
        os.write(self.master, text.encode() + CR)

    def handle(self, cmd):
        with open(CMD_LOG, "a") as f:
            f.write(cmd + "\n")
        log(f"<- {cmd}")

        kind = cmd[:2]
        num = int(cmd[2:5]) if len(cmd) >= 5 and cmd[2:5].isdigit() else 0

        if kind == "RA":
            self.send(f"RA{num:03d}{area_status.get(num, 'D')}OOOOOO")
        elif kind == "AL":
            self.send(f"AL{num:03d}{label(f'Area {num}')}")
        elif kind == "RZ":
            self.send(f"RZ{num:03d}{zone_status[num]}OOOO")
        elif kind == "ZL":
            self.send(f"ZL{num:03d}{label(f'Zone {num}')}")
        elif kind in ("AA", "AQ"):
            mode = cmd[5:6]
            self.send(f"{kind}{num:03d}&ok")
            if mode == "S":
                area_status[num] = "S"
                self.send(f"G012N004A{num:03d}")  # special arming -> stay
            else:
                area_status[num] = "A"
                self.send(f"G010N001A{num:03d}")  # arming with user code
        elif kind == "AD":
            area_status[num] = "D"
            self.send(f"AD{num:03d}&ok")
            self.send(f"G014N001A{num:03d}")
        elif kind == "UK":
            self.send(f"UK{num:03d}&ok")
        else:
            self.send(f"{cmd[:5]}&fail")

    def control(self, line):
        parts = line.split()
        if not parts:
            return
        if parts[0] == "ZONE":
            n, st = int(parts[1]), parts[2]
            zone_status[n] = st
            self.send(f"G{1 if st == 'O' else 0:03d}N{n:03d}A001")
        elif parts[0] == "RAW":
            self.send(line[4:])
        log(f"ctl: {line}")

    def run(self):
        while True:
            r, _, _ = select.select([self.master, self.ctl], [], [])
            if self.master in r:
                self.rx += os.read(self.master, 512)
                while CR in self.rx:
                    line, self.rx = self.rx.split(CR, 1)
                    if line:
                        self.handle(line.decode(errors="replace"))
            if self.ctl in r:
                try:
                    self.ctl_buf += os.read(self.ctl, 512)
                except BlockingIOError:
                    pass
                while b"\n" in self.ctl_buf:
                    line, self.ctl_buf = self.ctl_buf.split(b"\n", 1)
                    self.control(line.decode().strip())


if __name__ == "__main__":
    try:
        Prt3().run()
    except KeyboardInterrupt:
        sys.exit(0)

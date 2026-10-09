#!/usr/bin/env python3
"""Start a Pintos QEMU debug server owned by the VS Code debug session."""

import os
from pathlib import Path
import runpy
import signal
import socket
import subprocess
import sys
import time


def debug_port_is_listening():
    # Inspect the listener without occupying QEMU's GDB connection.
    with open("/proc/net/tcp", encoding="ascii") as connections:
        return any(
            fields[1] == "0100007F:04D2" and fields[3] == "0A"
            for fields in (line.split() for line in connections)
            if len(fields) > 3
        )


def stop_on_signal(signum, frame):
    raise SystemExit(0)


def main():
    root = Path(__file__).resolve().parent.parent
    build = root / "pintos" / "threads" / "build"
    test = sys.argv[1] if len(sys.argv) > 1 else "alarm-single"
    if not (build / "os.dsk").is_file():
        raise RuntimeError("Build Pintos before starting the debugger.")

    # Leave an already running debugger or QEMU session alone.
    with socket.socket() as probe:
        try:
            probe.bind(("127.0.0.1", 1234))
        except OSError as error:
            raise RuntimeError(
                "Debug port 1234 is in use. Stop the previous debug session first."
            ) from error

    signal.signal(signal.SIGTERM, stop_on_signal)
    signal.signal(signal.SIGINT, stop_on_signal)
    os.chdir(build)
    runner = runpy.run_path(str(root / "pintos" / "utils" / "pintos"))
    pintos = runner["Pintos"](gdb=True, args=["-q", "run", test])

    # Reuse this checkout's disk preparation and QEMU arguments unchanged.
    pintos.bdevs = pintos._Pintos__scan_dir()
    disk = pintos._Pintos__prepare_kernel_argument([], [])
    qemu = None
    try:
        pintos.bdevs["os"] = disk
        command = pintos._Pintos__prepare_cmd()
        index = command.index("-s")
        command[index:index + 1] = ["-gdb", "tcp:127.0.0.1:1234"]
        qemu = subprocess.Popen(command, stdin=subprocess.DEVNULL)
        deadline = time.monotonic() + 10
        while not debug_port_is_listening():
            if qemu.poll() is not None:
                raise RuntimeError("QEMU exited before its GDB server was ready.")
            if time.monotonic() >= deadline:
                raise RuntimeError("QEMU's GDB server did not start within 10 seconds.")
            time.sleep(0.05)
        print("PINTOS_QEMU_READY", flush=True)
        return qemu.wait()
    finally:
        if qemu is not None and qemu.poll() is None:
            qemu.terminate()
            try:
                qemu.wait(timeout=3)
            except subprocess.TimeoutExpired:
                qemu.kill()
                qemu.wait()
        Path(disk).unlink(missing_ok=True)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"Pintos debug startup failed: {error}", file=sys.stderr, flush=True)
        sys.exit(1)

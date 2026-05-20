#!/usr/bin/env python3
"""
shocker_serial.py — Python controller for ESP32 Numpad+Serial shocker

Requires: pip install pyserial

Usage:
  python shocker_serial.py [PORT] [--baud 115200]

  If PORT is omitted, lists available ports and prompts you to pick one.

  On Linux you may need:  sudo usermod -aG dialout $USER  (then log out/in)

Interactive commands:
  7 / 8 / 9        shock ch1 / ch2 / both (at current level)
  4 / 5            ch1 / ch2 level +1
  1 / 2            ch1 / ch2 level -1
  + / -            both channels level +1 / -1
  6 / 3            test ch1 / ch2 (vibrate + beep, no shock)
  0                reset both levels to 1
  shock1 [N]       shock ch1 at intensity N (or current level)
  shock2 [N]       shock ch2 at intensity N
  shockb [N]       shock both at intensity N
  level1 N         set ch1 level (1-99)
  level2 N         set ch2 level (1-99)
  status           show current ESP32 state
  help             show this help
  quit / exit      close connection
"""

import sys
import time
import threading

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("pyserial not found. Install it with:  pip install pyserial")
    sys.exit(1)


def find_port():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found. Make sure the ESP32 is plugged in.")
        return None
    if len(ports) == 1:
        print(f"Auto-selected: {ports[0].device}  ({ports[0].description})")
        return ports[0].device
    print("Available serial ports:")
    for i, p in enumerate(ports):
        print(f"  [{i}] {p.device}  —  {p.description}")
    while True:
        try:
            idx = int(input("Select port number: "))
            if 0 <= idx < len(ports):
                return ports[idx].device
        except (ValueError, KeyboardInterrupt):
            pass
        print("Invalid selection, try again.")


def reader_thread(ser, stop_event):
    """Background thread: print lines coming from the ESP32."""
    while not stop_event.is_set():
        try:
            line = ser.readline().decode("utf-8", errors="replace").rstrip()
            if line:
                print(f"\r  ESP32: {line}")
                print("> ", end="", flush=True)
        except serial.SerialException:
            break
        except Exception:
            break


def send(ser, cmd):
    ser.write((cmd + "\n").encode("utf-8"))


def parse_args():
    port = None
    baud = 115200
    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] in ("--baud", "-b") and i + 1 < len(args):
            try:
                baud = int(args[i + 1])
            except ValueError:
                pass
            i += 2
        elif not port and not args[i].startswith("-"):
            port = args[i]
            i += 1
        else:
            i += 1
    return port, baud


def main():
    port, baud = parse_args()

    if not port:
        port = find_port()
    if not port:
        sys.exit(1)

    print(f"Connecting to {port} at {baud} baud ...")
    try:
        ser = serial.Serial(port, baud, timeout=0.1)
    except serial.SerialException as e:
        print(f"Error opening {port}: {e}")
        sys.exit(1)

    # ESP32 resets when the serial port opens; wait for it to boot
    time.sleep(1.5)
    ser.reset_input_buffer()

    stop_event = threading.Event()
    t = threading.Thread(target=reader_thread, args=(ser, stop_event), daemon=True)
    t.start()

    print("Connected. Type 'help' for commands, 'quit' to exit.\n")

    try:
        while True:
            try:
                line = input("> ").strip()
            except EOFError:
                break

            if not line:
                continue

            lower = line.lower()

            if lower in ("quit", "exit", "q"):
                break

            if lower == "help":
                print(__doc__)
                continue

            # Normalize shorthand: + and - are fine as-is
            send(ser, line)
            time.sleep(0.05)

    except KeyboardInterrupt:
        print()

    stop_event.set()
    ser.close()
    print("Disconnected.")


if __name__ == "__main__":
    main()

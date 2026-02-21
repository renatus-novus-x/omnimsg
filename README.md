[![windows](https://github.com/renatus-novus-x/omnimsg/workflows/windows/badge.svg)](https://github.com/renatus-novus-x/omnimsg/actions?query=workflow%3Awindows)
[![macos](https://github.com/renatus-novus-x/omnimsg/workflows/macos/badge.svg)](https://github.com/renatus-novus-x/omnimsg/actions?query=workflow%3Amacos)
[![ubuntu](https://github.com/renatus-novus-x/omnimsg/workflows/ubuntu/badge.svg)](https://github.com/renatus-novus-x/omnimsg/actions?query=workflow%3Aubuntu)

omnimsg_min
===========

Omni Messenger (omnimsg): a minimal, serverless, cross-platform LAN messenger.

Stage 1 (this repo):
  - text chat only (UDP broadcast within the same subnet)
  - no file transfer yet
  - no server required
  - builds on:
      * Windows
      * macOS / Linux
      * X68000 (elf2x68k + libsocket, Human68k)

Build
-----
Windows / macOS / Linux:
  cmake -S . -B build
  cmake --build build

X68000 (elf2x68k):
  make -f Makefile.x68k

Usage
-----
Interactive chat:
  omnimsg --nick Alice --broadcast 192.168.0.255
  omnimsg --nick Bob   --broadcast 192.168.0.255

Send one message and exit:
  omnimsg --nick Alice --broadcast 192.168.0.255 --send "hello"

Options:
  --nick <name>        nickname (default: anon)
  --port <port>        UDP port (default: 24250)
  --broadcast <ip>     broadcast address (default: 255.255.255.255)
  --send <text>        send one message and exit
  --help               show help

Notes (X68000)
--------------
- poll() is planned but not yet available. Receive is implemented as non-blocking by
  checking how many bytes are waiting in the receive buffer (SO_SOCKLENRECV).
- Interactive input is currently blocking on X68000, so incoming messages are printed
  after you press Enter. Once poll() exists, stdin+socket can be multiplexed cleanly.
- Sleeping uses IOCS _ONTIME via TRAP #15 to obtain a 1/100-second tick, then busy-waits.

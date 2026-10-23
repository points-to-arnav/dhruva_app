# Design

The design of the Dhruva Jaal LoRa mesh: the firmware ([../firmware/firmware.ino](../firmware/firmware.ino)) for the dongle, the relays (balloon or ground nodes) and the Helper. The phone web app in this repo is the user interface on top of it.

| Document | What's in it |
| --- | --- |
| [plan.md](plan.md) | Goal, architecture, hardware and wiring, toolchain, firmware layout, USB command line and logs, milestones, demo, success criteria, decisions |
| [packet-design.md](packet-design.md) | Every LoRa packet byte by byte (rev 4), radio settings, airtime |
| [algorithm-design.md](algorithm-design.md) | One-channel listen-before-talk access, neighbour discovery, geographic routing, receive and transmit algorithms, storage, failure behaviour |
| [../docs/usb-protocol.md](../docs/usb-protocol.md) | Phone ↔ dongle USB text protocol |

Read them in that order: the plan for the big picture, the packet design for the formats, then the algorithm.

# Firmware

One Arduino sketch, [firmware.ino](firmware.ino), for every Dhruva Jaal module: the dongle, the relays (balloon or ground nodes) and the Helper. It implements the design in [../design/](../design/): the [packet formats](../design/packet-design.md), the [algorithm](../design/algorithm-design.md) (one channel with listen-before-talk, DATA as JSON, geographic routing, store-and-forward, delivery confirmation) and the [plan](../design/plan.md) (wiring, demo).

## Build

1. Open `firmware/firmware.ino` in the Arduino IDE.
2. Edit the top of the file:
   - **Section 1**, which module this is. Set exactly one to 1:
     ```c
     #define IS_DONGLE 0   // plugged into the phone
     #define IS_RELAY  1   // balloon or ground node (NEO-6M on GPIO20)
     #define IS_HELPER 0   // rescue base, prints messages to the PC
     ```
   - **Section 2**, this module's `MY_ID` (unique per board) and position. A relay also sets `RELAY_IS_BALLOON` (1 = balloon, 0 = ground).
   - **Section 3**, the network: `HELPER_ID` and its position, `DEMO_RANGE_KM`, `TX_POWER`, `HELLO_INTERVAL_MS`. Keep these the same on every module.
3. Set the Tools menu:
   - Board: **ESP32C3 Dev Module** (esp32 by Espressif 3.x)
   - USB CDC On Boot: **Enabled**
4. Install the libraries **LoRa** by Sandeep Mistry and **ArduinoJson** by Benoit Blanchon (v7). `SPI` and `Preferences` come with the esp32 core.
5. Upload.

The defaults are the demo setup in [plan.md section 8](../design/plan.md#8-demo): Helper `0x0001`, relay `0x0011`, dongle `0x2001`, 4 km apart in a line, range filter off.

## Using it

Open the serial monitor at 115200 with the newline line ending.

| Type | What happens |
| --- | --- |
| `status` | neighbours and stored messages |
| `send 2 03` | a test message from this dongle or relay: 2 people, flags `0x03` (trapped, injured) |
| `erase` | clears stored messages and seen keys (the message counter is kept) |
| `{"type":"send","people":2,"flags":3,"lat":27.68,"lon":85.32,"alt":1300,"acc":10,"time":1761217205}` | dongle only: exactly what the website sends |
| `{"type":"info_request"}` | dongle only: answers `{"type":"info","battery":null,"nodes":<nodes in range>}` |

What it prints:

- `# ...`: readable status lines.
- `@{...}`: one JSON event per line (`data_rx`, `ack_rx`, `retry`, `waiting`, `delivered`, `nbr`, `msg_state`, ...) for the dashboard and the simulation.
- Helper: a `DATA RECEIVED` block for every new message.
- Dongle: JSON `{"type":"status",...}` and `{"type":"info",...}` lines for the phone app ([docs/usb-protocol.md](../docs/usb-protocol.md)). The app ignores everything else on the port.

## Quick test without the phone

1. Flash a Helper and a dongle (or a relay), and keep them at least 1 m apart.
2. Wait until the dongle's log shows the Helper as a neighbour (`"ev":"nbr"`), within about 12 s.
3. On the dongle, type `send 1 01`. The Helper prints the DATA block, and the dongle logs `DELIVERED`.

A relay forwards only through ACTIVE neighbours, which takes 3 HELLOs (about 30 s) after power-on.

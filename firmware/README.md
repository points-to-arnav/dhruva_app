# Firmware

One Arduino sketch for every Dhruva Jaal module: the dongle, the relays (balloon or ground nodes) and the Helper. It implements the design in [../design/](../design/): the [packet formats](../design/packet-design.md), the [algorithm](../design/algorithm-design.md) (hybrid TDM + FDM channel access, time sync, geographic routing, store-and-forward) and the [plan](../design/plan.md) (wiring, USB commands, demo).

## Build

1. Open `firmware/firmware.ino` in the Arduino IDE.
2. Pick the module in `role.h`, with exactly one flag set to 1:
   ```c
   #define IS_DONGLE 0   // plugged into the phone
   #define IS_RELAY  1   // balloon or ground node (NEO-6M on GPIO20)
   #define IS_HELPER 0   // rescue base, prints messages to the PC
   ```
3. Set the Tools menu:
   - Board: **ESP32C3 Dev Module** (esp32 by Espressif 3.x)
   - USB CDC On Boot: **Enabled**
   - Flash Size: 4 MB
4. Install the library **LoRa** by Sandeep Mistry. `SPI`, `Preferences` and the flash partition API come with the esp32 core.
5. Upload. `partitions.csv` in this folder adds the 256 KB `mesh` partition where messages are stored; the core uses it automatically.

The wiring is in `pins.h` and in [plan.md section 3](../design/plan.md#3-hardware).

## First setup over USB

Open the serial monitor (any baud, newline line ending) and type `help`. Settings are kept in flash after `save`.

```
show cfg
set id 0x0011                         # unique per module; after save + reboot
set role balloon                      # relays: balloon or ground
set helper 0x0001 27.752000 85.320000 # the Helper's ID and position, the same on every module
set pos 27.716000 85.320000 1300      # fixed position (Helper, dongle) / fallback until a GPS fix (relay)
set txp 3                             # dBm; 2-5 on a table
save
reboot
```

The defaults are the demo setup in [plan.md section 8](../design/plan.md#8-demo): Helper `0x0001`, relay `0x0011`, dongle `0x2001`, 4 km apart in a line, range filter off. Every module must use the same `sf_slots` and `ctrl_slots`.

Other commands: `show nbr`, `show msgs`, `show stats`, `show sync`, `set range <km>`, `set txlat <us>`, `erase store`, and `send <people> <flags_hex>` to inject a test message from a dongle or relay.

## What it prints

- `# ...`: readable status lines.
- `@{...}`: one JSON event per line (`data_rx`, `ack_rx`, `retry`, `waiting`, `delivered`, `sync`, `nbr`, `stats`, ...) for the dashboard and the simulation.
- Helper: a `DATA RECEIVED` block for every new message.
- Dongle: `STATUS|...` and `INFO|...` lines for the phone app ([docs/usb-protocol.md](../docs/usb-protocol.md)). The app ignores everything else on the port.

## Files

| File | What it does |
| --- | --- |
| `firmware.ino` | `setup()` and the main loop |
| `role.h`, `pins.h` | module flags, wiring |
| `mesh_config.*` | config in NVS, own message counter |
| `cli.*` | USB command line; on the dongle it passes phone lines to `phone.*` |
| `radio.*` | SX1278 through the LoRa library: DIO0 interrupt, channel switching, CAD |
| `packet.*` | packet encode / decode, CRC8, airtime |
| `timesync.*` | superframe clock from the Helper's HELLOs |
| `mac.*` | control / data / guard windows, one exchange per pair, retries and backoff |
| `neighbor.*` | neighbour table, HEARD / ACTIVE / STALE, forwarding set of 4 |
| `routing.*` | next-hop choice (geographic progress, ACK rate, SNR) |
| `txq.*` | transmit jobs |
| `store.*` | message log and seen set in flash, store before ACK |
| `mesh.*` | receive dispatch, DATA / DELIVERY handling, own messages |
| `phone.*` | dongle: phone protocol |
| `gps.*` | relay: NEO-6M reader |
| `report.*` | Helper: DATA block for the PC |
| `geo.*`, `timeutil.*`, `mesh_log.*` | distances, UTC time, logs |

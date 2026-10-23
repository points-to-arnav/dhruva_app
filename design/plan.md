# Dhruva Jaal: V1 plan

A modular LoRa disaster-relief mesh. A trapped person needs only a **phone + a USB LoRa dongle**: they pick what they need, press SEND, and the mesh of balloon and ground nodes carries the message hop by hop to a **Helper** (the rescue base), which confirms delivery all the way back.

Companion documents:
- [packet-design.md](packet-design.md): on-air packet formats (rev 4)
- [algorithm-design.md](algorithm-design.md): channel access, neighbours, routing, retries, storage
- [../docs/usb-protocol.md](../docs/usb-protocol.md): phone ↔ dongle USB protocol (already implemented by the web app in this repo)

## 1. Goal

Build battery-powered LoRa nodes that **reliably** deliver an emergency message to a Helper through a multi-hop network. V1 is not about maximum range. Reliability comes first.

The network must:
- discover nearby nodes automatically
- know the approximate position of its neighbours and of the Helper
- forward toward the Helper geographically
- store each message before acknowledging it, and before sending it on
- retry failed transmissions
- prevent duplicate messages
- survive individual node failures
- prefer reliability over latency

Priority: **reliability > link stability > network capacity > latency.**

## 2. System architecture

```
 TRAPPED PERSON
       |
     PHONE  (web app: user interface only, never part of the mesh)
       | USB-C
     DONGLE (LoRa edge node: originates DATA, never relays)
       ~ LoRa
  BALLOON / GROUND NODES  (infra: HELLO, neighbour table, store-and-forward)
       ~ LoRa  (up to 10 hops, ~5 km each)
     HELPER (destination; sends DELIVERY back along the path)
       | USB
     LAPTOP (readable message block + JSON log)
```

| Layer | Is | Handles |
| --- | --- | --- |
| Phone | user interface | USB connection, composing the SOS (people, needs, position), showing status |
| Dongle | LoRa edge node | turns `SEND` into DATA, stores it, finds a node, retries, reports status |
| Mesh | reliable transport | discovery, geographic forwarding, ACK / retry, dedup, TTL, DELIVERY |
| Helper | destination | receives, de-duplicates, prints to the PC, confirms delivery |

The phone handles no discovery, routing, retries, TTL or LoRa config. That all lives in the firmware. Since the phone is only the interface, other edge devices can use the same mesh later.

**Deployment assumptions:** node-to-node design range 5 km. The infrastructure is the Helper plus balloons, with optional ground nodes. Dongles are on the ground, often indoors or under rubble. Drones are used only for inaccessible areas and are not part of the mesh in V1. Maximum reach is TTL 10 × 5 km ≈ 50 km.

## 3. Hardware

Every module:

| Part | Notes |
| --- | --- |
| ESP32-C3 Super Mini | native USB (USB Serial/JTAG, VID `0x303A` PID `0x1001`), 4 MB flash |
| SX1278 433 MHz LoRa | **3.3 V only**; never transmit without the antenna attached (it can damage the PA) |
| Antenna | external, 433 MHz |
| Battery | own battery on every module, the dongle included |
| NEO-6M GPS v2 | **relays (balloon / ground) only**; the dongle and Helper have none |

### Wiring (ESP32-C3 Super Mini)

| Signal | GPIO | Notes |
| --- | --- | --- |
| SX1278 DIO0 | 2 | GPIO2 is a strapping pin, see the note below |
| SX1278 RST | 3 | |
| SX1278 MOSI | 4 | SPI |
| SX1278 MISO | 5 | SPI |
| SX1278 SCK | 6 | SPI |
| SX1278 NSS | 7 | SPI chip select |
| NEO-6M TX → ESP RX | 20 | UART1 RX, 9600 baud (relays only; ESP TX to the GPS is not needed) |

- **GPIO2 is a strapping pin** on the C3. DIO0 is an input, so it normally works, but if a board fails to boot with the LoRa module attached, move DIO0 to GPIO10 (free and not strapping).
- **No interrupts are used.** The firmware polls the radio, and reads the CAD result from the SX1278's IRQ register over SPI, so DIO1 is not needed.
- On a table: TX power **2–5 dBm**, modules **≥ 1 m apart**. Full power at 30 cm overloads the receiver and corrupts packets.
- If a board gets no power over a C-to-C cable, the cable or board is missing the CC pull-down resistors. Use an OTG adapter with an A-to-C cable.

### Radio

One channel, **433.175 MHz**, for every packet. SF 9, BW 125 kHz, CR 4/5, sync word `0x12`, preamble 8, explicit header, hardware CRC on, PA_BOOST. This stays inside **433.05–434.79 MHz**, India's licence-exempt low-power band. The power, bandwidth and duty-cycle limits for that band must be checked before any field deployment: the older 2015 notification capped ERP at 10 mW.

## 4. Toolchain

| | |
| --- | --- |
| IDE | **Arduino IDE** |
| Board package | esp32 by Espressif **3.x**, board **ESP32C3 Dev Module** |
| Board settings | USB CDC On Boot: **Enabled** (so `Serial` is the native USB port), Flash Size: 4 MB, default partition scheme |
| Libraries | **LoRa by Sandeep Mistry** (`LoRa.h`), **ArduinoJson by Benoit Blanchon** (v7) and `SPI.h`; `Preferences.h` comes with the esp32 core |
| GPS | a small NMEA reader (`$GPGGA`) in the firmware, no GPS library |
| Storage | `Preferences` (NVS): stored messages, seen messages, own message counter |
| Sketch | `firmware/firmware.ino`, one file |

## 5. Firmware

**One file, `firmware/firmware.ino`, for every module.** The top of the file is all a team member edits before flashing:

```c
// 1. Which module is this? Set exactly one to 1.
#define IS_DONGLE 0
#define IS_RELAY  1
#define IS_HELPER 0

// 2. This module's ID and position (one block per module type)
#define MY_ID 0x0011
#define RELAY_IS_BALLOON 1                             // 0 = ground node
const double MY_LAT = 27.716000, MY_LON = 85.320000;  // used until the GPS has a fix

// 3. The network: the same on every module
#define HELPER_ID 0x0001
const double HELPER_LAT = 27.752000, HELPER_LON = 85.320000;
#define DEMO_RANGE_KM 0
#define TX_POWER 5
#define HELLO_INTERVAL_MS 10000UL
```

The mesh code is the same on every module. The flags only switch the role-specific parts: the phone protocol (dongle), the GPS (relay), the DATA printout (helper), and whether DATA is accepted and forwarded. The rest of the file, in order:

| Section | What it does |
| --- | --- |
| Wiring, radio, packets, timing, sizes | constants from sections 3 and 4, packet design and algorithm design |
| Types, state | packet structs, neighbour table, message store |
| Packet encode / decode | DATA as JSON (ArduinoJson); the binary packets with explicit little-endian fields, CRC8, length checks |
| Radio | `LoRa` setup, send, polled receive, CAD |
| Neighbours | HELLO / INI handling, HEARD / ACTIVE / STALE, routing order |
| Message store | messages and seen keys in `Preferences`, eviction |
| Status to the phone | JSON `status` / `info` lines (dongle) |
| Helper printout | the DATA block for the PC |
| Receiving | store before ACK, DELIVERY handling |
| Sending | listen before talk, ACK wait, retries and backoff, next-hop choice |
| Beacons | HELLO / INI from relays and the Helper |
| GPS | NEO-6M reader (relays) |
| USB | phone protocol (dongle) and test commands |
| `setup()` / `loop()` | |

```c
void loop() {
  usbTick();                                 // phone lines (dongle) and test commands
  if (radioReceive(...)) handlePacket(...);  // polled receive
  gpsTick();                                 // relays only
  beaconTick();                              // HELLO / INI, relays and Helper
  doWork();                                  // send one due message: DELIVERY, own, forward
  secondTick();                              // neighbours going quiet, waiting messages, INFO
}
```

### Using the Sandeep Mistry LoRa library

| Need | How |
| --- | --- |
| Setup | `SPI.begin(6, 5, 4, 7); LoRa.setPins(7, 3, 2); LoRa.begin(433175000);`, then SF, BW, CR, preamble, sync word, `enableCrc()`, `setTxPower()` |
| Receive | Poll `LoRa.parsePacket()` every loop, read the bytes, `packetRssi()`, `packetSnr()`. No callbacks or interrupts. |
| Transmit | `beginPacket()`, `write()`, `endPacket()`, which waits until the packet is out (≤ 330 ms). |
| CAD | Put the radio in CAD mode with two register writes over the same SPI bus, poll `RegIrqFlags` (`0x12`: CadDone `0x04`, CadDetected `0x01`), clear the flags, `LoRa.idle()`. The next `parsePacket()` receives again. |

### USB

On the **dongle**, the port carries the phone protocol as JSON lines (`send`, `info_request` in; `status`, `info` out, see [usb-protocol.md](../docs/usb-protocol.md)). A team member can type the same JSON line into the serial monitor to test without the phone, e.g. `{"type":"send","people":2,"flags":3,"lat":27.68,"lon":85.32,"alt":1300,"acc":10,"time":1761217205}`.

On every module, three test commands:

```
status                      neighbours and stored messages
send <people> <flags_hex>   create a test message (dongle and relay)
erase                       clear stored messages and seen keys (keeps the message counter)
```

Every other line the firmware prints starts with `#` or `@`, never `{`, so the phone app ignores it. `Serial.setTxTimeoutMs(0)` keeps a module from stalling when nothing reads its USB port.

### Logs

Two kinds go out on the same port.

**Readable text**, including the Helper's block per **new** message (duplicates are not printed):

```
============== DATA RECEIVED ==============
type           : DATA (JSON, 177 bytes)
message id     : 2001-00042
source         : 0x2001 (dongle)
destination    : 0x0001 (helper)
next hop       : 0x0001
ttl            : 9
hops           : 1
-------------------------------------------
people         : 2
trapped        : YES
injured        : YES
fire           : NO
flood          : NO
water/food     : NO
-------------------------------------------
latitude       : 27.680000 N
longitude      : 85.320000 E
altitude       : 1300 m
position acc.  : no fix
timestamp      : 2025-10-23 11:00:05 UTC (1761217205)
-------------------------------------------
path           : 0x2001 -> 0x0011 (2 nodes)
last hop       : 0x0011, RSSI -71 dBm, SNR 8.5 dB
crc            : OK (LoRa CRC16)
-------------------------------------------
json           : {"t":"DATA","src":"2001","mid":42,"dst":"0001","nh":"0001","ttl":9,...}
===========================================
```

The firmware prints UTC; the PC adds its own receive time.

**JSON lines starting with `@`**, for the PC dashboard and the live simulation:

```
@{"t":12345,"node":"0x0011","ev":"data_rx","key":"2001-00042","from":"0x2001","rssi":-71,"snr":8.5,"ttl":10,"hops":0}
@{"t":12346,"node":"0x0011","ev":"ack_rx","key":"2001-00042","from":"0x0001","retries":1}
@{"t":12350,"node":"0x2001","ev":"retry","key":"2001-00042","to":"0x0011","n":2}
@{"t":12360,"node":"0x2001","ev":"waiting","key":"2001-00042"}
@{"t":12400,"node":"0x0001","ev":"delivered","key":"2001-00042","path":["0x2001","0x0011","0x0001"]}
@{"t":13000,"node":"0x0011","ev":"nbr","id":"0x0001","state":"ACTIVE","why":"hellos/snr","rssi":-60,"snr":9.0}
@{"t":13100,"node":"0x2001","ev":"msg_state","key":"2001-00042","state":"DELIVERED"}
```

They feed the simulation's panels: the per-message log (path, timestamp, status, route), each node's RSSI, and network status (average hop count, message success rate, average RSSI, total / active / down nodes).

## 6. Sizing

| | MVP test | Design target |
| --- | --- | --- |
| Helpers | 1 | 1 per zone (more zones in V2) |
| Infra nodes | 1–5 | ~50 balloons at 5 km spacing, zone ~1,000 km² (radius ~18 km, ~4 hops) |
| Dongles | 1–2 | up to 10,000 registered |
| Helper capacity | | ~1,000–1,500 messages / hour. Each message costs ~0.8 s of Helper channel time (DATA + ACK + DELIVERY + DELIVERY_ACK). Bursts above that wait in the mesh (STORED / WAITING); they are not lost. |
| Storage per node | 50 messages (dongle 16), 200 seen keys | 1,000 messages, 10,000 seen keys (needs the raw flash store, algorithm design section 9) |

## 7. Milestones

Each milestone has a test that says it is done.

| # | Build | Done when |
| --- | --- | --- |
| M0 | Bring-up: SPI, radio, USB | `LoRa.begin()` succeeds; `status` answers on every module |
| M1 | Packet encode / decode | All 6 types round-trip (DATA as JSON); CRC matches the test vectors; the app and the sim use the same vectors |
| M2 | Radio ping | 1,000 packets A ↔ B; measured airtime within ±5 ms of the table; RSSI / SNR logged |
| M3 | DATA / ACK / retry with CAD | Pull B's antenna → A retries with backoff; two senders at once both get through |
| M4 | Store + seen keys | Reboot B → the message is still in B (`status`); a duplicate DATA gets an ACK but isn't stored again |
| M5 | HELLO, neighbour table, geographic forwarding | A → B → C with no configured route: A picks B because B is closer to the Helper (C) |
| M6 | DELIVERY, phone protocol on the dongle, Helper output | **Phone → dongle → Helper → laptop works, the app shows DELIVERED**; Act 1 passes 10 times in a row. **Demo ready.** |
| M7 | Relay with GPS + range filter | Act 2 passes 10 times in a row; the relay's HELLO carries its GPS position once it has a fix |
| M8 | Redundancy + failure testing | Diamond A–B–D–C (C = Helper): turn B off and the message still reaches C via D. Measure delivery rate, retry count, hops, duplicates, delivery time, lost messages under node failure, reboot, packet loss, moving nodes, simultaneous messages. |

## 8. Demo

All modules have antennas on, TX power 2–5 dBm, and are ≥ 1 m apart. Power the Helper (and relay) on first. The dongle can use a node as soon as it hears one HELLO (≤ 12 s with `HELLO_INTERVAL_MS` 10 s), so check that the app shows nodes in range before pressing SEND. A message sent earlier just waits (WAITING) and goes out on its own. A relay forwards only through ACTIVE neighbours, which takes 3 HELLOs (~30 s) after power-on.

**Act 1: dongle + Helper** (`DEMO_RANGE_KM 0` on both)

1. Send from the app → the laptop prints the DATA block; the app shows DELIVERED.
2. Power off the Helper → send → the app shows WAITING.
3. Power the Helper back on → its HELLOs return, the message goes through → DELIVERED (store and forward).
4. Reboot the Helper right after it prints a message → no duplicate on the laptop when the dongle retries (the seen keys survive the reboot).

**Act 2: through the relay**: set `DEMO_RANGE_KM 5` and the positions below at the top of the file, and flash all three.

| Module | ID | Position (placeholder; swap in the simulation's Nepal 2024 flood-area coordinates) |
| --- | --- | --- |
| Dongle | `0x2001` | 27.680000, 85.320000 (the app's `DEMO_POSITION`) |
| Relay | `0x0011` | 27.716000, 85.320000 (4.0 km from the dongle) |
| Helper | `0x0001` | 27.752000, 85.320000 (4.0 km from the relay, 8.0 km from the dongle → filtered) |

These are the defaults in the file. The relay uses its GPS position once it has a fix, so keep its GPS indoors (no fix) for the demo, or the range filter sees its real position.

1. Send from the app → the laptop shows `path: 0x2001 -> 0x0011 (2 nodes)`.
2. Power off the relay → WAITING → power it on → DELIVERED.
3. Reboot the relay mid-flow → the message survives in flash, no duplicate on the laptop.

Powering the Helper off also closes its serial port on the laptop, so the laptop-side reader must reconnect on its own.

Expected latency with nothing unplugged: about 1–1.5 s per hop (a JSON DATA takes ~0.9–1.2 s on air), plus backoff if a packet is lost.

## 9. V1 success criteria

- [ ] 3–5 nodes operate independently
- [ ] Nodes discover nearby nodes automatically and maintain neighbour information
- [ ] Nodes know approximate positions (GPS or fallback) and the Helper's position
- [ ] Geographic forwarding works
- [ ] Messages are stored before ACK, and survive a node reboot
- [ ] Duplicate messages are suppressed
- [ ] Failed transmissions are retried
- [ ] TTL prevents infinite propagation
- [ ] Multiple forwarding paths can exist, and a message survives at least one forwarding-node failure
- [ ] A message eventually reaches the Helper, and DELIVERY reaches the dongle
- [ ] Phone → dongle → mesh → Helper → laptop works end to end
- [ ] Delivery and retry statistics can be measured (JSON log)

## 10. Out of scope for V1 (V2+)

Custom PCB, solar power, drone integration, LTE / 4G, sensors, OLED / e-paper, dynamic or multiple Helpers, authentication, encryption, full A*, dynamic spreading factor, message inventory exchange ("I have / I need"), advanced congestion control, sleep and power optimisation, sophisticated routing recovery, multi-channel (TDM + FDM) channel access.

## 11. Decisions that change the original plan

| Original (`plan.txt`, `packet v2`, `TX RX plan`) | V1 design | Why |
| --- | --- | --- |
| The dongle also relays (plan §24, §28) | The dongle **never** relays; it originates and receives ACK / DELIVERY for its own messages | packet v2: keeps trapped users' batteries for their own SOS and keeps devices that come and go out of routing |
| Free-text message | `people` + `flags` bitfield | Fixed-size DATA, shorter airtime |
| Node IDs 1 byte; SYNC packet type | uint16 IDs by role range; HELLO does the upkeep | packet v2 |
| SF 7–10 to be tested | SF 9 fixed | packet v2 |
| Hybrid TDM + FDM after the demo (TX RX plan) | One channel with CAD (packet v2 rev 4) | Kept basic: no time sync and no channel hopping. Multi-channel stays a V2 option. |
| 433.000 MHz (packet v2) | 433.175 MHz | A 125 kHz channel centred on 433.000 MHz reaches below the 433.05 MHz band edge |
| No GPS in the demo (TX RX plan) | NEO-6M on relays, fixed position until the first fix; dongle and Helper without GPS | Team hardware decision |
| PlatformIO + RadioLib (TX RX plan) | Arduino IDE + Sandeep Mistry LoRa, one `.ino` file | Team toolchain, kept basic |
| Config over USB, stored in flash (packet v2) | Role, ID and positions are constants at the top of the file | Kept basic: edit and flash, like the team's test sketch |
| Raw flash partition for messages (TX RX plan) | `Preferences` (NVS), 50 messages and 200 seen keys | No custom partition table; enough for the MVP |
| TX RX plan pins (with DIO1) | Team wiring in section 3, no DIO1, no interrupts | Boards already wired; polling is simpler |
| Phone frame `SEND\|app_id\|…` / `ST\|…` (TX RX plan §13a) | JSON lines both ways: `send` / `info_request` from the website, `status` / `info` from the dongle | Team decision; the phone supplies the victim position |
| Binary DATA (packet v2) | DATA as JSON over LoRa; the other packets stay binary | Team decision: readable on any receiver; costs ~3–4x airtime, so `SLOT` is 1.3 s |
| DELIVERY_ACK `ack_to` = DELIVERY sender | `ack_to` = stored next_hop; matched on `ack_from` + key | DELIVERY has no sender field (packet design section 6) |
| DATA accepted by the helper named in `destination_id` | Any Helper accepts any DATA | V1 has one Helper; a message that reaches a rescue base is delivered |
| — | STALE probation, DELIVERY retry rounds | Real-world gaps found while detailing the algorithm (algorithm design sections 3, 6) |

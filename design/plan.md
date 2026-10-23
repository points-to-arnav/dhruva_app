# Dhruva Jaal: V1 plan

A modular LoRa disaster-relief mesh. A trapped person needs only a **phone + a USB LoRa dongle**: they pick what they need, press SEND, and the mesh of balloon and ground nodes carries the message hop by hop to a **Helper** (the rescue base), which confirms delivery all the way back.

Companion documents:
- [packet-design.md](packet-design.md): on-air packet formats (rev 5)
- [algorithm-design.md](algorithm-design.md): MAC, time sync, neighbours, routing, retries, storage
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
| SX1278 DIO0 | 2 | RxDone / CadDone interrupt. GPIO2 is a strapping pin, see the note below |
| SX1278 RST | 3 | |
| SX1278 MOSI | 4 | SPI |
| SX1278 MISO | 5 | SPI |
| SX1278 SCK | 6 | SPI |
| SX1278 NSS | 7 | SPI chip select |
| NEO-6M TX → ESP RX | 20 | UART1 RX, 9600 baud (relays only; ESP TX to the GPS is not needed) |
| Status LED | 8 | onboard, active low |

- **GPIO2 is a strapping pin** on the C3. DIO0 is an input, so it normally works, but if a board fails to boot with the LoRa module attached, move DIO0 to GPIO10 (free and not strapping).
- **DIO1 is not wired.** The CAD result is read from the SX1278's IRQ register over SPI instead (section 5).
- On a table: TX power **2–5 dBm**, modules **≥ 1 m apart**. Full power at 30 cm overloads the receiver and corrupts packets.
- If a board gets no power over a C-to-C cable, the cable or board is missing the CC pull-down resistors. Use an OTG adapter with an A-to-C cable.

### Radio

SF 9, BW 125 kHz, CR 4/5, sync word `0x12`, preamble 8, explicit header, hardware CRC on, PA_BOOST. There are 8 channels at 200 kHz spacing from 433.175 MHz: ch 0 is control, ch 1–7 are data (packet design section 7). This stays inside **433.05–434.79 MHz**, India's licence-exempt low-power band. The power, bandwidth and duty-cycle limits for that band must be checked before any field deployment: the older 2015 notification capped ERP at 10 mW.

## 4. Toolchain

| | |
| --- | --- |
| IDE | **Arduino IDE** |
| Board package | esp32 by Espressif **3.x**, board **ESP32C3 Dev Module** |
| Board settings | USB CDC On Boot: **Enabled** (so `Serial` is the native USB port), Flash Size: 4 MB |
| Libraries | **LoRa by Sandeep Mistry** (`LoRa.h`) and `SPI.h`; nothing else to install |
| GPS | own NMEA reader (GGA + RMC) in `gps.cpp`, no GPS library |
| Config storage | `Preferences` (NVS), built in |
| Message storage | raw flash partition via `esp_partition_*`, built in |
| Sketch | `firmware/firmware.ino`; the module is picked in `firmware/role.h` |

`firmware/partitions.csv` sits next to the sketch, so the esp32 core picks it up automatically:

```
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   0x5000
phy_init, data, phy,     0xe000,   0x1000
factory,  app,  factory, 0x10000,  0x200000
mesh,     data, 0x40,    0x210000, 0x40000
```

## 5. Firmware

**One firmware for every module.** The module is picked at compile time in `role.h`, with exactly one flag set to 1:

```c
#define IS_DONGLE 0   // plugged into the phone: originates SOS messages, never relays
#define IS_RELAY  1   // balloon or ground node (which one: `set role` over USB)
#define IS_HELPER 0   // rescue base: final stop, prints to the PC, sends DELIVERY
```

The mesh code is the same on every module. The flags only switch the role-specific parts: the phone protocol (dongle), the GPS (relay), the DATA printout and time root (helper), and whether DATA is accepted and forwarded.

```
firmware/
  firmware.ino     setup() + loop()
  role.h           IS_DONGLE / IS_RELAY / IS_HELPER, role IDs, default node ID
  pins.h           wiring (section 3)
  partitions.csv   adds the 256 KB `mesh` partition
  mesh_config.*    NVS config, own message counter
  cli.*            USB command line (and routes phone lines to phone.*)
  radio.*          LoRa library wrapper: DIO0 interrupt, channel switch, CAD
  packet.*         serialize / deserialize, crc8, airtime
  timesync.*       superframe clock, parent choice
  mac.*            control / data / guard windows, one exchange per pair, retries
  neighbor.*       neighbour table, states, range filter, dongle candidates
  routing.*        next-hop selection
  txq.*            jobs, backoff state, refill from flash
  store.*          raw-partition message log, seen set, boot scan
  mesh.*           receive dispatch, store before ACK, DELIVERY, own messages
  geo.*            distance, progress to the Helper
  timeutil.*       Unix time <-> UTC
  mesh_log.*       readable text + @JSON lines
  phone.*          dongle: SEND / STATUS / INFO
  gps.*            relay: NEO-6M NMEA reader, INI on first fix
  report.*         helper: DATA block to the PC
```

The code style is plain C-style modules (structs + functions, no class hierarchies) with a single main loop and no extra FreeRTOS tasks:

```c
void loop() {
    cli_poll();                       // CLI, and on the dongle the phone protocol
    if (radio_poll(f)) mesh_on_frame(f);  // DIO0 flag -> receive dispatch
    gps_poll();                       // relay only
    mesh_tick(now);                   // time sync, control / data / guard windows, exchanges
    phone_tick(now);                  // dongle only: INFO when the node count changes
}
```

### Using the Sandeep Mistry LoRa library

| Need | How |
| --- | --- |
| Setup | `SPI.begin(6, 5, 4, 7); LoRa.setSPI(SPI); LoRa.setPins(7, 3, 2); LoRa.begin(433.175E6);`, then SF, BW, CR, sync word, preamble, `enableCrc()`, `setTxPower()` |
| Receive | Not `onReceive()`, which reads SPI inside the interrupt. Instead, an own `IRAM_ATTR` interrupt on DIO0 records only a flag and `esp_timer_get_time()`. The loop then calls `LoRa.parsePacket()`, reads the bytes and `packetRssi()` / `packetSnr()`, and calls `LoRa.receive()` again. `parsePacket()` is called only after the flag, because on an idle continuous RX it would switch the radio to single RX. |
| Transmit | `beginPacket()`, `write()`, blocking `endPacket()` (≤ 330 ms), then `receive()`. The TX start time is taken just before `endPacket()`, for `sf_offset_ms`. |
| Channel switch | `LoRa.idle(); LoRa.setFrequency(f);` then back to RX or on to CAD. Skipped if already on that channel. |
| CAD | Map DIO0 to CadDone and put the radio in CAD mode (register writes over the same SPI bus), wait for DIO0, read `RegIrqFlags` (`0x12`: CadDone `0x04`, CadDetected `0x01`), clear the flags, then `receive()` (which maps DIO0 back to RxDone). |

### USB command line (every module)

These are text commands, one per line, so any serial monitor works. Config is saved in NVS.

```
show cfg | nbr | msgs | stats | sync
set id 0x0011
set role balloon|ground                 # relays only
set helper 0x0001 27.752000 85.320000   # helper ID + position
set pos 27.716000 85.320000 1300        # fallback / fixed position
set txp 3                               # dBm
set range 5                             # DEMO_RANGE_KM, 0 = off
set sf_slots 25   set ctrl_slots 4      # superframe layout, the same on every node
set txlat 0                             # TX_LATENCY_US for time sync
save | reboot | erase store
send <people> <flags_hex>               # inject a test message (not on the Helper)
```

- **Dongle:** the same port carries the phone protocol (`SEND|…`, `INFO?` in; `STATUS|…`, `INFO|…` out, see [usb-protocol.md](../docs/usb-protocol.md)). Phone lines are uppercase and contain `|`; CLI commands are lowercase words. No log line starts with `STATUS` or `INFO`, so the app ignores them.
- `Serial.setTxTimeoutMs(0)`: with native USB, `Serial.print` can block when nothing is reading, which would freeze the radio timing.

### Logs

Two kinds go out on the same port.

**Readable text**, including the Helper's block per **new** message (duplicates are not printed):

```
============== DATA RECEIVED ==============
type / version : DATA / v1 (0x12)
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
crc8           : OK
===========================================
```

The firmware prints UTC; the PC adds its own receive time.

**JSON lines starting with `@`**, for the PC dashboard and the live simulation:

```
@{"t":12345,"node":"0x0011","ev":"data_rx","key":"2001-00042","from":"0x2001","rssi":-71,"snr":8.5,"ch":4}
@{"t":12346,"node":"0x0011","ev":"ack_rx","key":"2001-00042","from":"0x0001","retries":1}
@{"t":12350,"node":"0x2001","ev":"retry","key":"2001-00042","to":"0x0011","n":2}
@{"t":12360,"node":"0x2001","ev":"waiting","key":"2001-00042"}
@{"t":12400,"node":"0x0001","ev":"delivered","key":"2001-00042","path":["0x2001","0x0011","0x0001"]}
@{"t":12500,"node":"0x0011","ev":"sync","parent":"0x0001","depth":1,"err_ms":3}
@{"t":13000,"node":"0x0011","ev":"nbr","id":"0x0001","state":"ACTIVE","rssi":-60,"snr":9.0}
@{"t":20000,"node":"0x0011","ev":"stats","rx":120,"tx":40,"crc_fail":2,"dup":1,"nbr_active":2}
```

They feed the simulation's panels: the per-message log (path, timestamp, status, route), each node's RSSI, and network status (average hop count, message success rate, average RSSI, total / active / down nodes).

## 6. Sizing

| | MVP test | Design target |
| --- | --- | --- |
| Helpers | 1 | 1 per zone (more zones in V2) |
| Infra nodes | 1–5 | ~50 balloons at 5 km spacing, zone ~1,000 km² (radius ~18 km, ~4 hops) |
| Dongles | 1–2 | up to 10,000 registered |
| Helper capacity | | ~1,000–1,500 messages / hour. Each message costs ~0.8 s of Helper channel time (DATA + ACK + DELIVERY + DELIVERY_ACK). Bursts above that wait in the mesh (STORED / WAITING); they are not lost. |
| Storage per node | | 2,048 messages + ≥ 10,000 seen keys (algorithm design section 10) |

## 7. Milestones

Each milestone has a test that says it is done.

| # | Build | Done when |
| --- | --- | --- |
| M0 | Bring-up: SPI, USB command line, NVS config | SX1278 version register reads `0x12`; `set` / `show` / `save` survive a reboot |
| M1 | `packet` module + host tests | All 6 types round-trip; CRC matches the test vectors; the app and the sim use the same vectors |
| M2 | Radio ping on ch 0, then channel switching | 1,000 packets A ↔ B; measured airtime within ±5 ms of the table; RSSI / SNR logged; A and B meet on each of ch 1–7 |
| M3 | Superframe + time sync (one channel) | Relay and dongle report `sync` with `\|err\| < 10 ms`; `TX_LATENCY_US` measured |
| M4 | Home channels + DATA / ACK / retry | Pull B's antenna → A retries with backoff; two exchanges on different channels in the same pair both succeed |
| M5 | Flash store + seen set | Reboot B → the message is still in B; a duplicate DATA gets an ACK but isn't stored again |
| M6 | HELLO, neighbour table, geographic forwarding | A → B → C with no configured route: A picks B because B is closer to the Helper (C) |
| M7 | DELIVERY, phone protocol on the dongle, Helper output | **Phone → dongle → Helper → laptop works, the app shows DELIVERED**; Act 1 passes 10 times in a row. **Demo ready.** |
| M8 | Relay with GPS + range filter | Act 2 passes 10 times in a row; the relay's HELLO carries its GPS position once it has a fix |
| M9 | Redundancy + failure testing | Diamond A–B–D–C (C = Helper): turn B off and the message still reaches C via D. Measure delivery rate, retry count, hops, duplicates, delivery time, lost messages under node failure, reboot, packet loss, moving nodes, simultaneous messages. |

A cheap 24 MHz USB logic analyzer makes M3 an afternoon: toggle a GPIO at each superframe start on two boards and compare.

## 8. Demo

All modules have antennas on, TX power 2–5 dBm, and are ≥ 1 m apart. Power the Helper (and relay) on first. A dongle needs one HELLO (≤ 10 s with the demo superframe) to sync, so check that the app shows nodes in range before pressing SEND. A message sent earlier just waits (WAITING) and goes out on its own.

**Act 1: dongle + Helper** (`range 0` on both)

1. Send from the app → the laptop prints the DATA block; the app shows DELIVERED.
2. Power off the Helper → send → the app shows WAITING.
3. Power the Helper back on → its HELLOs return, the dongle re-syncs, the message goes through → DELIVERED (store and forward).
4. Reboot the Helper right after it prints a message → no duplicate on the laptop when the dongle retries (the seen set survives the reboot).

**Act 2: through the relay** (config only, no reflash): `range 5` on all three, then `set pos` with the positions below, `save`, `reboot`.

| Module | ID | Position (placeholder; swap in the simulation's Nepal 2024 flood-area coordinates) |
| --- | --- | --- |
| Dongle | `0x2001` | 27.680000, 85.320000 (the app's `DEMO_POSITION`) |
| Relay | `0x0011` | 27.716000, 85.320000 (4.0 km from the dongle) |
| Helper | `0x0001` | 27.752000, 85.320000 (4.0 km from the relay, 8.0 km from the dongle → filtered) |

1. Send from the app → the laptop shows `path: 0x2001 -> 0x0011 (2 nodes)`.
2. Power off the relay → WAITING → power it on → DELIVERED.
3. Reboot the relay mid-flow → the message survives in flash, no duplicate on the laptop.

Powering the Helper off also closes its serial port on the laptop, so the laptop-side reader must reconnect on its own.

Expected latency with nothing unplugged: one exchange per hop, at the next free pair. That is about 1–3 s per hop with the demo superframe, plus up to one superframe if the dongle has just synced.

## 9. V1 success criteria

- [ ] 3–5 nodes operate independently
- [ ] Nodes discover nearby nodes automatically and maintain neighbour information
- [ ] Nodes know approximate positions (GPS or fallback) and the Helper's position
- [ ] Every node keeps the superframe clock from the Helper, indoors, with no GPS
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

Custom PCB, solar power, drone integration, LTE / 4G, sensors, OLED / e-paper, dynamic or multiple Helpers, authentication, encryption, full A*, dynamic spreading factor, message inventory exchange ("I have / I need"), advanced congestion control, sleep and power optimisation, sophisticated routing recovery.

## 11. Decisions that change the original plan

| Original (`plan.txt`, `packet v2`, `TX RX plan`) | V1 design | Why |
| --- | --- | --- |
| The dongle also relays (plan §24, §28) | The dongle **never** relays; it originates and receives ACK / DELIVERY for its own messages | packet v2: keeps trapped users' batteries for their own SOS and keeps devices that come and go out of routing |
| Free-text message | `people` + `flags` bitfield | Fixed-size DATA, shorter airtime |
| Node IDs 1 byte; SYNC packet type | uint16 IDs by role range; HELLO does the upkeep | packet v2 |
| SF 7–10 to be tested | SF 9 fixed | packet v2 |
| CSMA for the demo, hybrid optional (TX RX plan) | **Hybrid TDM + FDM is the only MAC** | Chosen for real-world scale (balloons all hear each other); the demo exercises the same code path |
| `sync_depth` in `role_flags` bits 4–6 | Bits 4–7, 15 = unsynced | The clock reaches 14 hops, so it covers TTL 10 |
| No GPS in the demo (TX RX plan) | NEO-6M on relays, flash fallback until the first fix; dongle and Helper without GPS | Team hardware decision |
| PlatformIO + RadioLib (TX RX plan) | Arduino IDE + Sandeep Mistry LoRa | Team toolchain; the gaps (no DIO1, CAD result, interrupt-safe RX) are covered in section 5 |
| A codebase with the role as a config value (TX RX plan) | One firmware; the role is a compile-time flag in `role.h` (`IS_DONGLE` / `IS_RELAY` / `IS_HELPER`) | Each board's role is fixed by its hardware (phone port, GPS, PC link); balloon vs ground stays a USB setting |
| TX RX plan pins (with DIO1) | Team wiring in section 3, no DIO1 | Boards already wired |
| Phone frame `SEND\|app_id\|…` / `ST\|…` (TX RX plan §13a) | The protocol the web app already speaks: `SEND\|people\|flags\|lat\|lon\|alt\|acc\|ts`, `STATUS`, `INFO` | The app is built; the phone supplies the victim position |
| DELIVERY_ACK `ack_to` = DELIVERY sender | `ack_to` = stored next_hop; matched on `ack_from` + key | DELIVERY has no sender field (packet design section 6) |
| DATA accepted by the helper named in `destination_id` | Any Helper accepts any DATA | V1 has one Helper; a message that reaches a rescue base is delivered |
| — | STALE probation, recovery beyond the 4-neighbour set, DELIVERY retry rounds, store relocation before erase | Real-world gaps found while detailing the algorithm (algorithm design sections 4, 5, 7, 10) |


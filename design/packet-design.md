# Dhruva Jaal: packet design (V1, rev 5)

The on-air format for every LoRa packet in the mesh. Rev 5 is rev 4 (`packet v2`) plus the two fields the hybrid TDM + FDM MAC needs for time sync (HELLO and INI, sections 1 and 2). DATA, ACK, DELIVERY and DELIVERY_ACK are unchanged from rev 4.

How the packets are used (routing, retries, timing) is in [algorithm-design.md](algorithm-design.md). The phone ↔ dongle USB text protocol is a separate format, in [../docs/usb-protocol.md](../docs/usb-protocol.md). The phone never sees LoRa packets.

## Packet types

| Type | Code | Sent by | Purpose |
| --- | --- | --- | --- |
| HELLO | `0x1` | infra (helper, balloon, ground) | Periodic presence beacon: neighbour discovery, link quality, time sync |
| DATA | `0x2` | dongle (own SOS), infra (forwarding) | The emergency message |
| ACK | `0x3` | node that stored a DATA | Hop-by-hop: "I received and **stored** this" |
| DELIVERY | `0x4` | helper, then each node back along the path | Final confirmation, walked back hop by hop |
| INI | `0x5` | infra | First broadcast after boot, and again on the first GPS fix |
| DELIVERY_ACK | `0x6` | node that received a DELIVERY | Hop-by-hop ACK for DELIVERY |
| | `0x7`–`0xF` | | reserved |

## Common conventions

**Byte 0 of every packet is `type_ver`.** The upper 4 bits are the protocol version (V1 = `0x1`) and the lower 4 bits are the packet type. V1 DATA is `0x12`.

**Node IDs** are uint16, assigned over USB at setup and stored in flash. They are never derived from the ESP32 MAC address: hashing 10,000 MACs into 16 bits gives hundreds of collisions.

| Range | Role | Count |
| --- | --- | --- |
| `0x0000` | unassigned / invalid | |
| `0x0001`–`0x000F` | helpers | 15 (V1 uses 1) |
| `0x0010`–`0x0FFF` | balloon nodes | 4,080 |
| `0x1000`–`0x1FFF` | ground nodes | 4,096 |
| `0x2000`–`0xFFFE` | dongles | 57,343 |
| `0xFFFF` | broadcast | |

`role_flags` is the authority on role. The ranges keep IDs unique and readable in logs.

**Encoding**

| Field | Encoding |
| --- | --- |
| node IDs | uint16 |
| latitude, longitude | int32, degrees × 1e7 |
| altitude | int16, metres above sea level |
| timestamp | uint32, Unix seconds (UTC), 0 = no time yet |

- Multi-byte fields are **little-endian**.
- Use explicit serialize/deserialize functions. Never cast a packed struct onto the buffer.
- Packet length is not a field: LoRa explicit header mode carries it.

**`role_flags` (1 byte)**

| Bits | Meaning |
| --- | --- |
| 0–2 | role: 0 ground, 1 helper, 2 dongle, 3 balloon, 4–7 reserved |
| 3 | `gps_fix`: 1 = valid fix |
| 4–7 | `sync_depth` (rev 5): 0 = helper (time root), 1–14 = hops from the helper, 15 = unsynced |

**`crc8`** uses polynomial `0x07` with init `0x00`, and covers every byte before the crc8 byte. It is a software check on top of the SX1278 hardware CRC16.

## 1. INI: 25 bytes (infra only)

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | type_ver | 1 | `0x15` |
| 1 | node_id | 2 | |
| 3 | role_flags | 1 | includes gps_fix and sync_depth |
| 4 | helper_id | 2 | the helper this node is configured for |
| 6 | latitude | 4 | int32 × 1e7 |
| 10 | longitude | 4 | |
| 14 | altitude | 2 | int16 m |
| 16 | position_acc | 2 | uint16 decimetres, `0xFFFF` = no fix |
| 18 | gps_time | 4 | uint32 Unix s, 0 = no time yet |
| 22 | sf_offset_ms | 2 | rev 5: ms from the start of the sender's superframe to the start of this TX |
| 24 | crc8 | 1 | |

INI is sent once at boot (gps_fix = 0, position = the flash fallback position) and once more when a relay gets its first GPS fix. It is not ACKed. Receivers treat it like a HELLO (it goes into the neighbour table) and record `helper_id`.

## 2. HELLO: 20 bytes (infra only)

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | type_ver | 1 | `0x11` |
| 1 | node_id | 2 | |
| 3 | role_flags | 1 | includes gps_fix and sync_depth |
| 4 | latitude | 4 | int32 × 1e7 |
| 8 | longitude | 4 | |
| 12 | altitude | 2 | int16 m |
| 14 | position_acc | 2 | uint16 decimetres, `0xFFFF` = no fix |
| 16 | sequence_number | 1 | wraps 255 → 0 |
| 17 | sf_offset_ms | 2 | rev 5, as in INI |
| 19 | crc8 | 1 | |

- RSSI and SNR are **not** in the packet. The receiver reads them from its SX1278 for every packet and stores them in its neighbour table.
- A missed HELLO shows up as a gap in `sequence_number` (mod 256).
- Balloons drift. Receivers overwrite the stored neighbour position on every HELLO and recompute that neighbour's progress to the helper.

Example: node `0x0011` (balloon), role_flags `0x1B` (balloon, gps_fix = 1, sync_depth = 1), position_acc 25 (2.5 m), sequence 12.

## 3. DATA: 29 + 2n bytes, n = path_count (1..11), so 31 to 51 bytes

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | type_ver | 1 | `0x12` |
| 1 | source_id | 2 | the dongle (or node) that created the message |
| 3 | message_id | 2 | per-source counter, persisted in flash |
| 5 | destination_id | 2 | helper_id |
| 7 | next_hop_id | 2 | the one node that should take this packet |
| 9 | ttl | 1 | starts at 10 |
| 10 | people | 1 | 1–100 |
| 11 | flags | 1 | bitfield, below |
| 12 | latitude | 4 | int32 × 1e7, **victim** position |
| 16 | longitude | 4 | |
| 20 | altitude | 2 | int16 m |
| 22 | position_acc | 1 | uint8 metres, capped at 254, `0xFF` = no fix (indoor) |
| 23 | timestamp | 4 | uint32 Unix s, creation time (dongle: phone clock; node: GPS time) |
| 27 | path_count | 1 | 1..11 |
| 28 | path_history | 2n | uint16 node IDs, source first |
| 28 + 2n | crc8 | 1 | |

**Flags**

| Bit | Meaning |
| --- | --- |
| 0 | Trapped |
| 1 | Injured (medical help needed; the app's "Medical help") |
| 2 | Fire |
| 3 | Flood |
| 4 | Water/Food |
| 5–7 | reserved (0) |

**Dedup key:** `(source_id, message_id)`, 4 bytes, stored as `source_id << 16 | message_id`. It is shown as `2001-00042`: the source as 4 hex digits, `-`, then the message_id as 5 decimal digits.

**Path and TTL rules**

- The source creates the message with `path = [source]`, `path_count = 1` and `ttl = 10`.
- Only the node whose ID equals `next_hop_id` stores, ACKs and forwards. Other nodes that overhear it ignore it.
- On forward:
  - if `ttl == 0`: store, do not forward (the message is FAILED at this node)
  - `ttl = ttl - 1`
  - append its own ID to `path_history`, `path_count += 1`
  - pick a new `next_hop_id`
- The longest path is the source plus 10 forwarders, 11 entries. Hop count = `path_count - 1`.
- **Loop prevention:** a node never chooses a `next_hop_id` that is already in `path_history`.
- **prev_hop** is not a field. On receive, the node takes the **last** entry of `path_history` as prev_hop and stores it with the message. ACK and DELIVERY are routed back with it.

Example: source `0x2001`, message 42, destination `0x0001`, next hop `0x0012`, ttl 9, people 2, flags `0x03` (Trapped, Injured), position_acc `0xFF`, path_count 2, path `0x2001, 0x0011`.

## 4. ACK: 10 bytes (hop-by-hop, for DATA)

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | type_ver | 1 | `0x13` |
| 1 | ack_from | 2 | node that stored the message |
| 3 | ack_to | 2 | prev_hop, the node waiting for this ACK |
| 5 | orig_source | 2 | source_id of the DATA |
| 7 | message_id | 2 | |
| 9 | crc8 | 1 | |

An ACK means "`ack_from` received **and stored** `(orig_source, message_id)`". It does **not** mean the message reached the helper. Nodes whose ID is not `ack_to` ignore it. A duplicate DATA (already stored) is still ACKed, so the sender stops retrying.

## 5. DELIVERY: 10 bytes (final confirmation from the helper)

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | type_ver | 1 | `0x14` |
| 1 | helper_id | 2 | the helper that received the DATA (unchanged along the way back) |
| 3 | to_id | 2 | next node back along the path |
| 5 | orig_source | 2 | |
| 7 | message_id | 2 | |
| 9 | crc8 | 1 | |

The helper sets `to_id` to its stored prev_hop for that message. Each node whose ID equals `to_id` then:

1. sends a DELIVERY_ACK
2. marks `(orig_source, message_id)` as DELIVERED
3. if it is `orig_source`, stops there (a dongle reports DELIVERED to the phone)
4. otherwise sets `to_id` to its own stored prev_hop and sends the DELIVERY onward

DELIVERY uses the same retry and backoff rules as DATA. Example trace: `0x0001 → 0x0011 → 0x2001`.

## 6. DELIVERY_ACK: 10 bytes (hop-by-hop, for DELIVERY)

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | type_ver | 1 | `0x16` |
| 1 | ack_from | 2 | node that received the DELIVERY |
| 3 | ack_to | 2 | node that sent the DELIVERY (see note) |
| 5 | orig_source | 2 | |
| 7 | message_id | 2 | |
| 9 | crc8 | 1 | |

This has the same layout as ACK, with a separate type so it is never confused with a DATA ACK.

DELIVERY carries no sender field, because `helper_id` stays the helper's ID all the way back. So the receiver fills `ack_to` with the node it handed the DATA to (its stored `next_hop`), which is normally the node sending the DELIVERY back, or `0xFFFF` if it has no record. The waiting sender matches a DELIVERY_ACK on `ack_from` and the key, not on `ack_to`. That way a DELIVERY that comes back along a different path still gets acknowledged. This happens when an ACK was lost and the source then handed the same message to a second neighbour.

## 7. Radio configuration (the same on every node, stored in flash)

| Setting | Value |
| --- | --- |
| Band | 433.05–434.79 MHz, 8 channels (below) |
| Bandwidth | 125 kHz |
| Spreading factor | 9 |
| Coding rate | 4/5 |
| Preamble | 8 symbols |
| Sync word | `0x12` (private) |
| Header | explicit |
| Hardware CRC | on |
| IQ | normal (not inverted) |
| LDRO | off (symbol time 4.096 ms < 16 ms) |
| PA output | PA_BOOST |
| TX power | the same on all nodes; 2–5 dBm on a table, check the WPC 433 MHz limit in the field |
| Channel access | CAD (Channel Activity Detection) before DATA, DELIVERY, HELLO and INI |

**Channel plan** (200 kHz spacing):

| Ch | Centre (MHz) | Use |
| --- | --- | --- |
| 0 | 433.175 | control: HELLO, INI, time sync |
| 1 | 433.375 | data |
| 2 | 433.575 | data |
| 3 | 433.775 | data |
| 4 | 433.975 | data |
| 5 | 434.175 | data |
| 6 | 434.375 | data |
| 7 | 434.575 | data |

Every node, dongles included, has a **home channel** computed from its ID. Nobody signals it: anyone who knows your ID knows your channel.

```c
uint8_t home_ch(uint16_t id) {
    uint8_t mix = (id & 0xFF) ^ (id >> 8);
    return 1 + (mix % 7);            // 1..7
}
```

The demo IDs land on different channels: `0x0001` helper → ch 2, `0x0011` relay → ch 4, `0x2001` dongle → ch 6.

## 8. Airtime (SF9 / BW125 / CR4/5 / preamble 8 / CRC on)

| Packet | Size | Airtime |
| --- | --- | --- |
| ACK, DELIVERY, DELIVERY_ACK | 10 B | 144 ms |
| HELLO | 20 B | 185 ms |
| INI | 25 B | 206 ms |
| DATA, path 1–3 | 31–35 B | 247 ms |
| DATA, path 4–5 | 37–39 B | 267 ms |
| DATA, path 6–7 | 41–43 B | 288 ms |
| DATA, path 8–9 | 45–47 B | 308 ms |
| DATA, path 10–11 | 49–51 B | 329 ms |

`SLOT = 400 ms` fits every packet, including the largest DATA. The rev 5 HELLO and INI are 2 bytes longer than in rev 4, but they still round to the same number of LoRa symbols, so their airtime is unchanged.

Airtime formula (used by time sync to work back from the RxDone time to the sender's TX start):

```
T_sym     = 2^SF / BW                                   = 4.096 ms
T_pre     = (preamble + 4.25) * T_sym
n_payload = 8 + max(ceil((8*PL - 4*SF + 28 + 16) / (4*SF)) * (CR + 4), 0)
airtime   = T_pre + n_payload * T_sym                    (CR = 1 for 4/5)
```

## 9. Changes from rev 4 (`packet v2`)

| Change | Why |
| --- | --- |
| HELLO 18 → 20 B, INI 23 → 25 B: new `sf_offset_ms` before `crc8` | The hybrid MAC syncs every node's superframe clock from beacons. Because the offset is in the packet, a node may send its HELLO in any control slot. |
| `role_flags` bits 4–7 = `sync_depth` (15 = unsynced) | The TX/RX plan proposed bits 4–6 (depth 0–6). With 4 bits the clock reaches 14 hops, so it covers the whole TTL 10 path. |
| 8 × 125 kHz channels, receiver home channels | FDM: exchanges with different receivers can run at the same time. |
| DELIVERY_ACK `ack_to` = stored next_hop, matched on `ack_from` + key | DELIVERY has no sender field (section 6). |
| DATA, ACK, DELIVERY: unchanged | The phone app and the helper output don't change. |

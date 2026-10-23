# Dhruva Jaal: packet design (V1, rev 4)

The on-air format for every LoRa packet in the mesh: `packet v2` rev 4, except that **DATA travels as JSON** (section 3). HELLO, INI, ACK, DELIVERY and DELIVERY_ACK stay short binary packets. Section 9 lists what is settled on top of rev 4.

How the packets are used (routing, retries, timing) is in [algorithm-design.md](algorithm-design.md). The phone ↔ dongle USB protocol (JSON lines) is a separate format, in [../docs/usb-protocol.md](../docs/usb-protocol.md). The phone never sees LoRa packets.

## Packet types

| Type | Code | Sent by | Purpose |
| --- | --- | --- | --- |
| HELLO | `0x1` | infra (helper, balloon, ground) | Periodic presence beacon: neighbour discovery, link quality |
| DATA | JSON (`0x2` reserved) | dongle (own SOS), infra (forwarding) | The emergency message |
| ACK | `0x3` | node that stored a DATA | Hop-by-hop: "I received and **stored** this" |
| DELIVERY | `0x4` | helper, then each node back along the path | Final confirmation, walked back hop by hop |
| INI | `0x5` | infra | First broadcast after boot, and again on the first GPS fix |
| DELIVERY_ACK | `0x6` | node that received a DELIVERY | Hop-by-hop ACK for DELIVERY |
| | `0x7`–`0xF` | | reserved |

## Common conventions

**Binary packets:** byte 0 is `type_ver`. The upper 4 bits are the protocol version (V1 = `0x1`) and the lower 4 bits are the packet type, so byte 0 is `0x11`–`0x16`. **A JSON DATA starts with `{`** (`0x7B`), which no binary packet does, so a receiver tells them apart by the first byte.

**Node IDs** are uint16, set per module at the top of the firmware. They are never derived from the ESP32 MAC address: hashing 10,000 MACs into 16 bits gives hundreds of collisions.

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
| 4–7 | reserved (0) |

**`crc8`** (binary packets) uses polynomial `0x07` with init `0x00`, and covers every byte before the crc8 byte. It is a software check on top of the SX1278 hardware CRC16. A JSON DATA has no crc8: it relies on the hardware CRC16, and a damaged one also fails to parse.

## 1. INI: 23 bytes (infra only)

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | type_ver | 1 | `0x15` |
| 1 | node_id | 2 | |
| 3 | role_flags | 1 | includes gps_fix |
| 4 | helper_id | 2 | the helper this node is configured for |
| 6 | latitude | 4 | int32 × 1e7 |
| 10 | longitude | 4 | |
| 14 | altitude | 2 | int16 m |
| 16 | position_acc | 2 | uint16 decimetres, `0xFFFF` = no fix |
| 18 | gps_time | 4 | uint32 Unix s, 0 = no time yet (the basic firmware sends 0) |
| 22 | crc8 | 1 | |

INI is sent once at boot (gps_fix = 0, position = the flash fallback position) and once more when a relay gets its first GPS fix. It is not ACKed. Receivers treat it like a HELLO (it goes into the neighbour table) and record `helper_id`.

## 2. HELLO: 18 bytes (infra only)

| Offset | Field | Size | Notes |
| --- | --- | --- | --- |
| 0 | type_ver | 1 | `0x11` |
| 1 | node_id | 2 | |
| 3 | role_flags | 1 | includes gps_fix |
| 4 | latitude | 4 | int32 × 1e7 |
| 8 | longitude | 4 | |
| 12 | altitude | 2 | int16 m |
| 14 | position_acc | 2 | uint16 decimetres, `0xFFFF` = no fix |
| 16 | sequence_number | 1 | wraps 255 → 0 |
| 17 | crc8 | 1 | |

- RSSI and SNR are **not** in the packet. The receiver reads them from its SX1278 for every packet and stores them in its neighbour table.
- A missed HELLO shows up as a gap in `sequence_number` (mod 256).
- Balloons drift. Receivers overwrite the stored neighbour position on every HELLO and recompute that neighbour's progress to the helper.

Example: node `0x0011` (balloon), role_flags `0x0B` (balloon, gps_fix = 1), position_acc 25 (2.5 m), sequence 12.

## 3. DATA: one JSON object, 169–249 bytes

```json
{"t":"DATA","src":"2001","mid":42,"dst":"0001","nh":"0011","ttl":10,"ppl":2,"flg":3,"lat":27.680000,"lon":85.320000,"alt":1300,"acc":255,"ts":1761217205,"path":["2001"]}
```

| Key | Field | Format |
| --- | --- | --- |
| `t` | type | always `"DATA"` |
| `src` | source_id | 4 hex digits: the dongle (or node) that created the message |
| `mid` | message_id | integer 0–65535, per-source counter, persisted in flash |
| `dst` | destination_id | 4 hex digits, the helper |
| `nh` | next_hop_id | 4 hex digits: the one node that should take this packet |
| `ttl` | ttl | integer, starts at 10 |
| `ppl` | people | integer 1–100 |
| `flg` | flags | integer 0–31, bitfield below |
| `lat`, `lon` | latitude, longitude | number, decimal degrees, 6 decimals: the **victim** position |
| `alt` | altitude | integer metres |
| `acc` | position_acc | integer metres, capped at 254, `255` = no fix (indoor) |
| `ts` | timestamp | integer Unix seconds, creation time (dongle: phone clock; node: 0) |
| `path` | path_history | array of 1–11 IDs (4 hex digits each), source first |

- Keys are short and IDs have no `0x`, so a DATA with a full 11-entry path stays under the 255-byte LoRa limit (249 bytes at most).
- Inside a node the DATA is decoded into the same fields as the binary rev 4 layout and stored that way; it is turned back into JSON for every transmission.

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

Example, after one forward: `{"t":"DATA","src":"2001","mid":42,"dst":"0001","nh":"0001","ttl":9,"ppl":2,"flg":3,"lat":27.680000,"lon":85.320000,"alt":1300,"acc":255,"ts":1761217205,"path":["2001","0011"]}`.

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

## 7. Radio configuration (the same on every node)

| Setting | Value |
| --- | --- |
| Frequency | 433.175 MHz, one channel for every packet (inside India's 433.05–434.79 MHz band) |
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
| Channel access | CAD (Channel Activity Detection) before DATA, DELIVERY, HELLO and INI; ACK and DELIVERY_ACK go out at once |

## 8. Airtime (SF9 / BW125 / CR4/5 / preamble 8 / CRC on)

| Packet | Size | Airtime |
| --- | --- | --- |
| ACK, DELIVERY, DELIVERY_ACK | 10 B | 144 ms |
| HELLO | 18 B | 185 ms |
| INI | 23 B | 206 ms |
| DATA (JSON), path 1 | 169–179 B | 0.86–0.90 s |
| DATA (JSON), path 5 | 197–207 B | 0.98–1.05 s |
| DATA (JSON), path 11 | 239–249 B | 1.19–1.23 s |

`SLOT = 1.3 s` fits every packet, including the largest JSON DATA. A binary DATA would have been 31–51 bytes (247–329 ms): JSON costs about 3–4 times the airtime, in exchange for packets any receiver can read.

Airtime formula:

```
T_sym     = 2^SF / BW                                   = 4.096 ms
T_pre     = (preamble + 4.25) * T_sym
n_payload = 8 + max(ceil((8*PL - 4*SF + 28 + 16) / (4*SF)) * (CR + 4), 0)
airtime   = T_pre + n_payload * T_sym                    (CR = 1 for 4/5)
```

## 9. Settled on top of `packet v2` rev 4

| Point | Decision | Why |
| --- | --- | --- |
| Frequency | 433.175 MHz instead of 433.000 MHz | A 125 kHz channel centred on 433.000 MHz reaches below the 433.05 MHz band edge |
| DELIVERY_ACK | `ack_to` = stored next_hop, matched on `ack_from` + key | DELIVERY has no sender field (section 6) |
| DATA | JSON instead of the binary layout | Team decision: readable on any receiver; costs ~3–4x airtime, so `SLOT` is 1.3 s |
| Other packets | binary, unchanged | HELLO and ACK traffic stays short |

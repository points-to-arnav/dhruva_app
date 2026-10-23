# Phone ↔ dongle USB protocol (V1)

The phone app (a web page in Chrome) and the dongle (ESP32-C3 Super Mini, built-in USB Serial/JTAG, VID `0x303A` PID `0x1001`) exchange **JSON lines** over the USB serial port: one JSON object per line, ending in `\n` (a trailing `\r` is ignored). Every message has a `"type"`. The app opens the port at 115200 baud, which the C3's native USB ignores.

When the app opens the port it sets **DTR and RTS both low**. On the C3's USB Serial/JTAG, RTS high holds the chip in reset and DTR high pulls the boot pin, so the app keeps both low. If the chip resets anyway when the port opens, the app sends an `info_request` 300 ms after opening; reply once you've booted.

The phone never builds or reads LoRa packets. The dongle turns a `send` into a DATA packet ([packet design](../design/packet-design.md), section 3) and reports its progress with `status`.

The app ignores every line that isn't a JSON object with a known `type`. The firmware's own logs start with `#` (readable) or `@` (JSON events), so they are harmless on the same port.

## App → dongle

### `send`: create an SOS

```json
{"type":"send","people":3,"flags":3,"lat":27.68,"lon":85.32,"alt":1300,"acc":10,"time":1761217205}
```

| Field | Format | DATA field |
| --- | --- | --- |
| `people` | integer 1–100 | `ppl` |
| `flags` | integer 0–31, bitfield | `flg` |
| `lat`, `lon` | number, decimal degrees (6 decimals), or `null` | `lat`, `lon` |
| `alt` | integer metres, or `null` | `alt` |
| `acc` | integer metres 0–254, or `null` | `acc` |
| `time` | integer Unix seconds (phone clock) | `ts` |

Flags bits: 0 Trapped, 1 Injured (the app's "Medical help"), 2 Fire, 3 Flood, 4 Water/Food. `3` = Trapped + Injured.

With no position fix the app sends `lat`, `lon`, `alt` and `acc` as `null`; the dongle then sends position 0, 0 with `acc` 255 (no fix).

The dongle assigns the source ID (its node ID), the message ID (its counter in flash), the destination (helper ID), the TTL and the path. The app sends none of these.

### `info_request`: ask for dongle info

```json
{"type":"info_request"}
```

Sent 300 ms after the port opens. The dongle answers with `info`.

## Dongle → app

### `status`: message progress

```json
{"type":"status","id":"2001-00042","state":"STORED"}
{"type":"status","id":"2001-00042","state":"SENT_TO_MESH","next_hop":"0x0011"}
```

- `id`: source ID as 4 hex digits, `-`, message ID as 5 decimal digits (same as the helper's PC output).
- `state`: one of `QUEUED`, `STORED`, `TRANSMITTING`, `SENT_TO_MESH`, `DELIVERED`, `WAITING`, `FAILED` ([algorithm design](../design/algorithm-design.md), section 8).
- `next_hop`: only with `SENT_TO_MESH`. The node that ACKed the DATA, as `0x` + 4 hex digits.

The first `status` after a `send` tells the app the new message's ID. After that the app only follows that ID and ignores lines about other messages still stored on the dongle.

| State | App shows |
| --- | --- |
| `QUEUED` | Saving to your dongle |
| `STORED` | Saved on your dongle, looking for a nearby node |
| `TRANSMITTING` | Sending to a nearby node |
| `WAITING` | Saved, waiting for the network (no infra node heard) |
| `SENT_TO_MESH` | Reached the network, via `next_hop` |
| `DELIVERED` | Rescue team received it (time = when the phone got the line) |
| `FAILED` | Did not reach the rescue team, with a Send again button |

### `info`: dongle info

```json
{"type":"info","battery":null,"nodes":2}
```

- `battery`: the dongle's battery percent 0–100, or `null`. The app currently ignores it and shows the phone's battery instead.
- `nodes`: number of infra nodes heard in the last 3 HELLO intervals (the dongle's candidate list: 3.5 min in deployment, 35 s with the demo HELLO interval).

Sent in answer to `info_request` and whenever the node count changes.

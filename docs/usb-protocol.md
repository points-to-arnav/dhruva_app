# Phone ↔ dongle USB protocol (V1)

The phone app (a web page in Chrome) and the dongle (ESP32-C3 Super Mini, built-in USB Serial/JTAG, VID `0x303A` PID `0x1001`) exchange **text lines** over the USB serial port. Each line is a message: fields separated by `|`, ending in `\n` (a trailing `\r` is ignored). The app opens the port at 115200 baud, which the C3's native USB ignores.

When the app opens the port it sets **DTR and RTS both low**. On the C3's USB Serial/JTAG, RTS high holds the chip in reset and DTR high pulls the boot pin, so the app keeps both low. If the chip resets anyway when the port opens, the app asks `INFO?` 300 ms after opening; reply once you've booted.

The phone never builds or reads LoRa packets. The dongle turns `SEND` into a DATA packet ([packet design](../design/packet-design.md), section 3) and reports its progress with `STATUS`.

Any line that doesn't start with a known keyword is ignored by the app, so firmware debug prints on the same port are harmless.

## App → dongle

### `SEND`: create an SOS

```
SEND|<people>|<flags>|<lat>|<lon>|<alt>|<acc>|<unix_time>
SEND|3|0x03|27.680000|85.320000|1300|10|1761217205
```

| Field | Format | DATA field |
| --- | --- | --- |
| people | integer 1–100 | `people` |
| flags | hex byte `0x00`–`0x1F` | `flags` |
| lat, lon | decimal degrees, 6 decimals | `latitude`, `longitude` (× 1e7 → int32) |
| alt | integer metres | `altitude` |
| acc | integer metres 0–254, `255` = no fix | `position_acc` |
| unix_time | integer Unix seconds (phone clock) | `timestamp` |

Flags bits: 0 Trapped, 1 Injured (the app's "Medical help"), 2 Fire, 3 Flood, 4 Water/Food.

With no fix the app sends `0.000000|0.000000|0|255`.

The dongle assigns `source_id` (its node ID), `message_id` (its counter in flash), `destination_id` (helper ID), `ttl` and the path. The app sends none of these.

### `INFO?`: ask for dongle info

Sent 300 ms after the port opens. The dongle answers with `INFO`.

## Dongle → app

### `STATUS`: message progress

```
STATUS|<message_id>|<state>[|<next_hop>]
STATUS|2001-00042|STORED
STATUS|2001-00042|SENT_TO_MESH|0x0011
```

- `message_id`: source ID as 4 hex digits, `-`, message_id as 5 decimal digits (same as the helper's PC output).
- `state`: one of `QUEUED`, `STORED`, `TRANSMITTING`, `SENT_TO_MESH`, `DELIVERED`, `WAITING`, `FAILED` ([algorithm design](../design/algorithm-design.md), section 9).
- `next_hop`: only with `SENT_TO_MESH`. The `ack_from` of the ACK, as `0x` + 4 hex digits.

The first `STATUS` after a `SEND` tells the app the new message's ID. After that the app only follows that ID and ignores lines about other messages still stored on the dongle.

| State | App shows |
| --- | --- |
| `QUEUED` | Saving to your dongle |
| `STORED` | Saved on your dongle, looking for a nearby node |
| `TRANSMITTING` | Sending to a nearby node |
| `WAITING` | Saved, waiting for the network (no infra node heard) |
| `SENT_TO_MESH` | Reached the network, via `next_hop` |
| `DELIVERED` | Rescue team received it (time = when the phone got the line) |
| `FAILED` | Did not reach the rescue team, with a Send again button |

### `INFO`: dongle info

```
INFO|<battery>|<nodes>
INFO|78|2
```

- `battery`: the dongle's battery percent 0–100, or `-`. The app currently ignores it and shows the phone's battery instead.
- `nodes`: number of infra nodes heard in the last 3 HELLO intervals (the dongle's candidate list: 3 min in deployment, 30 s with the demo superframe).

Send it in answer to `INFO?` and whenever the node count changes.

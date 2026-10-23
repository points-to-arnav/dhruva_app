# Dhruva

Phone app for the Disaster Mesh SOS dongle (ESP32-C3 over USB-C, USB CDC). Built with Expo (SDK 58), Expo Router and TypeScript. Android first.

## Run it

```bash
npm install
npx expo start
```

Check with `npm run typecheck` and `npm run lint`.

Right now the app talks to a **mock dongle**, so it runs in Expo Go on any phone or emulator. Scan the QR code from `npx expo start`.

The real USB link needs a native module, so it needs a development build (`npx expo run:android` or `eas build --profile development`), not Expo Go.

## Screens

| Route | States |
| --- | --- |
| `src/app/index.tsx` | Ready, Location not found, No dongle |
| `src/app/countdown.tsx` | 5-second cancel window |
| `src/app/status.tsx` | Saving, Saved, Waiting for the network, Reached the network, Delivered, Failed |

## Mock dongle controls

- **Long-press "Dongle connected"** to unplug or replug the mock dongle (shows the No dongle screen).
- **Long-press "Battery 78%"** (any screen) to drop or restore the mesh nodes. With none, a sent SOS goes to WAITING and continues once the nodes are back (demo script steps 2 and 3).
- **Long-press the SOS banner** (1 s) on the status screen to start over.
- After you send, the mock goes QUEUED, STORED, TRANSMITTING (1.5 s), SENT_TO_MESH via `0x0011` (3 s), then DELIVERED (6.5 s).

## Where things live

- `docs/usb-protocol.md`: the phone ↔ dongle text protocol (`SEND`, `INFO?`, `STATUS`, `INFO`). The firmware has to speak it.
- `src/serial/protocol.ts`: encodes `SEND` and parses dongle lines.
- `src/serial/types.ts`: the `SerialLink` interface (a line-based serial port).
- `src/serial/mockLink.ts`: a mock that behaves like the dongle firmware.
- `src/serial/index.ts`: picks the link. The USB CDC link goes here.
- `src/config.ts`: `DEMO_POSITION`, the preconfigured victim position for the live demo. Set it to `null` to use the phone's GPS.
- `src/state/SosContext.tsx`: app state (dongle info, picks, message progress).

The packet format is in packet_v2 (rev 4). Flags and people come from the screen; "Medical help" sets the Injured bit.

## Hardware notes

- The ESP32-C3's built-in USB shows up as CDC-ACM, VID `0x303A`, PID `0x1001`. The baud rate is ignored.
- Firmware: Arduino needs **USB CDC On Boot: Enabled**; with ESP-IDF, use the `usb_serial_jtag` driver.
- Opening the port can reset the C3 through DTR/RTS. Check it doesn't reboot when the app connects.
- If the board gets no power over a C-to-C cable, it's probably missing the CC pull-down resistors. Use an OTG adapter and an A-to-C cable.
- While the dongle is plugged in, debug the app over Wi-Fi: `adb pair` / `adb connect`.

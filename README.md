# Dhruva

Phone app for the Disaster Mesh SOS dongle (ESP32-C3 over USB-C, USB CDC). Built with Expo (SDK 58), Expo Router and TypeScript. Android first.

## Run it

```bash
npm install
npx expo start
```

Right now the app talks to a **mock dongle**, so it runs in Expo Go on any phone or emulator. Scan the QR code from `npx expo start`.

The real USB link needs a native module, so it needs a development build (`npx expo run:android` or `eas build --profile development`), not Expo Go.

## Screens

| Route | States |
| --- | --- |
| `src/app/index.tsx` | Ready, Location not found, No dongle |
| `src/app/countdown.tsx` | 5-second cancel window |
| `src/app/status.tsx` | Sending (saved, reaching the network), Delivered |

## Mock dongle controls

- **Long-press "Dongle connected"** to unplug or replug the mock dongle (shows the No dongle screen).
- **Long-press the SOS banner** (1 s) on the status screen to start over.
- After you send, the mock reports *saved* at 0.5 s, *reached the network* at 3 s and *delivered* at 6.5 s.

## Where things live

- `src/serial/types.ts`: the `DongleTransport` interface and the events the dongle sends back.
- `src/serial/mockTransport.ts`: the mock.
- `src/serial/protocol.ts`: the SOS packet. **The live demo sends a preconfigured packet.** Replace `DEMO_SOS_PACKET` with the one from the packet design.
- `src/serial/index.ts`: picks the transport. The USB CDC transport goes here.
- `src/state/SosContext.tsx`: app state (dongle status, picks, SOS progress).

## Hardware notes

- The ESP32-C3's built-in USB shows up as CDC-ACM, VID `0x303A`, PID `0x1001`. The baud rate is ignored.
- Firmware: Arduino needs **USB CDC On Boot: Enabled**; with ESP-IDF, use the `usb_serial_jtag` driver.
- Opening the port can reset the C3 through DTR/RTS. Check it doesn't reboot when the app connects.
- If the board gets no power over a C-to-C cable, it's probably missing the CC pull-down resistors. Use an OTG adapter and an A-to-C cable.
- While the dongle is plugged in, debug the app over Wi-Fi: `adb pair` / `adb connect`.

# Dhruva

SOS web app for the Disaster Mesh dongle. It runs in **Chrome on an Android phone** and talks to the dongle, an ESP32-C3 Super Mini, over USB-C (the C3's built-in USB Serial/JTAG, CDC-ACM). Built with React, Vite and Tailwind CSS.

## Run it on the phone (localhost)

Chrome only allows USB on secure pages: `https://` or `http://localhost`. The phone reaches the PC's localhost through `adb reverse` over a USB cable.

1. On the PC:
   ```bash
   npm install
   npm run dev
   ```
2. Plug the phone into the PC (USB debugging on), then:
   ```bash
   adb reverse tcp:5173 tcp:5173
   ```
3. On the phone, open **Chrome** at `http://localhost:5173`.
4. Unplug the PC cable and plug in the dongle. The page keeps running. Tap **Connect dongle**, pick the dongle, then tap **Allow**.

The phone has one USB-C port, so the page has to be loaded before the dongle goes in. If you reload the page, plug the PC back in and run `adb reverse` again. For the demo, `npm run build` then `npm run preview` serves the same app on port 5173 without the dev tools.

On a laptop, desktop Chrome works too: open `http://localhost:5173` with the dongle plugged into the laptop.

## Try it without the dongle

Open `http://localhost:5173/?mock`. A simulated dongle connects by itself:

- **Long-press "Dongle connected"** to unplug or replug it.
- **Long-press "Battery …%"** to drop or restore the mesh nodes. With none, an SOS goes to WAITING and continues once they're back (demo script steps 2 and 3).
- **Long-press the SOS banner** on the status screen to start over.

## Screens

| Screen | States |
| --- | --- |
| `src/screens/Home.jsx` | Ready, Location not found, No dongle (with Connect dongle), browser can't use USB |
| `src/screens/Countdown.jsx` | 5-second cancel window. The phone's back button cancels it. |
| `src/screens/Status.jsx` | Saving, Saved, Waiting for the network, Reached the network, Delivered, Failed |

## Where things live

- `design/`: the mesh design: plan, LoRa packet formats, routing and channel-access algorithm.
- `docs/usb-protocol.md`: the phone ↔ dongle text protocol (`SEND`, `INFO?`, `STATUS`, `INFO`). The firmware has to speak it.
- `src/serial/webSerialLink.js`: the USB link. On Android it uses WebUSB through [`web-serial-polyfill`](https://github.com/google/web-serial-polyfill); on desktop, the Web Serial API. Add `?serial=native` or `?serial=webusb` to force one.
- `src/serial/protocol.js`: encodes `SEND` and parses dongle lines.
- `src/serial/mockLink.js`: the simulated dongle used by `?mock`.
- `src/config.js`: `DEMO_POSITION`, the preconfigured victim position for the live demo (set it to `null` to use the phone's GPS), and the dongle's USB IDs.
- `src/state/SosContext.jsx`: app state (dongle, picks, message progress, phone battery, position).

The LoRa packet format is in `design/packet-design.md` (rev 5). Flags and people come from the screen; "Medical help" sets the Injured bit. The battery shown is the phone's (Battery Status API).

## Checks

```bash
npm run lint
npm run build
```

## Hardware notes

- Firmware: Arduino needs **USB CDC On Boot: Enabled**; with ESP-IDF, use the `usb_serial_jtag` driver.
- The app keeps DTR and RTS low, so opening the port shouldn't reset the C3. Check on the real board.
- If the board gets no power over a C-to-C cable, it's probably missing the CC pull-down resistors. Use an OTG adapter and an A-to-C cable.

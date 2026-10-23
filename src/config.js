// Live demo: the victim position is preconfigured, because GPS won't get a
// fix indoors. Set to null to use the phone's GPS instead.
// PLACEHOLDER values from packet_v2 section 12; swap in the Nepal 2024
// flood-area coordinates used by the sim.
export const DEMO_POSITION = {
  latitude: 27.68,
  longitude: 85.32,
  altitude: 1300, // metres
  accuracy: 10, // metres
};

// TEMPORARY: until the firmware sends STATUS lines, the status screen fakes
// its steps. Once the SEND line has gone out over USB, it moves to the next
// step (saved, reached the network, delivered) after a random 2-3 s each,
// and STATUS lines from the dongle are ignored. Set to false to follow the
// dongle's real STATUS replies. Not used with ?mock, which simulates its own.
export const FAKE_STATUS_PROGRESS = true;
export const FAKE_STEP_DELAY_MS = { min: 2000, max: 3000 };

// The ESP32-C3's built-in USB Serial/JTAG controller.
export const DONGLE_USB_FILTER = { usbVendorId: 0x303a, usbProductId: 0x1001 };

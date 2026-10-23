// Live demo: the victim position is preconfigured, because GPS won't get a
// fix indoors. Set to null to use the phone's GPS instead.
// PLACEHOLDER values from the demo plan (design/plan.md, section 8); swap in the Nepal 2024
// flood-area coordinates used by the sim.
export const DEMO_POSITION = {
  latitude: 27.68,
  longitude: 85.32,
  altitude: 1300, // metres
  accuracy: 10, // metres
};

// TEMPORARY: until the firmware sends status lines, the status screen fakes
// its steps. Once the SEND line has gone out over USB, it moves to the next
// step (saved, reached the network, delivered) after a random 2-3 s each,
// and status lines from the dongle are ignored. Set to false to follow the
// dongle's real status replies. Not used with ?mock, which simulates its own.
export const FAKE_STATUS_PROGRESS = true;
export const FUNCTION_DELAY_XYZ = { min: 2000, max: 3000 };

// The ESP32-C3's built-in USB Serial/JTAG controller.
export const DONGLE_USB_FILTER = { usbVendorId: 0x303a, usbProductId: 0x1001 };

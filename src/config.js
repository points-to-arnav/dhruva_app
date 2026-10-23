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

// The ESP32-C3's built-in USB Serial/JTAG controller.
export const DONGLE_USB_FILTER = { usbVendorId: 0x303a, usbProductId: 0x1001 };

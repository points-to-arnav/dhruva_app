import { MockLink } from './mockLink';
import type { SerialLink } from './types';

export type { DongleEvent, LinkEvent, SerialLink } from './types';
export * from './protocol';

// The USB CDC link for the ESP32-C3 (VID 0x303A, PID 0x1001) slots in here
// once it exists; it needs a development build, not Expo Go.
export function createLink(): SerialLink {
  return new MockLink();
}

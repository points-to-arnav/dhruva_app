import { MockTransport } from './mockTransport';
import type { DongleTransport } from './types';

export type { DongleEvent, DongleTransport } from './types';
export { buildSosPacket } from './protocol';

// The USB CDC transport for the ESP32-C3 (VID 0x303A, PID 0x1001) slots in
// here once it exists; it needs a development build, not Expo Go.
export function createTransport(): DongleTransport {
  return new MockTransport();
}

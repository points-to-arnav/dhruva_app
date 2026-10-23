import type { DongleEvent, DongleListener, DongleTransport } from './types';

// Pretends to be a dongle: connects shortly after start, then walks a sent
// SOS through saved -> reached the network -> delivered.
export class MockTransport implements DongleTransport {
  readonly isMock = true;
  private listeners = new Set<DongleListener>();
  private timers: ReturnType<typeof setTimeout>[] = [];
  private connected = false;
  private battery = 78;
  private requestCount = 41;

  start() {
    this.later(600, () => this.setConnected(true));
  }

  stop() {
    this.timers.forEach(clearTimeout);
    this.timers = [];
  }

  subscribe(listener: DongleListener) {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  async send(_packet: Uint8Array) {
    if (!this.connected) throw new Error('Dongle not connected');
    this.requestCount += 1;
    const requestId = `N001-${String(this.requestCount).padStart(3, '0')}`;
    this.later(500, () => this.emit({ type: 'saved', requestId }));
    this.later(3000, () => this.emit({ type: 'network', via: 'N003', hops: 2 }));
    this.later(6500, () => {
      this.battery -= 2;
      this.emitStatus();
      this.emit({ type: 'delivered', at: new Date() });
    });
  }

  // Demo control: long-press the dongle status to unplug or replug.
  toggleConnection() {
    this.setConnected(!this.connected);
  }

  private setConnected(connected: boolean) {
    this.connected = connected;
    this.emit({ type: 'connection', connected });
    if (connected) this.emitStatus();
  }

  private emitStatus() {
    this.emit({ type: 'status', battery: this.battery, nodesInRange: 2 });
  }

  private emit(event: DongleEvent) {
    this.listeners.forEach((listener) => listener(event));
  }

  private later(ms: number, fn: () => void) {
    this.timers.push(setTimeout(fn, ms));
  }
}

import type { LinkEvent, SerialLink } from './types';

const DONGLE_ID = '2001';
const NEXT_HOP = '0x0011';

// Pretends to be the dongle firmware: answers INFO? and walks each SEND
// through the v2 message states. With no infra node heard, a message sits
// in WAITING until one shows up (demo script steps 2-3).
export class MockLink implements SerialLink {
  private listeners = new Set<(event: LinkEvent) => void>();
  private timers: ReturnType<typeof setTimeout>[] = [];
  private connected = false;
  private battery = 78;
  private nodes = 2;
  private counter = 41;
  private waiting: string[] = [];

  readonly mock = {
    toggleConnection: () => this.setConnected(!this.connected),
    toggleNodes: () => {
      this.nodes = this.nodes > 0 ? 0 : 2;
      this.sendInfo();
      if (this.nodes > 0) this.waiting.splice(0).forEach((id) => this.transmit(id));
    },
  };

  start() {
    this.later(600, () => this.setConnected(true));
  }

  stop() {
    this.timers.forEach(clearTimeout);
    this.timers = [];
  }

  subscribe(listener: (event: LinkEvent) => void) {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  async writeLine(line: string) {
    if (!this.connected) throw new Error('Dongle not connected');
    if (line === 'INFO?') {
      this.later(50, () => this.sendInfo());
      return;
    }
    if (!line.startsWith('SEND|')) return;
    this.counter += 1;
    const id = `${DONGLE_ID}-${String(this.counter).padStart(5, '0')}`;
    this.later(150, () => this.status(id, 'QUEUED'));
    this.later(500, () => {
      this.status(id, 'STORED');
      if (this.nodes > 0) this.transmit(id);
      else this.later(800, () => {
        this.status(id, 'WAITING');
        this.waiting.push(id);
      });
    });
  }

  private transmit(id: string) {
    this.later(1000, () => this.status(id, 'TRANSMITTING'));
    this.later(2500, () => this.status(id, `SENT_TO_MESH|${NEXT_HOP}`));
    this.later(6000, () => {
      this.battery -= 2;
      this.sendInfo();
      this.status(id, 'DELIVERED');
    });
  }

  private setConnected(connected: boolean) {
    this.connected = connected;
    this.emit({ type: 'connection', connected });
  }

  private sendInfo() {
    if (this.connected) this.emit({ type: 'line', line: `INFO|${this.battery}|${this.nodes}` });
  }

  private status(id: string, rest: string) {
    if (this.connected) this.emit({ type: 'line', line: `STATUS|${id}|${rest}` });
  }

  private emit(event: LinkEvent) {
    this.listeners.forEach((listener) => listener(event));
  }

  private later(ms: number, fn: () => void) {
    this.timers.push(setTimeout(fn, ms));
  }
}

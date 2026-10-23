const DONGLE_ID = '2001';
const NEXT_HOP = '0x0011';

// Pretends to be the dongle firmware: answers info requests and walks each
// send through the message states, as JSON lines. With no infra node heard, a message sits
// in WAITING until one shows up (demo script steps 2-3). Open the app with
// ?mock to use it.
export class MockLink {
  supported = true;
  unsupportedReason = null;
  listeners = new Set();
  timers = [];
  connected = false;
  nodes = 2;
  counter = 41;
  waiting = [];

  mock = {
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

  subscribe(listener) {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  async connect() {
    this.setConnected(true);
  }

  async writeLine(line) {
    if (!this.connected) throw new Error('Dongle not connected');
    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      return;
    }
    if (msg.type === 'info_request') {
      this.later(50, () => this.sendInfo());
      return;
    }
    if (msg.type !== 'send') return;
    this.counter += 1;
    const id = `${DONGLE_ID}-${String(this.counter).padStart(5, '0')}`;
    this.later(150, () => this.status(id, 'QUEUED'));
    this.later(500, () => {
      this.status(id, 'STORED');
      if (this.nodes > 0) this.transmit(id);
      else
        this.later(800, () => {
          this.status(id, 'WAITING');
          this.waiting.push(id);
        });
    });
  }

  transmit(id) {
    this.later(1000, () => this.status(id, 'TRANSMITTING'));
    this.later(2500, () => this.status(id, 'SENT_TO_MESH', NEXT_HOP));
    this.later(6000, () => this.status(id, 'DELIVERED'));
  }

  setConnected(connected) {
    this.connected = connected;
    this.emit({ type: 'connection', connected });
  }

  sendInfo() {
    this.reply({ type: 'info', battery: null, nodes: this.nodes });
  }

  status(id, state, nextHop) {
    this.reply({ type: 'status', id, state, ...(nextHop && { next_hop: nextHop }) });
  }

  reply(msg) {
    if (this.connected) this.emit({ type: 'line', line: JSON.stringify(msg) });
  }

  emit(event) {
    this.listeners.forEach((listener) => listener(event));
  }

  later(ms, fn) {
    this.timers.push(setTimeout(fn, ms));
  }
}

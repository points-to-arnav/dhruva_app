// What the dongle (ESP32-C3 over USB CDC) reports back to the phone.
export type DongleEvent =
  | { type: 'connection'; connected: boolean }
  | { type: 'status'; battery: number; nodesInRange: number }
  | { type: 'saved'; requestId: string }
  | { type: 'network'; via: string; hops: number }
  | { type: 'delivered'; at: Date };

export type DongleListener = (event: DongleEvent) => void;

// One implementation per link: the mock today, USB serial next.
export interface DongleTransport {
  readonly isMock: boolean;
  start(): void;
  stop(): void;
  subscribe(listener: DongleListener): () => void;
  send(packet: Uint8Array): Promise<void>;
}

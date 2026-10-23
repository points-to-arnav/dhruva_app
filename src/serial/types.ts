import type { MessageState } from './protocol';

// Raw link to the dongle: a USB CDC serial port carrying text lines.
export type LinkEvent = { type: 'connection'; connected: boolean } | { type: 'line'; line: string };

export interface SerialLink {
  start(): void;
  stop(): void;
  subscribe(listener: (event: LinkEvent) => void): () => void;
  writeLine(line: string): Promise<void>;
  // Only the mock has these: demo controls for unplugging things.
  readonly mock?: { toggleConnection(): void; toggleNodes(): void };
}

// A parsed line from the dongle.
export type DongleEvent =
  | { type: 'info'; battery: number | null; nodesInRange: number }
  | { type: 'status'; messageId: string; state: MessageState; nextHop: string | null };

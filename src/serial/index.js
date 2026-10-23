import { MockLink } from './mockLink';
import { WebSerialLink } from './webSerialLink';

export * from './protocol';

// `?mock` in the URL swaps the dongle for a simulated one.
export function createLink() {
  const mock = new URLSearchParams(window.location.search).has('mock');
  return mock ? new MockLink() : new WebSerialLink();
}

import type { SosRequest } from '../state/sos';

// PLACEHOLDER: replace with the preconfigured demo packet from the Hack-octo
// packet design. The live demo sends this fixed payload whatever the user
// picks; the picks only drive what the screens show.
const DEMO_SOS_PACKET = 'DHRUVA_SOS_DEMO\n';

export function buildSosPacket(_request: SosRequest): Uint8Array {
  return new TextEncoder().encode(DEMO_SOS_PACKET);
}

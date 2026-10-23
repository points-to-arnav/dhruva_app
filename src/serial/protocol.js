// Phone <-> dongle USB protocol: one JSON object per line, ending in '\n'.
// Spec: docs/usb-protocol.md. The phone never builds LoRa packets; the
// dongle turns a "send" into a DATA packet.

export const MESSAGE_STATES = [
  'QUEUED',
  'STORED',
  'TRANSMITTING',
  'SENT_TO_MESH',
  'DELIVERED',
  'WAITING',
  'FAILED',
];

// DATA flags bitfield, packet design section 3 (design/packet-design.md). "Medical help" is the Injured bit.
export const FLAG_BITS = {
  Trapped: 0,
  'Medical help': 1,
  Fire: 2,
  Flood: 3,
  'Need water or food': 4,
};

// DATA position_acc: metres, capped at 254.
const MAX_ACCURACY = 254;

export const INFO_REQUEST = JSON.stringify({ type: 'info_request' });

export function encodeFlags(needs) {
  return needs.reduce((bits, need) => bits | (1 << FLAG_BITS[need]), 0);
}

// {"type":"send","people":3,"flags":3,"lat":27.68,"lon":85.32,"alt":1300,"acc":10,"time":1761217205}
// position: { latitude, longitude, altitude, accuracy } or null for no fix,
// which sends lat / lon / alt / acc as null.
export function encodeSend({ people, needs, position, time }) {
  const accuracy =
    position?.accuracy == null ? MAX_ACCURACY : Math.min(MAX_ACCURACY, Math.max(0, Math.round(position.accuracy)));
  return JSON.stringify({
    type: 'send',
    people,
    flags: encodeFlags(needs),
    lat: position ? Number(position.latitude.toFixed(6)) : null,
    lon: position ? Number(position.longitude.toFixed(6)) : null,
    alt: position ? Math.round(position.altitude) : null,
    acc: position ? accuracy : null,
    time: Math.floor(time.getTime() / 1000),
  });
}

// Returns null for anything that isn't a protocol line (e.g. firmware logs
// on the same port, which start with '#' or '@'), so those are ignored.
export function parseLine(raw) {
  const text = raw.trim();
  if (!text.startsWith('{')) return null;
  let msg;
  try {
    msg = JSON.parse(text);
  } catch {
    return null;
  }
  switch (msg?.type) {
    case 'status':
      if (typeof msg.id !== 'string' || !MESSAGE_STATES.includes(msg.state)) return null;
      return { type: 'status', messageId: msg.id, state: msg.state, nextHop: msg.next_hop ?? null };
    case 'info':
      if (!Number.isInteger(msg.nodes)) return null;
      return { type: 'info', nodesInRange: msg.nodes };
  }
  return null;
}

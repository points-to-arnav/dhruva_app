// Phone <-> dongle USB protocol: one '|'-separated text line per message,
// ending in '\n'. Spec: docs/usb-protocol.md. The phone never builds LoRa
// packets; the dongle turns SEND into a DATA packet.

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

// DATA position_acc: uint8 metres capped at 254, 0xFF = no fix.
export const NO_FIX_ACCURACY = 255;
const MAX_ACCURACY = 254;

export const INFO_REQUEST = 'INFO?';

export function encodeFlags(needs) {
  const value = needs.reduce((bits, need) => bits | (1 << FLAG_BITS[need]), 0);
  return `0x${value.toString(16).toUpperCase().padStart(2, '0')}`;
}

// SEND|people|flags|lat|lon|alt|acc|unix_time
// position: { latitude, longitude, altitude, accuracy } or null for no fix.
export function encodeSend({ people, needs, position, time }) {
  const unix = Math.floor(time.getTime() / 1000);
  const fields = position
    ? [
        position.latitude.toFixed(6),
        position.longitude.toFixed(6),
        Math.round(position.altitude),
        position.accuracy == null ? MAX_ACCURACY : Math.min(MAX_ACCURACY, Math.max(0, Math.round(position.accuracy))),
      ]
    : ['0.000000', '0.000000', 0, NO_FIX_ACCURACY];
  return ['SEND', people, encodeFlags(needs), ...fields, unix].join('|');
}

// Returns null for anything that isn't a protocol line (e.g. firmware logs
// on the same port), so those are ignored.
export function parseLine(raw) {
  const parts = raw.trim().split('|');
  switch (parts[0]) {
    case 'STATUS': {
      const [, messageId, state, nextHop] = parts;
      if (!messageId || !MESSAGE_STATES.includes(state)) return null;
      return { type: 'status', messageId, state, nextHop: nextHop || null };
    }
    case 'INFO': {
      const nodes = parseInt(parts[2], 10);
      if (Number.isNaN(nodes)) return null;
      return { type: 'info', nodesInRange: nodes };
    }
  }
  return null;
}

import type { Position } from './serial/protocol';

// Live demo: the victim position is preconfigured, because GPS won't get a
// fix indoors. Set to null to use the phone's GPS instead.
// PLACEHOLDER values from packet_v2 section 12; swap in the Nepal 2024
// flood-area coordinates used by the sim.
export const DEMO_POSITION: Position | null = {
  latitude: 27.68,
  longitude: 85.32,
  altitude: 1300,
  accuracy: 10,
};

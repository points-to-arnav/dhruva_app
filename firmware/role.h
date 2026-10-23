// What this board is. The same firmware goes on every module: set exactly
// one of these to 1 before flashing. Design: design/algorithm-design.md §1.
#pragma once

#define IS_DONGLE 0  // plugged into the phone: originates SOS messages, never relays
#define IS_RELAY  1  // balloon or ground node: stores and forwards (balloon/ground: `set role`)
#define IS_HELPER 0  // rescue base: final stop, prints messages to the PC, sends DELIVERY

#if (IS_DONGLE + IS_RELAY + IS_HELPER) != 1
#error "role.h: set exactly one of IS_DONGLE, IS_RELAY, IS_HELPER to 1"
#endif

// Only relays carry the NEO-6M.
#define HAS_GPS IS_RELAY

// role_flags bits 0-2 (packet design, common conventions)
#define ROLE_GROUND  0
#define ROLE_HELPER  1
#define ROLE_DONGLE  2
#define ROLE_BALLOON 3

// Demo IDs, used until `set id` is saved.
#if IS_HELPER
#define DEFAULT_NODE_ID 0x0001
#elif IS_RELAY
#define DEFAULT_NODE_ID 0x0011
#else
#define DEFAULT_NODE_ID 0x2001
#endif

// Next-hop selection (design/algorithm-design.md §5).
#pragma once
#include <Arduino.h>

// The best next hop not in path[] and not in failed[], or 0 (ID_NONE) if
// there is none: the message then waits.
uint16_t route_next_hop(const uint16_t* path, uint8_t path_n, const uint16_t* failed, uint8_t n_failed,
                        int64_t now);

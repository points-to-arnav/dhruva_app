// Helper only: the readable DATA block for the PC, one per new message
// (design/plan.md §5, "Logs").
#pragma once
#include "packet.h"
#include "radio.h"

void report_data(const DataPkt& p, const RxFrame& f);

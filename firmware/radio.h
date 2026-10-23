// SX1278 driver on top of the LoRa library (design/plan.md §5, "Using the
// Sandeep Mistry LoRa library"). The DIO0 interrupt only records a flag and a
// timestamp; all SPI work happens in the main loop.
#pragma once
#include <Arduino.h>

#define NUM_CHANNELS 8
#define CH_CONTROL 0

struct RxFrame {
  uint8_t buf[64];
  int len;
  int rssi;
  float snr;
  int64_t t_rx_us; // RxDone interrupt time
  uint8_t ch;
  bool crc_err;    // hardware CRC failed
};

bool radio_begin();
void radio_set_txp(int dbm);
uint32_t ch_freq_hz(uint8_t ch);
uint8_t home_ch(uint16_t id);   // packet design §7
uint8_t radio_channel();

// Continuous RX on ch (no-op if already there).
void radio_rx(uint8_t ch);
// Channel Activity Detection on ch. True if a preamble was seen (or CAD timed out).
bool radio_cad_busy(uint8_t ch);
// Blocking TX on ch, then continuous RX on the same channel. Returns the TX start time.
int64_t radio_tx(uint8_t ch, const uint8_t* buf, int len);
// True when a packet (or a hardware CRC failure) came in; fills f.
bool radio_poll(RxFrame& f);

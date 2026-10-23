#include "radio.h"
#include <SPI.h>
#include <LoRa.h>
#include <esp_timer.h>
#include "pins.h"
#include "mesh_config.h"

// SX127x registers the LoRa library doesn't expose (CAD result).
#define REG_OP_MODE 0x01
#define REG_IRQ_FLAGS 0x12
#define REG_DIO_MAPPING_1 0x40
#define MODE_LONG_RANGE 0x80
#define MODE_CAD 0x07
#define IRQ_CAD_DONE 0x04
#define IRQ_CAD_DETECTED 0x01
#define DIO0_CAD_DONE 0x80
#define CAD_TIMEOUT_US 50000

enum RadioMode : uint8_t { RM_IDLE, RM_RX };

static volatile bool s_irq = false;
static volatile int64_t s_irq_us = 0;
static uint8_t s_ch = 0xFF;
static RadioMode s_mode = RM_IDLE;
static const SPISettings s_spi(8000000, MSBFIRST, SPI_MODE0); // the LoRa library's default

static void IRAM_ATTR on_dio0() {
  s_irq_us = esp_timer_get_time();
  s_irq = true;
}

static uint8_t reg_read(uint8_t addr) {
  SPI.beginTransaction(s_spi);
  digitalWrite(PIN_LORA_NSS, LOW);
  SPI.transfer(addr & 0x7F);
  uint8_t v = SPI.transfer(0x00);
  digitalWrite(PIN_LORA_NSS, HIGH);
  SPI.endTransaction();
  return v;
}

static void reg_write(uint8_t addr, uint8_t v) {
  SPI.beginTransaction(s_spi);
  digitalWrite(PIN_LORA_NSS, LOW);
  SPI.transfer(addr | 0x80);
  SPI.transfer(v);
  digitalWrite(PIN_LORA_NSS, HIGH);
  SPI.endTransaction();
}

uint32_t ch_freq_hz(uint8_t ch) { return 433175000UL + 200000UL * ch; }

uint8_t home_ch(uint16_t id) {
  uint8_t mix = (id & 0xFF) ^ (id >> 8);
  return 1 + (mix % 7);
}

uint8_t radio_channel() { return s_ch; }

// The SX1278 takes a new frequency in standby; the caller restores the mode.
static void set_channel(uint8_t ch) {
  if (ch == s_ch) return;
  LoRa.idle();
  s_mode = RM_IDLE;
  LoRa.setFrequency(ch_freq_hz(ch));
  s_ch = ch;
}

bool radio_begin() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  LoRa.setSPI(SPI);
  LoRa.setPins(PIN_LORA_NSS, PIN_LORA_RST, PIN_LORA_DIO0);
  if (!LoRa.begin(ch_freq_hz(CH_CONTROL))) return false;
  s_ch = CH_CONTROL;
  LoRa.setSpreadingFactor(9);
  LoRa.setSignalBandwidth(125E3);
  LoRa.setCodingRate4(5);
  LoRa.setPreambleLength(8);
  LoRa.setSyncWord(0x12);
  LoRa.enableCrc();
  LoRa.disableInvertIQ();
  radio_set_txp(cfg.txp_dbm);
  pinMode(PIN_LORA_DIO0, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_LORA_DIO0), on_dio0, RISING);
  radio_rx(CH_CONTROL);
  return true;
}

void radio_set_txp(int dbm) {
  if (dbm < 2) dbm = 2;
  if (dbm > 20) dbm = 20;
  LoRa.setTxPower(dbm); // PA_BOOST
}

void radio_rx(uint8_t ch) {
  if (s_mode == RM_RX && s_ch == ch) return;
  set_channel(ch);
  s_irq = false;
  LoRa.receive(); // maps DIO0 to RxDone
  s_mode = RM_RX;
}

bool radio_cad_busy(uint8_t ch) {
  set_channel(ch);
  LoRa.idle();
  s_mode = RM_IDLE;
  reg_write(REG_IRQ_FLAGS, 0xFF);
  reg_write(REG_DIO_MAPPING_1, DIO0_CAD_DONE);
  reg_write(REG_OP_MODE, MODE_LONG_RANGE | MODE_CAD);
  // DIO1 (CadDetected) isn't wired, so poll the IRQ register (~2 symbols, ~8 ms).
  int64_t t0 = esp_timer_get_time();
  uint8_t flags = 0;
  while (!(flags & IRQ_CAD_DONE) && esp_timer_get_time() - t0 < CAD_TIMEOUT_US) flags = reg_read(REG_IRQ_FLAGS);
  reg_write(REG_IRQ_FLAGS, 0xFF);
  radio_rx(ch);
  if (!(flags & IRQ_CAD_DONE)) return true; // no answer: don't risk a collision
  return flags & IRQ_CAD_DETECTED;
}

int64_t radio_tx(uint8_t ch, const uint8_t* buf, int len) {
  set_channel(ch);
  digitalWrite(PIN_LED, LOW);
  LoRa.beginPacket();
  LoRa.write(buf, len);
  int64_t t_start = esp_timer_get_time();
  LoRa.endPacket(); // blocking until TxDone (<= 330 ms)
  digitalWrite(PIN_LED, HIGH);
  s_mode = RM_IDLE;
  radio_rx(ch);
  return t_start;
}

bool radio_poll(RxFrame& f) {
  if (!s_irq) return false;
  noInterrupts();
  int64_t t = s_irq_us;
  s_irq = false;
  interrupts();
  if (s_mode != RM_RX) return false;
  f.t_rx_us = t;
  f.ch = s_ch;
  f.len = 0;
  f.crc_err = false;
  // Only call parsePacket() after RxDone: on an idle continuous RX it would
  // switch the radio to single RX.
  int n = LoRa.parsePacket();
  if (n <= 0) {
    f.crc_err = true;
  } else {
    while (LoRa.available() && f.len < (int)sizeof f.buf) f.buf[f.len++] = LoRa.read();
    f.rssi = LoRa.packetRssi();
    f.snr = LoRa.packetSnr();
  }
  s_mode = RM_IDLE; // parsePacket() left the radio in standby or single RX
  radio_rx(s_ch);
  return true;
}

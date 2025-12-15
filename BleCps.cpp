#include "BleCps.h"
#include <NimBLEDevice.h>

// Cycling Power Service and characteristics
static NimBLECharacteristic* ch_measurement = nullptr;

// UUIDs
static const uint16_t CPS_UUID16 = 0x1818; // Cycling Power Service
static const uint16_t CPM_UUID16 = 0x2A63; // Cycling Power Measurement (Notify)
static const uint16_t CPF_UUID16 = 0x2A65; // Cycling Power Feature (Read)
static const uint16_t CSL_UUID16 = 0x2A5D; // Sensor Location (Read)

static inline void put_u16_le(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void put_s16_le(uint8_t* p, int16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

class ServerCB : public NimBLEServerCallbacks {
  void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
    NimBLEDevice::startAdvertising();
  }
};

void BleCps::begin(const char* deviceName) {
  NimBLEDevice::init(deviceName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);

  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  NimBLEService* cps = server->createService(NimBLEUUID((uint16_t)CPS_UUID16));

  // Cycling Power Feature: 32-bit bitfield. 0 = only mandatory instantaneous power.
  NimBLECharacteristic* ch_feature =
      cps->createCharacteristic(NimBLEUUID((uint16_t)CPF_UUID16), NIMBLE_PROPERTY::READ);
  uint8_t feature_val[4] = {0x08, 0, 0, 0}; // Bit 3: Crank Revolution Data Supported
  ch_feature->setValue(feature_val, sizeof(feature_val));

  // Sensor Location (optional). 13 = Rear Hub (implies total power, not single-sided)
  NimBLECharacteristic* ch_location =
      cps->createCharacteristic(NimBLEUUID((uint16_t)CSL_UUID16), NIMBLE_PROPERTY::READ);
  uint8_t loc = 13;
  ch_location->setValue(&loc, 1);

  // Measurement (Notify)
  ch_measurement =
      cps->createCharacteristic(NimBLEUUID((uint16_t)CPM_UUID16), NIMBLE_PROPERTY::NOTIFY);

  cps->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NimBLEUUID((uint16_t)CPS_UUID16));
  // adv->setScanResponse(true); // Removed in NimBLE-Arduino 2.x
  adv->start();

  started = true;
}

void BleCps::notify(const PowerSample& s) {
  if (!started || ch_measurement == nullptr) return;

  // Build CPS measurement:
  // Flags (uint16) + Instantaneous Power (sint16) + Crank Rev Data (uint16 + uint16)
  // Flags: bit5 set = crank revolution data present (0x0020)
  uint8_t payload[8];
  uint16_t flags = 0x0020;

  int16_t pwr = (int16_t)constrain((int)lroundf(s.power_w), -32768, 32767);

  put_u16_le(&payload[0], flags);
  put_s16_le(&payload[2], pwr);
  put_u16_le(&payload[4], s.crank_revs);
  put_u16_le(&payload[6], s.crank_evt_1024);

  ch_measurement->setValue(payload, sizeof(payload));
  ch_measurement->notify();
}

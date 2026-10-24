#pragma once

#include <Arduino.h>

namespace GpsConfig {
struct Frame {
  uint8_t cls, id;
  uint16_t length;
  uint8_t payload[256];
};

inline void send(HardwareSerial& port, uint8_t cls, uint8_t id,
                 const uint8_t* data, uint16_t length) {
  const uint8_t header[] = {cls, id, static_cast<uint8_t>(length),
                           static_cast<uint8_t>(length >> 8)};
  uint8_t a = 0, b = 0;
  port.write(0xB5);
  port.write(0x62);
  for (uint8_t value : header) {
    port.write(value);
    a += value;
    b += a;
  }
  for (uint16_t i = 0; i < length; ++i) {
    port.write(data[i]);
    a += data[i];
    b += a;
  }
  port.write(a);
  port.write(b);
  port.flush();
}

// Startup-only parser: ignore NMEA and reject oversized or corrupt UBX frames.
inline bool receive(HardwareSerial& port, Frame& frame, uint32_t timeout) {
  const uint32_t start = millis();
  uint16_t position = 0, length = 0;
  uint8_t header[4] = {}, a = 0, b = 0, receivedA = 0;
  while (millis() - start < timeout) {
    if (!port.available()) { delay(1); continue; }
    const uint8_t value = port.read();
    if (position == 0) { if (value == 0xB5) position = 1; continue; }
    if (position == 1) {
      position = value == 0x62 ? 2 : value == 0xB5 ? 1 : 0;
      a = b = 0;
      continue;
    }
    if (position < 6) {
      header[position - 2] = value;
      a += value; b += a;
      if (position == 5) {
        length = header[2] | (static_cast<uint16_t>(header[3]) << 8);
        if (length > sizeof(frame.payload)) { position = 0; continue; }
      }
    } else if (position < 6 + length) {
      frame.payload[position - 6] = value;
      a += value; b += a;
    } else if (position == 6 + length) {
      receivedA = value;
    } else {
      position = 0;
      if (receivedA != a || value != b) continue;
      frame.cls = header[0]; frame.id = header[1]; frame.length = length;
      return true;
    }
    ++position;
  }
  return false;
}

inline bool poll(HardwareSerial& port, uint8_t id, Frame& frame) {
  send(port, 0x06, id, nullptr, 0);
  const uint32_t start = millis();
  while (true) {
    const uint32_t elapsed = millis() - start;
    if (elapsed >= 1200 || !receive(port, frame, 1200 - elapsed)) return false;
    if (frame.cls == 0x06 && frame.id == id) return true;
  }
}

inline bool set(HardwareSerial& port, uint8_t id, const uint8_t* data, uint16_t size) {
  // Remove stale acknowledgments before sending another CFG command.
  while (port.available()) port.read();
  send(port, 0x06, id, data, size);
  const uint32_t start = millis();
  Frame frame;
  while (true) {
    const uint32_t elapsed = millis() - start;
    if (elapsed >= 1200 || !receive(port, frame, 1200 - elapsed)) break;
    if (frame.cls == 0x05 && frame.length == 2 &&
        frame.payload[0] == 0x06 && frame.payload[1] == id) {
      return frame.id == 0x01;
    }
  }
  return false;
}

inline bool configure(HardwareSerial& port, uint32_t& baud) {
  Frame frame;
  bool found = false;
  // Both a cold GPS start and an ESP32-only reset must work.
  for (uint32_t candidate : {38400UL, 9600UL, 19200UL, 57600UL, 115200UL, 4800UL}) {
    port.updateBaudRate(candidate);
    delay(100);
    while (port.available()) port.read();
    if (poll(port, 0x00, frame) && frame.length == 20) {
      baud = candidate;
      found = true;
      break;
    }
  }
  if (!found) {
    baud = 9600;
    port.updateBaudRate(baud);
    Serial.println("GPS CFG: ni UBX odgovora; 5 Hz NI potrjeno. Preveri GPS RX -> GPIO4.");
    return false;
  }
  Serial.printf("GPS CFG: dvosmerna UBX povezava pri %lu baud.\n", static_cast<unsigned long>(baud));
  if (baud != 38400) {
    // Preserve all other UART settings, enable UBX+NMEA, and raise bandwidth.
    const uint32_t targetBaud = 38400;
    for (uint8_t i = 0; i < 4; ++i) frame.payload[8 + i] = targetBaud >> (8 * i);
    frame.payload[12] |= 0x03;
    frame.payload[14] |= 0x03;
    send(port, 0x06, 0x00, frame.payload, 20);
    delay(100);
    port.updateBaudRate(targetBaud);
    delay(100);
    if (!poll(port, 0x00, frame) || frame.length != 20) {
      port.updateBaudRate(baud);
      Serial.println("GPS CFG: prehod na 38400 ni potrjen; 5 Hz NI nastavljeno.");
      return false;
    }
    baud = targetBaud;
  }
  // GGA (position/quality) and RMC (position/speed/time) every navigation epoch.
  // Disable redundant NMEA traffic on this UART.
  bool messagesOk = true;
  for (uint8_t id = 0; id <= 5; ++id) {
    const uint8_t message[] = {0xF0, id, static_cast<uint8_t>(id == 0 || id == 4)};
    if (!set(port, 0x01, message, sizeof(message))) messagesOk = false;
  }
  // Continuous mode; power saving can restrict navigation update frequency.
  const uint8_t rxm[] = {0x08, 0x00};
  const bool continuous = set(port, 0x11, rxm, sizeof(rxm));
  const uint8_t rate[] = {0xC8, 0x00, 0x01, 0x00, 0x01, 0x00};
  const bool ack = set(port, 0x08, rate, sizeof(rate));
  const bool readback = poll(port, 0x08, frame) && frame.length == 6 &&
                        memcmp(frame.payload, rate, sizeof(rate)) == 0;
  Serial.printf("GPS CFG: UART=%lu, GGA/RMC=%s, continuous=%s, CFG-RATE ACK=%s, readback 200 ms=%s.\n",
                static_cast<unsigned long>(baud), messagesOk ? "OK" : "NAPAKA",
                continuous ? "OK" : "NAPAKA", ack ? "OK" : "NAPAKA",
                readback ? "OK" : "NAPAKA");
  // Apply on every boot, without writing receiver flash/EEPROM.
  return messagesOk && continuous && ack && readback;
}
}  // namespace GpsConfig

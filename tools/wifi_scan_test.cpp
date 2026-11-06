#include <Arduino.h>
#include <WiFi.h>

namespace {
constexpr unsigned long SCAN_INTERVAL_MS = 15000;
unsigned long lastScanFinishedAt = 0;

void scanWifiNetworks() {
  Serial.println();
  Serial.println("Aktivno iskanje Wi-Fi omrezij ...");

  int networkCount = WiFi.scanNetworks(
      /*async=*/false, /*hidden=*/true, /*passive=*/false,
      /*max_ms_per_channel=*/500);

  if (networkCount == 0) {
    Serial.println("Aktivni pregled ni nasel omrezij; poskusam pasivno ...");
    delay(250);
    networkCount = WiFi.scanNetworks(
        /*async=*/false, /*hidden=*/true, /*passive=*/true,
        /*max_ms_per_channel=*/1000);
  }

  if (networkCount < 0) {
    Serial.printf("Napaka pri Wi-Fi pregledu: %d\n", networkCount);
    return;
  }

  Serial.printf("Najdenih omrezij: %d\n", networkCount);
  for (int index = 0; index < networkCount; ++index) {
    const char* security =
        WiFi.encryptionType(index) == WIFI_AUTH_OPEN ? "odprto" : "zasciteno";

    Serial.printf("%2d. %-32s | %4ld dBm | kanal %2ld | %s\n",
                  index + 1,
                  WiFi.SSID(index).c_str(),
                  static_cast<long>(WiFi.RSSI(index)),
                  static_cast<long>(WiFi.channel(index)),
                  security);
  }

  WiFi.scanDelete();
  lastScanFinishedAt = millis();
  Serial.println("Wi-Fi pregled koncan. Naslednji pregled cez 15 sekund.");
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("ESP32 lap timer: Wi-Fi komunikacijski preizkus.");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  scanWifiNetworks();
}

void loop() {
  if (millis() - lastScanFinishedAt >= SCAN_INTERVAL_MS) {
    scanWifiNetworks();
  }

  delay(50);
}

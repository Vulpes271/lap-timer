#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <esp_mac.h>
#include "demo_data.h"

// Standalone application-connection fixture. No GPIO peripherals, GPS, SD or display.
namespace {
constexpr char SSID[] = "LapTimer-TEST";
constexpr char PASSWORD[] = "testtimer123";
const IPAddress address(192, 168, 4, 1);
WebServer server(80);
String device;
String serialLine;
bool radioOn = false;
uint32_t requests = 0;
uint32_t lastStatus = 0;

String sessionId() {
    // The generated fixture uses a positive epoch-seconds ID, stable across reboots.
    const String summary(DEMO_SESSION_JSON);
    const int start = summary.indexOf("\"id\":") + 5;
    return summary.substring(start, summary.indexOf(',', start));
}

String sessionsJson() {
    return String("{\"version\":1,\"device\":\"") + device +
        "\",\"storageError\":false,\"sessions\":[" + DEMO_SESSION_JSON + "]}";
}

String lapsJson() {
    return String("{\"session\":") + DEMO_SESSION_JSON + ",\"laps\":" + DEMO_LAPS_JSON + "}";
}

void respond(const String& body, int status = 200, const char* type = "application/json") {
    ++requests;
    Serial.printf("HTTP %s -> %d\n", server.uri().c_str(), status);
    server.sendHeader("Cache-Control", "no-store");
    server.send(status, type, body);
}

void printStatus() {
    Serial.printf("STATUS wifi=%s clients=%u requests=%lu device=%s\n", radioOn ? "ON" : "OFF",
        radioOn ? WiFi.softAPgetStationNum() : 0, static_cast<unsigned long>(requests), device.c_str());
}

void startRadio() {
    if (radioOn) { printStatus(); return; }
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAPConfig(address, address, IPAddress(255, 255, 255, 0)) ||
        !WiFi.softAP(SSID, PASSWORD, 1, false, 4)) {
        Serial.println("ERROR could not start test Wi-Fi; use return to retry");
        return;
    }
    server.begin();
    radioOn = true;
    Serial.printf("PHONE: SSID=%s PASSWORD=%s IP=192.168.4.1 DEVICE=%s\n", SSID, PASSWORD, device.c_str());
    Serial.println("READY one completed TEST session; 8 laps; best 1:03.800");
}

void stopRadio() {
    if (radioOn) {
        server.stop();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_OFF);
        radioOn = false;
    }
    Serial.println("DEPART Wi-Fi off; send return to simulate arrival in paddock");
}

// Use loopback: with no external station joined, routing to the AP's own IP can fail.
// This exercises real HTTP handlers; a phone separately exercises the radio link.
bool checkHttp(const String& path, int status, const String& body) {
    WiFiClient client;
    if (!client.connect(IPAddress(127, 0, 0, 1), 80, 1500)) {
        Serial.printf("HTTPTEST connection failed: %s errno=%d\n", path.c_str(), errno);
        return false;
    }
    client.print(String("GET ") + path + " HTTP/1.1\r\nHost: 192.168.4.1\r\nConnection: close\r\n\r\n");
    String response;
    const uint32_t started = millis();
    while (millis() - started < 4000) {
        server.handleClient();
        while (client.available()) response += static_cast<char>(client.read());
        if (!client.connected() && !client.available()) break;
        delay(1);
    }
    client.stop();
    const int split = response.indexOf("\r\n\r\n");
    const bool valid = response.startsWith(String("HTTP/1.1 ") + status + " ") && split >= 0 && response.substring(split + 4) == body;
    if (!valid) Serial.printf("HTTPTEST response mismatch: %s bytes=%u\n", path.c_str(), response.length());
    return valid;
}

void selfTest() {
    if (!radioOn) { Serial.println("SELFTEST {\"passed\":false,\"error\":\"wifi_off\"}"); return; }
    const bool index = checkHttp("/api/v1/sessions", 200, sessionsJson());
    const bool laps = checkHttp(String("/api/v1/laps?id=") + sessionId(), 200, lapsJson());
    const bool missing = checkHttp("/api/v1/laps", 400, "{\"error\":\"missing_id\"}");
    const bool invalid = checkHttp("/api/v1/laps?id=abc", 400, "{\"error\":\"invalid_id\"}");
    const bool absent = checkHttp("/api/v1/laps?id=0", 404, "{\"error\":\"session_not_found\"}");
    const bool unknown = checkHttp("/does-not-exist", 404, "{\"error\":\"not_found\"}");
    Serial.printf("SELFTEST {\"passed\":%s,\"sessions\":%s,\"laps\":%s,\"missingId\":%s,\"invalidId\":%s,\"notFound\":%s,\"unknownPath\":%s}\n",
        index && laps && missing && invalid && absent && unknown ? "true" : "false",
        index ? "true" : "false", laps ? "true" : "false", missing ? "true" : "false",
        invalid ? "true" : "false", absent ? "true" : "false", unknown ? "true" : "false");
}

void command(String line) {
    line.trim();
    if (line == "status") printStatus();
    else if (line == "sessions") Serial.println(String("JSON_SESSIONS ") + sessionsJson());
    else if (line == "laps") Serial.println(String("JSON_LAPS ") + lapsJson());
    else if (line == "depart") stopRadio();
    else if (line == "return") startRadio();
    else if (line == "selftest") selfTest();
    else Serial.println("COMMANDS status | sessions | laps | depart | return | selftest | help");
}
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\nTEST TIMER - standalone ESP32 app simulator v1");
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char identity[24];
    snprintf(identity, sizeof(identity), "TEST-%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    device = identity;
    WiFi.persistent(false);
    server.on("/api/v1/sessions", HTTP_GET, [] { respond(sessionsJson()); });
    server.on("/api/v1/laps", HTTP_GET, [] {
        if (!server.hasArg("id") || server.arg("id").isEmpty()) { respond("{\"error\":\"missing_id\"}", 400); return; }
        const String id = server.arg("id");
        if (id.length() > 19) { respond("{\"error\":\"invalid_id\"}", 400); return; }
        for (unsigned i = 0; i < id.length(); ++i) {
            if (id[i] < '0' || id[i] > '9') { respond("{\"error\":\"invalid_id\"}", 400); return; }
        }
        if (id != sessionId()) { respond("{\"error\":\"session_not_found\"}", 404); return; }
        respond(lapsJson());
    });
    server.on("/", HTTP_GET, [] {
        respond(String("<!doctype html><meta name='viewport' content='width=device-width'><title>Test timer</title>") +
            "<h1>TEST TIMER</h1><p>One simulated session, 8 laps, best 1:03.800. No sensors required.</p>" +
            "<p><a href='/api/v1/sessions'>Sessions JSON</a> | <a href='/api/v1/laps?id=" + sessionId() + "'>Laps JSON</a></p>", 200, "text/html; charset=utf-8");
    });
    server.onNotFound([] { respond("{\"error\":\"not_found\"}", 404); });
    startRadio();
    command("help");
}

void loop() {
    if (radioOn) server.handleClient();
    while (Serial.available()) {
        const char value = static_cast<char>(Serial.read());
        if (value == '\n') { command(serialLine); serialLine = ""; }
        else if (value != '\r') {
            if (serialLine.length() < 160) serialLine += value;
            else { serialLine = ""; Serial.println("ERROR serial command too long"); }
        }
    }
    if (millis() - lastStatus >= 10000) { printStatus(); lastStatus = millis(); }
    delay(2);
}

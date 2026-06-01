#include "wifi_task.h"
#include "config_manager.h"
#include <WiFi.h>
#include <ESPmDNS.h>

extern const char* DEVICE_ID;

bool wifiConnected = false;
bool apModeActive  = false;

static const unsigned long WIFI_RECONNECT_INTERVAL = 15000;

void setupAPMode() {
    WiFi.mode(WIFI_AP);
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char macSuffix[13];
    snprintf(macSuffix, sizeof(macSuffix), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    String ssid = String("AquaSensys-") + macSuffix;
    WiFi.softAP(ssid.c_str());
    apModeActive = true;
    Serial.println("[AP] No WiFi credentials found. Access Point started.");
    Serial.printf("[AP] SSID: %s\n", ssid.c_str());
    Serial.printf("[AP] IP:   %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("[AP] Connect and open http://192.168.4.1 to configure WiFi.");
}

void castDNS() {
    if (MDNS.begin(DEVICE_ID)) {
        Serial.println("mDNS responder started");
        Serial.printf("Access your ESP32 at: http://%s.local\n", DEVICE_ID);
        MDNS.addService("http", "tcp", 80);
    } else {
        Serial.println("Error starting mDNS");
    }
}

void setupWiFi() {
    if (config.wifi_ssid.isEmpty()) {
        setupAPMode();
        return;
    }
    WiFi.persistent(false);
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.mode(WIFI_STA);
    Serial.println("[WiFi] Scanning...");
    int scanCount = WiFi.scanNetworks(false, false);
    uint8_t targetBSSID[6] = {0};
    bool bssidFound = false;
    int bestRSSI = -100;
    for (int i = 0; i < scanCount; i++) {
        if (WiFi.SSID(i) == config.wifi_ssid && WiFi.RSSI(i) > bestRSSI) {
            memcpy(targetBSSID, WiFi.BSSID(i), 6);
            bestRSSI = WiFi.RSSI(i);
            bssidFound = true;
        }
    }
    WiFi.scanDelete();

    if (bssidFound) {
        Serial.printf("[WiFi] Pinning to BSSID %02X:%02X:%02X:%02X:%02X:%02X (RSSI %d)\n",
            targetBSSID[0], targetBSSID[1], targetBSSID[2],
            targetBSSID[3], targetBSSID[4], targetBSSID[5], bestRSSI);
        WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str(), 0, targetBSSID);
    } else {
        Serial.println("[WiFi] SSID not found in scan, connecting without BSSID pin");
        WiFi.begin(config.wifi_ssid.c_str(), config.wifi_password.c_str());
    }

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        vTaskDelay(pdMS_TO_TICKS(500));
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected to %s — IP: %s\n",
            config.wifi_ssid.c_str(), WiFi.localIP().toString().c_str());
        wifiConnected = true;
        castDNS();
    } else {
        Serial.println("\nFailed to connect to WiFi. Running in offline mode.");
        wifiConnected = false;
    }
}

void wifiTask(void* pvParameters) {
    for (;;) {
        if (!apModeActive) {
            if (wifiConnected && WiFi.status() != WL_CONNECTED) {
                wifiConnected = false;
                Serial.println("[WiFi] Connection lost");
            }
            if (!wifiConnected) {
                Serial.println("[WiFi] Attempting reconnect...");
                setupWiFi();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(WIFI_RECONNECT_INTERVAL));
    }
}

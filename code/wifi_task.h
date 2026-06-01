#ifndef WIFI_TASK_H
#define WIFI_TASK_H

extern bool wifiConnected;
extern bool apModeActive;

void setupWiFi();
void setupAPMode();
void castDNS();

void wifiTask(void* pvParameters); // Core 0, priority 1

#endif

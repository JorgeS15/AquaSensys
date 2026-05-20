#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <ArduinoJson.h>

// Function to setup all file management routes
void setupFileManagerRoutes(AsyncWebServer& server);

// Individual route handlers
void handleUploadPage(AsyncWebServerRequest *request);
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleFileUploadComplete(AsyncWebServerRequest *request);
void handleListFiles(AsyncWebServerRequest *request);
void handleDeleteFile(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

#endif
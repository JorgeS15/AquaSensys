#ifndef FILE_MANAGER_H
#define FILE_MANAGER_H

#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <ArduinoJson.h>

#define ZIP_MAX_FILENAME 128

enum ZipParseState {
    ZIP_IDLE,
    ZIP_FIND_SIG,
    ZIP_READ_LOCAL_HEADER,
    ZIP_READ_FILENAME,
    ZIP_READ_EXTRA,
    ZIP_READ_DATA,
    ZIP_DONE,
    ZIP_ERROR
};

struct ZipUploadState {
    ZipParseState state;

    uint8_t  hdrBuf[30];
    uint8_t  hdrFill;

    uint16_t compressionMethod;
    uint32_t crc32Expected;
    uint32_t compressedSize;
    uint32_t uncompressedSize;
    uint16_t filenameLen;
    uint16_t extraLen;

    char     filename[ZIP_MAX_FILENAME];
    uint16_t filenameFill;

    uint16_t extraRemaining;
    uint32_t dataRemaining;

    File     outFile;
    bool     skipFile;

    String   extractedFiles[32];
    uint8_t  extractedCount;

    uint8_t  sigBuf[4];
    uint8_t  sigFill;

    bool     hasError;
    String   errorMsg;
};

// Function to setup all file management routes
void setupFileManagerRoutes(AsyncWebServer& server);

// Individual route handlers
void handleUploadPage(AsyncWebServerRequest *request);
void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleFileUploadComplete(AsyncWebServerRequest *request);
void handleListFiles(AsyncWebServerRequest *request);
void handleDeleteFile(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

// ZIP archive upload handlers
void handleZipUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void handleZipUploadComplete(AsyncWebServerRequest *request);

#endif
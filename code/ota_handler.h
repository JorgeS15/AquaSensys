#ifndef OTA_HANDLER_H
#define OTA_HANDLER_H

#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <SD.h>
#include <ArduinoJson.h>
#include <MD5Builder.h>

// Device version - declare as extern so it can be defined in main sketch
extern const char* DEVICE_VERSION;
extern const char* DEVICE_NAME;

// OTA status enum
enum OTAStatus {
    OTA_IDLE,
    OTA_UPLOADING,
    OTA_SUCCESS,
    OTA_ERROR_NO_SPACE,
    OTA_ERROR_WRITE,
    OTA_ERROR_MD5,
    OTA_ERROR_MAGIC_BYTE,
    OTA_ERROR_UNKNOWN
};

// OTA progress callback type
typedef std::function<void(size_t progress, size_t total)> OTAProgressCallback;

class OTAHandler {
private:
    AsyncWebServer* _server;
    OTAStatus _status;
    String _errorMessage;
    size_t _totalSize;
    size_t _currentSize;
    uint32_t _startTime;
    String _expectedMD5;
    MD5Builder _md5;
    bool _isUpdating;
    OTAProgressCallback _progressCallback;
    
    // Backup settings
    bool _createBackup;
    String _backupPath;
    
    // Security
    String _updateToken;
    
    // Internal methods
    void handleUploadStart(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void handleUploadData(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    void handleUploadEnd(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
    bool verifyUpdateHeader(uint8_t *data, size_t len);
    String getErrorString(OTAStatus status);
    void cleanup();
    bool createConfigBackup();
    
public:
    OTAHandler();
    ~OTAHandler();
    
    // Initialize OTA with web server
    void begin(AsyncWebServer* server, const String& updatePath = "/update");
    
    // Set progress callback
    void onProgress(OTAProgressCallback callback) { _progressCallback = callback; }
    
    // Enable/disable config backup before update
    void enableBackup(bool enable, const String& path = "/backup") { 
        _createBackup = enable; 
        _backupPath = path;
    }
    
    // Set security token for updates (optional)
    void setUpdateToken(const String& token) { _updateToken = token; }
    
    // Get current status
    OTAStatus getStatus() const { return _status; }
    bool isUpdating() const { return _isUpdating; }
    
    // Get progress info
    size_t getCurrentSize() const { return _currentSize; }
    size_t getTotalSize() const { return _totalSize; }
    uint8_t getProgress() const { 
        return _totalSize > 0 ? (_currentSize * 100) / _totalSize : 0; 
    }
    
    // Get error message
    String getErrorMessage() const { return _errorMessage; }
    
    // Get update statistics
    uint32_t getElapsedTime() const { 
        return _isUpdating ? (millis() - _startTime) / 1000 : 0; 
    }
    
    // Manual update from SD card file
    bool updateFromFile(const String& filepath);
    
    // Check if update file exists on SD
    bool hasUpdateFile(const String& filepath = "/update.bin");
    
    // Recovery mode - attempt to recover from failed update
    bool attemptRecovery();
};

// Global OTA instance
extern OTAHandler OTA;

#endif // OTA_HANDLER_H
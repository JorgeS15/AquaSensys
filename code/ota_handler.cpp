#include "ota_handler.h"

// Global OTA instance
OTAHandler OTA;

OTAHandler::OTAHandler() : 
    _server(nullptr),
    _status(OTA_IDLE),
    _totalSize(0),
    _currentSize(0),
    _startTime(0),
    _isUpdating(false),
    _createBackup(true),
    _backupPath("/backup"),
    _progressCallback(nullptr) {
}

OTAHandler::~OTAHandler() {
    cleanup();
}

void OTAHandler::begin(AsyncWebServer* server, const String& updatePath) {
    _server = server;
    
    // Serve update page
    _server->on(updatePath.c_str(), HTTP_GET, [this](AsyncWebServerRequest *request) {
        if (!_updateToken.isEmpty()) {
            if (!request->hasParam("token") || request->getParam("token")->value() != _updateToken) {
                request->send(403, "text/plain", "Unauthorized");
                return;
            }
        }
        
        // Check if custom update.html exists on SD
        if (SD.exists("/update.html")) {
            // Read the file from SD card
            File file = SD.open("/update.html", FILE_READ);
            if (!file) {
                request->send(500, "text/plain", "Failed to open update.html");
                return;
            }
            
            String html = file.readString();
            file.close();
            
            // Replace placeholders with actual values
            html.replace("%VERSION%", DEVICE_VERSION ? DEVICE_VERSION : "Unknown");
            html.replace("%DEVICE_NAME%", DEVICE_NAME ? DEVICE_NAME : "Device");
            
            request->send(200, "text/html", html);
        } else {
            // Serve built-in update page
            String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Firmware Update</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            max-width: 800px;
            margin: 0 auto;
            padding: 20px;
            background: #f5f5f5;
        }
        .container {
            background: white;
            border-radius: 10px;
            padding: 30px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        h1 {
            color: #333;
            margin-bottom: 30px;
            display: flex;
            align-items: center;
            gap: 10px;
        }
        .update-form {
            margin-bottom: 30px;
        }
        .file-input-wrapper {
            position: relative;
            overflow: hidden;
            display: inline-block;
            cursor: pointer;
            background: #007bff;
            color: white;
            padding: 12px 24px;
            border-radius: 5px;
            transition: background 0.3s;
        }
        .file-input-wrapper:hover {
            background: #0056b3;
        }
        input[type="file"] {
            position: absolute;
            left: -9999px;
        }
        .file-name {
            margin-left: 15px;
            color: #666;
        }
        .update-btn {
            background: #28a745;
            color: white;
            border: none;
            padding: 12px 30px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 16px;
            margin-top: 20px;
            transition: background 0.3s;
        }
        .update-btn:hover:not(:disabled) {
            background: #218838;
        }
        .update-btn:disabled {
            background: #6c757d;
            cursor: not-allowed;
        }
        .progress-container {
            display: none;
            margin-top: 30px;
        }
        .progress-bar {
            width: 100%;
            height: 30px;
            background: #e9ecef;
            border-radius: 15px;
            overflow: hidden;
            position: relative;
        }
        .progress-fill {
            height: 100%;
            background: linear-gradient(90deg, #007bff, #0056b3);
            width: 0%;
            transition: width 0.3s;
            position: relative;
        }
        .progress-text {
            position: absolute;
            width: 100%;
            text-align: center;
            line-height: 30px;
            color: #333;
            font-weight: bold;
            z-index: 1;
        }
        .status {
            margin-top: 20px;
            padding: 15px;
            border-radius: 5px;
            display: none;
        }
        .status.info {
            background: #d1ecf1;
            color: #0c5460;
            border: 1px solid #bee5eb;
        }
        .status.success {
            background: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .status.error {
            background: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
        .stats {
            display: none;
            margin-top: 20px;
            font-size: 14px;
            color: #666;
        }
        .warning {
            background: #fff3cd;
            color: #856404;
            padding: 15px;
            border-radius: 5px;
            margin-bottom: 20px;
            border: 1px solid #ffeeba;
        }
        .current-version {
            background: #e9ecef;
            padding: 10px 15px;
            border-radius: 5px;
            margin-bottom: 20px;
            font-family: monospace;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🚀 Firmware Update</h1>
        
        <div class="warning">
            ⚠️ <strong>Warning:</strong> Do not power off the device during update!
        </div>
        
        <div class="current-version">
            Current Version: <strong>%VERSION%</strong>
        </div>
        
        <div class="update-form">
            <div class="file-input-wrapper">
                <span>Select Firmware File</span>
                <input type="file" id="firmwareFile" accept=".bin" onchange="handleFileSelect(this)">
            </div>
            <span class="file-name" id="fileName"></span>
            
            <br>
            
            <button class="update-btn" id="updateBtn" onclick="startUpdate()" disabled>
                Start Update
            </button>
        </div>
        
        <div class="progress-container" id="progressContainer">
            <div class="progress-bar">
                <div class="progress-text" id="progressText">0%</div>
                <div class="progress-fill" id="progressFill"></div>
            </div>
            <div class="stats" id="stats"></div>
        </div>
        
        <div class="status" id="statusMessage"></div>
    </div>

    <script>
        let updateFile = null;
        let startTime = null;
        
        function handleFileSelect(input) {
            updateFile = input.files[0];
            if (updateFile) {
                document.getElementById('fileName').textContent = updateFile.name + ' (' + formatBytes(updateFile.size) + ')';
                document.getElementById('updateBtn').disabled = false;
            } else {
                document.getElementById('fileName').textContent = '';
                document.getElementById('updateBtn').disabled = true;
            }
        }
        
        function formatBytes(bytes) {
            if (bytes === 0) return '0 Bytes';
            const k = 1024;
            const sizes = ['Bytes', 'KB', 'MB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i];
        }
        
        function formatTime(seconds) {
            const mins = Math.floor(seconds / 60);
            const secs = seconds % 60;
            return mins > 0 ? `${mins}m ${secs}s` : `${secs}s`;
        }
        
        function showStatus(message, type) {
            const status = document.getElementById('statusMessage');
            status.textContent = message;
            status.className = 'status ' + type;
            status.style.display = 'block';
        }
        
        async function startUpdate() {
            if (!updateFile) return;
            
            document.getElementById('updateBtn').disabled = true;
            document.getElementById('progressContainer').style.display = 'block';
            document.getElementById('stats').style.display = 'block';
            showStatus('Uploading firmware...', 'info');
            
            startTime = Date.now();
            
            const formData = new FormData();
            formData.append('firmware', updateFile);
            
            try {
                const xhr = new XMLHttpRequest();
                
                xhr.upload.addEventListener('progress', (e) => {
                    if (e.lengthComputable) {
                        const progress = Math.round((e.loaded / e.total) * 100);
                        updateProgress(progress, e.loaded, e.total);
                    }
                });
                
                xhr.addEventListener('load', function() {
                    if (xhr.status === 200) {
                        showStatus('Update successful! Device will restart...', 'success');
                        setTimeout(() => {
                            window.location.href = '/';
                        }, 5000);
                    } else {
                        showStatus('Update failed: ' + xhr.responseText, 'error');
                        document.getElementById('updateBtn').disabled = false;
                    }
                });
                
                xhr.addEventListener('error', function() {
                    showStatus('Upload failed: Network error', 'error');
                    document.getElementById('updateBtn').disabled = false;
                });
                
                xhr.open('POST', '/update/upload');
                xhr.send(formData);
                
            } catch (error) {
                showStatus('Update failed: ' + error.message, 'error');
                document.getElementById('updateBtn').disabled = false;
            }
        }
        
        function updateProgress(percent, loaded, total) {
            document.getElementById('progressFill').style.width = percent + '%';
            document.getElementById('progressText').textContent = percent + '%';
            
            const elapsed = Math.floor((Date.now() - startTime) / 1000);
            const speed = loaded / elapsed;
            const remaining = (total - loaded) / speed;
            
            document.getElementById('stats').innerHTML = 
                `Uploaded: ${formatBytes(loaded)} / ${formatBytes(total)} | ` +
                `Speed: ${formatBytes(speed)}/s | ` +
                `Time: ${formatTime(elapsed)} | ` +
                `Remaining: ${formatTime(Math.floor(remaining))}`;
        }
    </script>
</body>
</html>
)rawliteral";
            // Replace version placeholder in built-in page too
            html.replace("%VERSION%", DEVICE_VERSION ? DEVICE_VERSION : "Unknown");
            request->send(200, "text/html", html);
        }
    });
    
    // Handle firmware upload
    _server->on("/update/upload", HTTP_POST, 
        [this](AsyncWebServerRequest *request) {
            // Response will be sent by upload handlers
        },
        [this](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
            if (!index) {
                handleUploadStart(request, filename, index, data, len, final);
            }
            handleUploadData(request, filename, index, data, len, final);
            if (final) {
                handleUploadEnd(request, filename, index, data, len, final);
            }
        }
    );
    
    // Status endpoint
    _server->on("/update/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["status"] = getErrorString(_status);
        doc["progress"] = getProgress();
        doc["current"] = _currentSize;
        doc["total"] = _totalSize;
        doc["isUpdating"] = _isUpdating;
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Add API endpoint to check for update files on SD
    _server->on("/api/ota/status", HTTP_GET, [this](AsyncWebServerRequest *request) {
        DynamicJsonDocument doc(256);
        doc["hasUpdateFile"] = SD.exists("/update.bin");
        doc["isUpdating"] = _isUpdating;
        doc["status"] = getErrorString(_status);
        
        String response;
        serializeJson(doc, response);
        request->send(200, "application/json", response);
    });
    
    // Add API endpoint to update from SD card file
    _server->on("/api/ota/update-from-file", HTTP_POST, [this](AsyncWebServerRequest *request) {
        if (_isUpdating) {
            request->send(503, "application/json", "{\"error\":\"Update already in progress\"}");
            return;
        }
        
        if (updateFromFile("/update.bin")) {
            request->send(200, "application/json", "{\"status\":\"success\"}");
        } else {
            request->send(500, "application/json", "{\"error\":\"Update from file failed\"}");
        }
    });
}

void OTAHandler::handleUploadStart(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (_isUpdating) {
        request->send(503, "text/plain", "Update already in progress");
        return;
    }
    
    // Verify token if set
    if (!_updateToken.isEmpty()) {
        if (!request->hasParam("token") || request->getParam("token")->value() != _updateToken) {
            request->send(403, "text/plain", "Unauthorized");
            return;
        }
    }
    
    Serial.println("OTA: Update started");
    Serial.printf("OTA: Filename: %s\n", filename.c_str());
    
    // Initialize update
    _status = OTA_UPLOADING;
    _isUpdating = true;
    _currentSize = 0;
    _totalSize = request->contentLength();
    _startTime = millis();
    _errorMessage = "";
    _md5.begin();
    
    // Get expected MD5 if provided
    if (request->hasParam("md5")) {
        _expectedMD5 = request->getParam("md5")->value();
        Serial.printf("OTA: Expected MD5: %s\n", _expectedMD5.c_str());
    }
    
    // Create backup if enabled
    if (_createBackup) {
        if (!createConfigBackup()) {
            Serial.println("OTA: Warning - Failed to create config backup");
        }
    }
    
    // Verify header
    if (!verifyUpdateHeader(data, len)) {
        _status = OTA_ERROR_MAGIC_BYTE;
        _errorMessage = "Invalid firmware file";
        _isUpdating = false;
        request->send(400, "text/plain", _errorMessage);
        return;
    }
    
    // Begin update
    if (!Update.begin(_totalSize)) {
        _status = OTA_ERROR_NO_SPACE;
        _errorMessage = Update.errorString();
        _isUpdating = false;
        request->send(400, "text/plain", "Not enough space: " + _errorMessage);
        return;
    }
    
    Serial.printf("OTA: Update size: %u bytes\n", _totalSize);
}

void OTAHandler::handleUploadData(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!_isUpdating) return;
    
    // Update MD5
    _md5.add(data, len);
    
    // Write to flash
    if (Update.write(data, len) != len) {
        _status = OTA_ERROR_WRITE;
        _errorMessage = Update.errorString();
        _isUpdating = false;
        Update.abort();
        request->send(500, "text/plain", "Write failed: " + _errorMessage);
        return;
    }
    
    _currentSize += len;
    
    // Call progress callback
    if (_progressCallback) {
        _progressCallback(_currentSize, _totalSize);
    }
    
    // Print progress every 10%
    static uint8_t lastProgress = 0;
    uint8_t currentProgress = getProgress();
    if (currentProgress != lastProgress && currentProgress % 10 == 0) {
        Serial.printf("OTA: Progress: %u%%\n", currentProgress);
        lastProgress = currentProgress;
    }
}

void OTAHandler::handleUploadEnd(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if (!_isUpdating) return;
    
    Serial.println("OTA: Upload complete");
    
    // Calculate MD5
    _md5.calculate();
    String calculatedMD5 = _md5.toString();
    
    // Verify MD5 if provided
    if (!_expectedMD5.isEmpty() && _expectedMD5 != calculatedMD5) {
        _status = OTA_ERROR_MD5;
        _errorMessage = "MD5 mismatch";
        _isUpdating = false;
        Update.abort();
        request->send(400, "text/plain", _errorMessage);
        Serial.printf("OTA: MD5 mismatch - Expected: %s, Got: %s\n", 
                     _expectedMD5.c_str(), calculatedMD5.c_str());
        return;
    }
    
    // Finish update
    if (Update.end(true)) {
        _status = OTA_SUCCESS;
        Serial.printf("OTA: Update success in %u seconds\n", getElapsedTime());
        Serial.printf("OTA: MD5: %s\n", calculatedMD5.c_str());
        
        // Send success response immediately
        request->send(200, "text/plain", "Update successful");
        
        // Mark update as no longer running before restart
        _isUpdating = false;
        
        // Give time for response to be sent
        delay(100);
        
        // Schedule restart with a flag in NVS or just restart
        Serial.println("OTA: Restarting device...");
        delay(500);
        ESP.restart();
    } else {
        _status = OTA_ERROR_UNKNOWN;
        _errorMessage = Update.errorString();
        Serial.printf("OTA: Update failed: %s\n", _errorMessage.c_str());
        request->send(500, "text/plain", "Update failed: " + _errorMessage);
        _isUpdating = false;
    }
}

bool OTAHandler::verifyUpdateHeader(uint8_t *data, size_t len) {
    if (len < 4) return false;
    
    // Check for ESP32 magic bytes
    if (data[0] != 0xE9) return false;
    
    // Additional validation could be added here
    return true;
}

String OTAHandler::getErrorString(OTAStatus status) {
    switch (status) {
        case OTA_IDLE: return "Idle";
        case OTA_UPLOADING: return "Uploading";
        case OTA_SUCCESS: return "Success";
        case OTA_ERROR_NO_SPACE: return "No space";
        case OTA_ERROR_WRITE: return "Write error";
        case OTA_ERROR_MD5: return "MD5 mismatch";
        case OTA_ERROR_MAGIC_BYTE: return "Invalid firmware";
        case OTA_ERROR_UNKNOWN: return "Unknown error";
        default: return "Unknown";
    }
}

bool OTAHandler::createConfigBackup() {
    if (!SD.exists("/config.json")) {
        return true; // No config to backup
    }
    
    // Create backup directory
    if (!SD.exists(_backupPath)) {
        SD.mkdir(_backupPath);
    }
    
    // Generate backup filename with timestamp
    String backupFile = _backupPath + "/config_" + String(millis()) + ".json";
    
    // Copy config file
    File source = SD.open("/config.json", FILE_READ);
    if (!source) return false;
    
    File dest = SD.open(backupFile, FILE_WRITE);
    if (!dest) {
        source.close();
        return false;
    }
    
    // Copy data
    uint8_t buffer[512];
    size_t bytesRead;
    while ((bytesRead = source.read(buffer, sizeof(buffer))) > 0) {
        dest.write(buffer, bytesRead);
    }
    
    source.close();
    dest.close();
    
    Serial.printf("OTA: Config backed up to %s\n", backupFile.c_str());
    return true;
}

bool OTAHandler::updateFromFile(const String& filepath) {
    if (_isUpdating) return false;
    
    if (!SD.exists(filepath)) {
        Serial.printf("OTA: Update file not found: %s\n", filepath.c_str());
        return false;
    }
    
    File updateFile = SD.open(filepath, FILE_READ);
    if (!updateFile) {
        Serial.println("OTA: Failed to open update file");
        return false;
    }
    
    size_t fileSize = updateFile.size();
    Serial.printf("OTA: Updating from file: %s (%u bytes)\n", filepath.c_str(), fileSize);
    
    // Verify and begin update
    uint8_t header[4];
    updateFile.read(header, 4);
    updateFile.seek(0);
    
    if (!verifyUpdateHeader(header, 4)) {
        Serial.println("OTA: Invalid firmware file");
        updateFile.close();
        return false;
    }
    
    if (!Update.begin(fileSize)) {
        Serial.printf("OTA: Not enough space: %s\n", Update.errorString());
        updateFile.close();
        return false;
    }
    
    // Create backup
    if (_createBackup) {
        createConfigBackup();
    }
    
    // Write firmware
    _status = OTA_UPLOADING;
    _isUpdating = true;
    _totalSize = fileSize;
    _currentSize = 0;
    _startTime = millis();
    _md5.begin();
    
    uint8_t buffer[512];
    size_t bytesRead;
    
    while ((bytesRead = updateFile.read(buffer, sizeof(buffer))) > 0) {
        _md5.add(buffer, bytesRead);
        
        if (Update.write(buffer, bytesRead) != bytesRead) {
            Serial.printf("OTA: Write failed: %s\n", Update.errorString());
            Update.abort();
            updateFile.close();
            _status = OTA_ERROR_WRITE;
            _isUpdating = false;
            return false;
        }
        
        _currentSize += bytesRead;
        
        if (_progressCallback) {
            _progressCallback(_currentSize, _totalSize);
        }
        
        // Yield to prevent watchdog
        yield();
    }
    
    updateFile.close();
    _md5.calculate();
    
    if (Update.end(true)) {
        _status = OTA_SUCCESS;
        Serial.printf("OTA: Update from file successful\n");
        Serial.printf("OTA: MD5: %s\n", _md5.toString().c_str());
        
        // Remove update file after successful update
        SD.remove(filepath);
        
        delay(1000);
        ESP.restart();
        return true;
    } else {
        _status = OTA_ERROR_UNKNOWN;
        _errorMessage = Update.errorString();
        Serial.printf("OTA: Update failed: %s\n", _errorMessage.c_str());
    }
    
    _isUpdating = false;
    return false;
}

bool OTAHandler::hasUpdateFile(const String& filepath) {
    return SD.exists(filepath);
}

bool OTAHandler::attemptRecovery() {
    // Look for backup configs
    File backupDir = SD.open(_backupPath);
    if (!backupDir || !backupDir.isDirectory()) {
        return false;
    }
    
    String latestBackup = "";
    unsigned long latestTime = 0;
    
    // Find most recent backup
    File file = backupDir.openNextFile();
    while (file) {
        String name = String(file.name());
        if (name.startsWith("config_") && name.endsWith(".json")) {
            unsigned long timestamp = name.substring(7, name.length() - 5).toInt();
            if (timestamp > latestTime) {
                latestTime = timestamp;
                latestBackup = _backupPath + "/" + name;
            }
        }
        file = backupDir.openNextFile();
    }
    
    if (latestBackup.isEmpty()) {
        return false;
    }
    
    // Restore backup
    Serial.printf("OTA: Restoring config from %s\n", latestBackup.c_str());
    
    // Remove current config
    if (SD.exists("/config.json")) {
        SD.remove("/config.json");
    }
    
    // Copy backup to config
    File source = SD.open(latestBackup, FILE_READ);
    File dest = SD.open("/config.json", FILE_WRITE);
    
    if (!source || !dest) {
        if (source) source.close();
        if (dest) dest.close();
        return false;
    }
    
    uint8_t buffer[512];
    size_t bytesRead;
    while ((bytesRead = source.read(buffer, sizeof(buffer))) > 0) {
        dest.write(buffer, bytesRead);
    }
    
    source.close();
    dest.close();
    
    Serial.println("OTA: Config restored successfully");
    return true;
}

void OTAHandler::cleanup() {
    if (Update.isRunning()) {
        Update.abort();
    }
    _isUpdating = false;
    _status = OTA_IDLE;
}
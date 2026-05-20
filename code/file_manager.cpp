#include "file_manager.h"

// External device constants
extern const char* DEVICE_NAME;
extern const char* DEVICE_VERSION;

void setupFileManagerRoutes(AsyncWebServer& server) {
    // File upload page
    server.on("/upload", HTTP_GET, handleUploadPage);
    
    // Handle file upload
    server.on("/upload_file", HTTP_POST, handleFileUploadComplete, handleFileUpload);
    
    // List files endpoint
    server.on("/list_files", HTTP_GET, handleListFiles);
    
    // Delete file endpoint
    server.on("/delete_file", HTTP_POST, [](AsyncWebServerRequest *request) {
    }, NULL, handleDeleteFile);
}

void handleUploadPage(AsyncWebServerRequest *request) {
    // Try to serve the upload.html file from SD card
    if (SD.exists("/upload.html")) {
        // Read the file from SD card
        File file = SD.open("/upload.html", FILE_READ);
        if (!file) {
            request->send(500, "text/plain", "Failed to open upload.html");
            return;
        }
        
        String html = file.readString();
        file.close();
        
        // Replace placeholders with actual values
        html.replace("%DEVICE_NAME%", DEVICE_NAME ? DEVICE_NAME : "Device");
        html.replace("%VERSION%", DEVICE_VERSION ? DEVICE_VERSION : "Unknown");
        
        request->send(200, "text/html", html);
    } else {
        // Fallback: basic upload page if upload.html is not found
        String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>File Upload - upload.html missing</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { 
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; 
            margin: 40px; 
            background-color: #f0f0f0; 
            text-align: center; 
        }
        .container { 
            max-width: 500px; 
            margin: 0 auto; 
            background: white; 
            padding: 30px; 
            border-radius: 10px; 
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        .error { 
            color: #dc3545; 
            margin-bottom: 20px; 
            padding: 15px;
            background: #f8d7da;
            border: 1px solid #f5c6cb;
            border-radius: 5px;
        }
        .btn { 
            background-color: #007bff; 
            color: white; 
            padding: 12px 20px; 
            border: none; 
            border-radius: 5px; 
            cursor: pointer; 
            text-decoration: none; 
            display: inline-block; 
            margin: 5px;
            transition: background-color 0.2s;
        }
        .btn:hover {
            background-color: #0056b3;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>⚠️ Upload Page Missing</h1>
        <div class="error">
            <strong>Error:</strong> The upload.html file was not found on the SD card.<br>
            Please upload the upload.html file first to access the full file manager.
        </div>
        <a href="/" class="btn">🏠 Back to Dashboard</a>
        <a href="/debug" class="btn">🔍 Debug Info</a>
    </div>
</body>
</html>
)rawliteral";
        request->send(200, "text/html", html);
    }
}

void handleFileUploadComplete(AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "Upload complete");
}

void handleFileUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    static File uploadFile;
    
    if (!index) {
        // First chunk - open file for writing
        Serial.printf("Starting upload: %s\n", filename.c_str());
        String filepath = "/" + filename;
        
        // Remove file if it exists
        if (SD.exists(filepath)) {
            SD.remove(filepath);
        }
        
        uploadFile = SD.open(filepath, FILE_WRITE);
        if (!uploadFile) {
            Serial.println("Failed to open file for writing");
            return;
        }
    }
    
    // Write chunk to file
    if (uploadFile && len) {
        size_t written = uploadFile.write(data, len);
        if (written != len) {
            Serial.println("Write failed");
        }
    }
    
    if (final) {
        // Last chunk - close file
        if (uploadFile) {
            uploadFile.close();
            Serial.printf("Upload complete: %s\n", filename.c_str());
        }
    }
}

void handleListFiles(AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    JsonArray files = doc.to<JsonArray>();
    
    File root = SD.open("/");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            JsonObject fileObj = files.createNestedObject();
            fileObj["name"] = String(file.name());
            fileObj["size"] = file.size();
            file = root.openNextFile();
        }
        root.close();
    }
    
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}

void handleDeleteFile(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (!error && doc.containsKey("filename")) {
        String filename = doc["filename"];
        String filepath = "/" + filename;
        
        if (SD.exists(filepath)) {
            if (SD.remove(filepath)) {
                request->send(200, "application/json", "{\"status\":\"success\"}");
            } else {
                request->send(500, "application/json", "{\"error\":\"Failed to delete file\"}");
            }
        } else {
            request->send(404, "application/json", "{\"error\":\"File not found\"}");
        }
    } else {
        request->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    }
}
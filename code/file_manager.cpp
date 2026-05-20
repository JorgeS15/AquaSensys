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

    // ZIP archive upload endpoint
    server.on("/upload_zip", HTTP_POST, handleZipUploadComplete, handleZipUpload);
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

// ─── ZIP archive upload ───────────────────────────────────────────────────────

static ZipUploadState zipState;

// Feed up to `avail` bytes from `buf` through the ZIP state machine.
// Returns the number of bytes consumed.
static size_t zipFeedBytes(const uint8_t *buf, size_t avail) {
    switch (zipState.state) {

    case ZIP_FIND_SIG: {
        size_t consumed = 0;
        while (consumed < avail && zipState.state == ZIP_FIND_SIG) {
            zipState.sigBuf[zipState.sigFill++] = buf[consumed++];
            if (zipState.sigFill == 4) {
                uint32_t sig = (uint32_t)zipState.sigBuf[0]
                             | ((uint32_t)zipState.sigBuf[1] << 8)
                             | ((uint32_t)zipState.sigBuf[2] << 16)
                             | ((uint32_t)zipState.sigBuf[3] << 24);
                if (sig == 0x04034b50) {
                    zipState.state   = ZIP_READ_LOCAL_HEADER;
                    zipState.hdrFill = 0;
                    zipState.sigFill = 0;
                } else if (sig == 0x02014b50 || sig == 0x06054b50) {
                    zipState.state = ZIP_DONE;
                } else {
                    // Slide window forward by one byte
                    zipState.sigBuf[0] = zipState.sigBuf[1];
                    zipState.sigBuf[1] = zipState.sigBuf[2];
                    zipState.sigBuf[2] = zipState.sigBuf[3];
                    zipState.sigFill   = 3;
                }
            }
        }
        return consumed;
    }

    case ZIP_READ_LOCAL_HEADER: {
        // 26 bytes after the 4-byte signature
        size_t need = 26 - zipState.hdrFill;
        size_t take = (avail < need) ? avail : need;
        memcpy(zipState.hdrBuf + zipState.hdrFill, buf, take);
        zipState.hdrFill += take;

        if (zipState.hdrFill == 26) {
            uint16_t gpFlag = (uint16_t)zipState.hdrBuf[2]
                            | ((uint16_t)zipState.hdrBuf[3] << 8);
            if (gpFlag & 0x08) {
                zipState.hasError = true;
                zipState.errorMsg = "Data descriptor entries not supported";
                return take;
            }

            zipState.compressionMethod = (uint16_t)zipState.hdrBuf[4]
                                       | ((uint16_t)zipState.hdrBuf[5] << 8);
            if (zipState.compressionMethod != 0 && zipState.compressionMethod != 8) {
                zipState.hasError = true;
                zipState.errorMsg = "Unsupported compression: " + String(zipState.compressionMethod);
                return take;
            }

            zipState.crc32Expected    = (uint32_t)zipState.hdrBuf[10]
                                      | ((uint32_t)zipState.hdrBuf[11] << 8)
                                      | ((uint32_t)zipState.hdrBuf[12] << 16)
                                      | ((uint32_t)zipState.hdrBuf[13] << 24);
            zipState.compressedSize   = (uint32_t)zipState.hdrBuf[14]
                                      | ((uint32_t)zipState.hdrBuf[15] << 8)
                                      | ((uint32_t)zipState.hdrBuf[16] << 16)
                                      | ((uint32_t)zipState.hdrBuf[17] << 24);
            zipState.uncompressedSize = (uint32_t)zipState.hdrBuf[18]
                                      | ((uint32_t)zipState.hdrBuf[19] << 8)
                                      | ((uint32_t)zipState.hdrBuf[20] << 16)
                                      | ((uint32_t)zipState.hdrBuf[21] << 24);
            zipState.filenameLen      = (uint16_t)zipState.hdrBuf[22]
                                      | ((uint16_t)zipState.hdrBuf[23] << 8);
            zipState.extraLen         = (uint16_t)zipState.hdrBuf[24]
                                      | ((uint16_t)zipState.hdrBuf[25] << 8);

            if (zipState.filenameLen == 0 || zipState.filenameLen >= ZIP_MAX_FILENAME) {
                zipState.hasError = true;
                zipState.errorMsg = "Invalid filename length";
                return take;
            }

            zipState.filenameFill = 0;
            zipState.state        = ZIP_READ_FILENAME;
        }
        return take;
    }

    case ZIP_READ_FILENAME: {
        size_t need = zipState.filenameLen - zipState.filenameFill;
        size_t take = (avail < need) ? avail : need;
        memcpy(zipState.filename + zipState.filenameFill, buf, take);
        zipState.filenameFill += take;

        if (zipState.filenameFill == zipState.filenameLen) {
            zipState.filename[zipState.filenameLen] = '\0';

            // Strip any directory prefix — keep only the basename
            char *base = zipState.filename;
            for (char *p = zipState.filename; *p; p++) {
                if (*p == '/' || *p == '\\') base = p + 1;
            }
            if (base != zipState.filename) {
                memmove(zipState.filename, base, strlen(base) + 1);
            }

            bool isDir    = (zipState.filename[0] == '\0');
            bool isConfig = (strcmp(zipState.filename, "config.json") == 0);
            zipState.skipFile = isDir || isConfig;

            if (!zipState.skipFile) {
                String path = "/" + String(zipState.filename);
                if (SD.exists(path)) SD.remove(path);
                zipState.outFile = SD.open(path, FILE_WRITE);
                if (!zipState.outFile) {
                    zipState.hasError = true;
                    zipState.errorMsg = "Cannot open: " + path;
                    return take;
                }
            }

            zipState.extraRemaining = zipState.extraLen;
            zipState.state          = ZIP_READ_EXTRA;
        }
        return take;
    }

    case ZIP_READ_EXTRA: {
        size_t take = (avail < zipState.extraRemaining) ? avail : zipState.extraRemaining;
        zipState.extraRemaining -= (uint16_t)take;

        if (zipState.extraRemaining == 0) {
            zipState.dataRemaining = zipState.compressedSize;

            if (!zipState.skipFile && zipState.compressionMethod == 8) {
                zipState.zs.zalloc   = Z_NULL;
                zipState.zs.zfree    = Z_NULL;
                zipState.zs.opaque   = Z_NULL;
                zipState.zs.avail_in = 0;
                zipState.zs.next_in  = Z_NULL;
                if (inflateInit2(&zipState.zs, -15) != Z_OK) {
                    zipState.hasError = true;
                    zipState.errorMsg = "inflateInit failed";
                    return take;
                }
                zipState.zlibInit = true;
            }
            zipState.state = ZIP_READ_DATA;
        }
        return take;
    }

    case ZIP_READ_DATA: {
        size_t take = (avail < zipState.dataRemaining) ? avail : zipState.dataRemaining;

        if (!zipState.skipFile) {
            if (zipState.compressionMethod == 0) {
                if (zipState.outFile) zipState.outFile.write(buf, take);
            } else {
                zipState.zs.avail_in = (uInt)take;
                zipState.zs.next_in  = (Bytef*)buf;
                int ret = Z_OK;
                while (zipState.zs.avail_in > 0 && ret != Z_STREAM_END) {
                    zipState.zs.avail_out = ZIP_OUT_BUF_SIZE;
                    zipState.zs.next_out  = zipState.zlibOutBuf;
                    ret = inflate(&zipState.zs, Z_SYNC_FLUSH);
                    if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
                        zipState.hasError = true;
                        zipState.errorMsg = "inflate error: " + String(ret);
                        return take;
                    }
                    size_t produced = ZIP_OUT_BUF_SIZE - zipState.zs.avail_out;
                    if (produced && zipState.outFile) zipState.outFile.write(zipState.zlibOutBuf, produced);
                }
            }
        }

        zipState.dataRemaining -= (uint32_t)take;

        if (zipState.dataRemaining == 0) {
            if (!zipState.skipFile) {
                if (zipState.outFile) zipState.outFile.close();
                if (zipState.zlibInit) {
                    inflateEnd(&zipState.zs);
                    zipState.zlibInit = false;
                }
                if (zipState.extractedCount < 32) {
                    zipState.extractedFiles[zipState.extractedCount++] = String(zipState.filename);
                }
                Serial.printf("[ZIP] Extracted: %s\n", zipState.filename);
            } else if (strcmp(zipState.filename, "config.json") == 0) {
                Serial.println("[ZIP] Skipped config.json");
            }

            zipState.sigFill = 0;
            zipState.state   = ZIP_FIND_SIG;
        }
        return take;
    }

    default:
        return avail; // DONE / ERROR: drain silently
    }
}

void handleZipUpload(AsyncWebServerRequest *request, String filename,
                     size_t index, uint8_t *data, size_t len, bool final) {
    if (index == 0) {
        if (zipState.outFile) zipState.outFile.close();
        if (zipState.zlibInit) { inflateEnd(&zipState.zs); }
        zipState = ZipUploadState();
        zipState.state = ZIP_FIND_SIG;
        Serial.println("[ZIP] Upload started");
    }

    if (!zipState.hasError && zipState.state != ZIP_DONE) {
        size_t consumed = 0;
        while (consumed < len && !zipState.hasError && zipState.state != ZIP_DONE) {
            consumed += zipFeedBytes(data + consumed, len - consumed);
        }
    }

    if (final) {
        if (zipState.outFile) zipState.outFile.close();
        if (zipState.zlibInit) {
            inflateEnd(&zipState.zs);
            zipState.zlibInit = false;
        }
        Serial.printf("[ZIP] Upload complete, %d file(s) extracted\n", zipState.extractedCount);
    }
}

void handleZipUploadComplete(AsyncWebServerRequest *request) {
    if (zipState.hasError) {
        String body = "{\"success\":false,\"error\":\"" + zipState.errorMsg + "\"}";
        request->send(400, "application/json", body);
        return;
    }

    DynamicJsonDocument doc(1024);
    doc["success"] = true;
    JsonArray arr = doc.createNestedArray("files");
    for (uint8_t i = 0; i < zipState.extractedCount; i++) {
        arr.add(zipState.extractedFiles[i]);
    }
    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
}
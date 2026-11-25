#ifndef AUTH_H
#define AUTH_H

#include <ESPAsyncWebServer.h>

// Password for actions.
//
// With config.admin_password set, every request that changes something (any
// method but GET, HEAD and OPTIONS: settings, pump commands, reboot, updates,
// uploads, calibration, factory reset) needs HTTP Basic authentication as user
// AUTH_USER with that password; the browser asks for it once and remembers
// it. Readings, pages and the live updates stay open. Without a password
// everything is open, as before. config.json itself is never served as a
// file, since it holds the WiFi and MQTT passwords.

static const char* const AUTH_USER = "admin";

// True when no password is set, or the request carries the right one
bool isAuthorized(AsyncWebServerRequest* request);

// Adds the check to every request the server handles. Upload and body
// handlers run before it (while the request is still arriving), so they must
// also skip their work when isAuthorized() is false; the check then answers
// 401 once the request is complete.
void installAuth(AsyncWebServer& server);

#endif // AUTH_H

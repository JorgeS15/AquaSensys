#include "auth.h"
#include "config_manager.h"

bool isAuthorized(AsyncWebServerRequest* request) {
    if (config.admin_password.length() == 0) return true;
    return request->authenticate(AUTH_USER, config.admin_password.c_str());
}

static bool changesSomething(AsyncWebServerRequest* request) {
    WebRequestMethodComposite m = request->method();
    return !(m == HTTP_GET || m == HTTP_HEAD || m == HTTP_OPTIONS);
}

static bool isSecretFile(const String& url) {
    return url.equalsIgnoreCase("/config.json") || url.equalsIgnoreCase("/config.tmp");
}

void installAuth(AsyncWebServer& server) {
    server.addMiddleware([](AsyncWebServerRequest* request, ArMiddlewareNext next) {
        if (isSecretFile(request->url())) {
            request->send(403, "application/json",
                          "{\"error\":\"config.json holds passwords; use Settings to see or back it up\"}");
            return;
        }
        if (changesSomething(request) && !isAuthorized(request)) {
            request->requestAuthentication(AsyncAuthType::AUTH_BASIC, "AquaSensys",
                                           "{\"error\":\"Password required\"}");
            return;
        }
        next();
    });
}

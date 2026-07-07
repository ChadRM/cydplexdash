#include "wifi_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

namespace {

const int MAX_SAVED_NETWORKS = 5;
const char* AP_SSID = "CYD-Setup";
const IPAddress AP_IP(192, 168, 4, 1);

struct WifiNetwork {
    String ssid;
    String password;
};

WifiNetwork g_savedNetworks[MAX_SAVED_NETWORKS];
int g_savedCount = 0;

DNSServer g_dnsServer;
WebServer g_webServer(80);
bool g_portalConnectPending = false;
String g_pendingSsid;
String g_pendingPassword;
String g_portalError;
int g_scanCount = 0;

void loadSavedNetworks() {
    Preferences prefs;
    prefs.begin("wifi", true);
    String json = prefs.getString("nets", "[]");
    prefs.end();

    JsonDocument doc;
    g_savedCount = 0;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return;

    for (JsonObject item : doc.as<JsonArray>()) {
        if (g_savedCount >= MAX_SAVED_NETWORKS) break;
        g_savedNetworks[g_savedCount].ssid = item["ssid"] | "";
        g_savedNetworks[g_savedCount].password = item["password"] | "";
        g_savedCount++;
    }
}

void persistSavedNetworks() {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < g_savedCount; i++) {
        JsonObject item = arr.add<JsonObject>();
        item["ssid"] = g_savedNetworks[i].ssid;
        item["password"] = g_savedNetworks[i].password;
    }
    String json;
    serializeJson(doc, json);

    Preferences prefs;
    prefs.begin("wifi", false);
    prefs.putString("nets", json);
    prefs.end();
}

// Moves (or inserts) ssid/password to the front of the saved list, evicting the oldest entry
// once at capacity, then persists to NVS.
void rememberNetwork(const String& ssid, const String& password) {
    int existing = -1;
    for (int i = 0; i < g_savedCount; i++) {
        if (g_savedNetworks[i].ssid == ssid) {
            existing = i;
            break;
        }
    }
    int shiftFrom = existing >= 0 ? existing
                    : g_savedCount < MAX_SAVED_NETWORKS ? g_savedCount
                                                          : MAX_SAVED_NETWORKS - 1;
    for (int i = shiftFrom; i > 0; i--) g_savedNetworks[i] = g_savedNetworks[i - 1];
    g_savedNetworks[0] = {ssid, password};
    if (existing < 0 && g_savedCount < MAX_SAVED_NETWORKS) g_savedCount++;
    persistSavedNetworks();
}

bool tryConnect(const String& ssid, const String& password, unsigned long timeoutMs) {
    Serial.printf("[wifi] trying saved network \"%s\"...\n", ssid.c_str());
    WiFi.begin(ssid.c_str(), password.c_str());
    unsigned long start = millis();
    while (millis() - start < timeoutMs) {
        if (WiFi.status() == WL_CONNECTED) return true;
        delay(250);
    }
    WiFi.disconnect();
    return false;
}

String htmlPage(const String& errorMessage) {
    String html =
        "<!DOCTYPE html><html><head><meta name='viewport' "
        "content='width=device-width,initial-scale=1'>"
        "<title>CYD Plex Dashboard Setup</title>"
        "<style>body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 16px}"
        "input,select{width:100%;padding:8px;margin:6px 0;box-sizing:border-box}"
        "button{width:100%;padding:10px;margin-top:12px}"
        ".err{color:#b00020}</style></head><body>"
        "<h2>WiFi Setup</h2>";
    if (errorMessage.length() > 0) {
        html += "<p class='err'>" + errorMessage + "</p>";
    }
    html +=
        "<form method='POST' action='/save'>"
        "<label>Network</label><select name='ssid'>";
    for (int i = 0; i < g_scanCount; i++) {
        html += "<option value=\"" + WiFi.SSID(i) + "\">" + WiFi.SSID(i) + "</option>";
    }
    html +=
        "</select>"
        "<label>Password</label>"
        "<input type='password' name='password'>"
        "<button type='submit'>Connect</button>"
        "</form></body></html>";
    return html;
}

void handleRoot() {
    g_webServer.send(200, "text/html", htmlPage(g_portalError));
    g_portalError = "";
}

void handleSave() {
    g_pendingSsid = g_webServer.arg("ssid");
    g_pendingPassword = g_webServer.arg("password");
    g_portalConnectPending = true;
    g_webServer.send(200, "text/html",
                      "<!DOCTYPE html><html><body><h2>Connecting&hellip;</h2>"
                      "<p>Attempting to join the network. If it doesn't work, this page will "
                      "reload with an error in about 10 seconds.</p>"
                      "<script>setTimeout(()=>location.reload(),10000)</script>"
                      "</body></html>");
}

void handleCaptivePortalRedirect() {
    g_webServer.sendHeader("Location", String("http://") + AP_IP.toString() + "/", true);
    g_webServer.send(302, "text/plain", "");
}

// Never returns: a successful credential submission restarts the device; otherwise this keeps
// serving the portal indefinitely.
void startPortal() {
    Serial.println("[wifi] no saved network connected - starting setup portal");
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    WiFi.softAP(AP_SSID);

    g_scanCount = WiFi.scanNetworks();

    g_dnsServer.start(53, "*", AP_IP);

    g_webServer.on("/", handleRoot);
    g_webServer.on("/save", HTTP_POST, handleSave);
    // Common OS captive-portal probe paths - redirecting them to the portal makes the
    // "sign in to network" prompt pop automatically on phones/laptops.
    static const char* PROBE_PATHS[] = {"/generate_204",       "/gen_204", "/hotspot-detect.html",
                                         "/ncsi.txt",           "/fwlink",  "/connecttest.txt"};
    for (const char* path : PROBE_PATHS) g_webServer.on(path, handleCaptivePortalRedirect);
    g_webServer.onNotFound(handleCaptivePortalRedirect);
    g_webServer.begin();

    Serial.printf("[wifi] portal up: SSID \"%s\", visit http://%s\n", AP_SSID,
                  AP_IP.toString().c_str());

    for (;;) {
        g_dnsServer.processNextRequest();
        g_webServer.handleClient();

        if (g_portalConnectPending) {
            g_portalConnectPending = false;
            if (tryConnect(g_pendingSsid, g_pendingPassword, 10000)) {
                rememberNetwork(g_pendingSsid, g_pendingPassword);
                Serial.println("[wifi] portal-provided credentials connected - restarting");
                delay(1000);
                ESP.restart();
            } else {
                Serial.println("[wifi] portal-provided credentials failed to connect");
                g_portalError = "Couldn't connect with that password - try again.";
            }
        }
        delay(2);
    }
}

}  // namespace

void wifiManagerConnect(std::function<void()> onEnterPortalMode) {
    WiFi.mode(WIFI_STA);
    loadSavedNetworks();

    for (int i = 0; i < g_savedCount; i++) {
        if (tryConnect(g_savedNetworks[i].ssid, g_savedNetworks[i].password, 8000)) {
            Serial.printf("[wifi] connected to saved network \"%s\"\n",
                          g_savedNetworks[i].ssid.c_str());
            if (i != 0) rememberNetwork(g_savedNetworks[i].ssid, g_savedNetworks[i].password);
            return;
        }
    }

    if (onEnterPortalMode) onEnterPortalMode();
    startPortal();
}

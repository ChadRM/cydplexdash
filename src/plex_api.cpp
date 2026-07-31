#include "plex_api.h"

#include <WiFiClientSecure.h>

#include "secrets.h"

static String urlEncode(const char* s) {
    String encoded;
    char buf[4];
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += (char)c;
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

int plexHttpGet(HTTPClient& http, const String& path, bool jsonAccept,
                 unsigned long localTimeoutMs, unsigned long funnelTimeoutMs) {
    String localUrl = String("http://") + PLEX_LOCAL_IP + ":" + String(PLEX_SERVER_PORT) + path;
    http.begin(localUrl);
    if (jsonAccept) http.addHeader("Accept", "application/json");
    http.setConnectTimeout(localTimeoutMs);
    http.setTimeout(localTimeoutMs);
    int code = http.GET();
    if (code == HTTP_CODE_OK) return code;
    http.end();

    String funnelUrl = String("https://") + PLEX_FUNNEL_HOST + path;
    // Funnel's cert is a valid publicly-trusted Let's Encrypt cert, but validating it needs an
    // NTP-synced clock, which isn't guaranteed yet this early in boot - skip validation and rely
    // on PLEX_TOKEN as the auth boundary instead, same as the plain-HTTP local path already does.
    static WiFiClientSecure secureClient;
    secureClient.setInsecure();
    http.begin(secureClient, funnelUrl);
    if (jsonAccept) http.addHeader("Accept", "application/json");
    http.setConnectTimeout(funnelTimeoutMs);
    http.setTimeout(funnelTimeoutMs);
    return http.GET();
}

String buildArtPath(const char* thumbPath, int width, int height) {
    // The transcoder's "url" param wants the plain library-relative path (e.g.
    // "/library/metadata/123/thumb/456"), resolved internally - passing a full http://host:port
    // URL here 404s, since Plex treats that as an external image reference instead. Tautulli's
    // thumb/grandparent_thumb fields use this same library-relative format, so this works
    // unchanged whether the path came from Plex or Tautulli.
    //
    // Plex's aspect-fit behavior for width+height isn't a reliable "crop to exactly fill" (it
    // doesn't clamp to the requested box - e.g. a 200x150 request can come back 200x300 for a
    // portrait source). Rather than chase undocumented transcoder flags, we request a plain
    // fit and center-crop it ourselves in fetchAndDecodeArt() using the JPEG's real dimensions.
    return "/photo/:/transcode?width=" + String(width) + "&height=" + String(height) +
           "&url=" + urlEncode(thumbPath) + "&X-Plex-Token=" + PLEX_TOKEN;
}

//
//  DAVLogRedaction.hpp — co z CalDAV/CardDAV wolno zapisać w logu (#275)
//
//  Czyste funkcje (bez sieci, bez silnika), testowane w
//  Tests/dav-log-redaction-test.cpp; strażnik źródeł: Tests/dav-log-guard-test.cpp.
//
//  Logi ActunaMail nie są szyfrowane bazą aplikacji i mogą trafić do zgłoszeń
//  serwisowych, a karty vCard i dokumenty ICS to dane osobowe osób trzecich
//  (kontaktów i uczestników spotkań). Zasady:
//   - treść vCard/ICS/XML odpowiedzi: nigdy, ani we fragmencie — tylko rozmiar;
//   - URL/href zasobu: host + stabilny uchwyt (bez ścieżki, która niesie
//     login, adres e-mail, UID karty czy nazwę kalendarza);
//   - ETag, ctag, token synchronizacji: tylko uchwyt — widać, że się zmienił,
//     nie widać wartości;
//   - debuginfo wyjątków (NetworkRequestUtils: "<url> RETURNED <body>"):
//     zredagowany URL, rozmiar treści i wyłącznie znaczniki, na których silnik
//     podejmuje decyzje (valid-sync-token, invalid_grant, dailyLimitExceeded…).
//
//  Uchwyt to FNV-1a 64 bit skrócony do 8 znaków hex: deterministyczny między
//  uruchomieniami (korelacja wpisów w logu), ale nie odwracalny do treści.
//  To nie jest skrót kryptograficzny — służy korelacji, nie tajności.
//
//  Polityka poświadczeń (DAVAuthPolicy.hpp) bez zmian; stąd tylko davURLHost.
//
//  Tylko nagłówek: bez zmian w projekcie Xcode.
//

#ifndef DAVLogRedaction_hpp
#define DAVLogRedaction_hpp

#include "DAVAuthPolicy.hpp"

#include <cstdint>
#include <cstdio>
#include <string>

using namespace std;

/** Stabilny, nieodwracalny uchwyt wartości (8 znaków hex). */
inline string davLogHandle(const string & value) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : value) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    char buf[17];
    snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
    return string(buf).substr(0, 8);
}

/** URL lub href zasobu → "host/…#uchwyt", sam host bez ścieżki → "host", pusty → "-". */
inline string davRedactURL(const string & url) {
    if (url.empty()) return "-";
    string host;
    string rest;
    if (url[0] == '/') {
        rest = url; // href względny — bez hosta
    } else {
        host = davURLHost(url);
        size_t start = url.find("://");
        start = (start == string::npos) ? 0 : start + 3;
        size_t pathStart = url.find_first_of("/?#", start);
        rest = (pathStart == string::npos) ? "" : url.substr(pathStart);
    }
    if (rest.empty() || rest == "/") return host.empty() ? "-" : host;
    return host + "/…#" + davLogHandle(url);
}

/** ETag (oraz ctag, token synchronizacji) → "etag#uchwyt", pusty → "-". */
inline string davRedactETag(const string & etag) {
    if (etag.empty()) return "-";
    return "etag#" + davLogHandle(etag);
}

/** Treść vCard/ICS/XML → wyłącznie rozmiar w bajtach. */
inline string davRedactPayload(const string & payload) {
    return to_string(payload.size()) + " B";
}

/** Znaczniki z treści odpowiedzi, na których silnik podejmuje decyzje — tylko one zostają. */
inline string davDecisionMarkers(const string & body) {
    static const char * markers[] = {"valid-sync-token", "invalid_grant", "dailyLimitExceeded",
                                     "Sync token is expired"};
    string out;
    for (const char * m : markers) {
        if (body.find(m) != string::npos) out += string(" [") + m + "]";
    }
    return out;
}

/** Każdy http(s)://… w tekście → davRedactURL. */
inline string davRedactURLsInText(const string & text) {
    string out;
    size_t i = 0;
    while (i < text.size()) {
        size_t a = text.find("http://", i);
        size_t b = text.find("https://", i);
        size_t p = min(a, b);
        if (p == string::npos) {
            out += text.substr(i);
            break;
        }
        out += text.substr(i, p - i);
        size_t e = text.find_first_of(" \t\r\n\"'<>", p);
        if (e == string::npos) e = text.size();
        out += davRedactURL(text.substr(p, e - p));
        i = e;
    }
    return out;
}

/** debuginfo SyncException z zapytań DAV, zanim trafi do logu lub zadania. */
inline string davRedactDebugInfo(const string & debuginfo) {
    static const string sep = " RETURNED ";
    size_t r = debuginfo.find(sep);
    if (r != string::npos) {
        const string body = debuginfo.substr(r + sep.size());
        return davRedactURLsInText(debuginfo.substr(0, r)) + sep + davRedactPayload(body) + davDecisionMarkers(body);
    }
    // Ładunek (surowy XML z DavXML, vCard, ICS) — tylko rozmiar i znaczniki.
    if (debuginfo.find('<') != string::npos || debuginfo.find('\n') != string::npos ||
        debuginfo.find("BEGIN:") != string::npos || debuginfo.size() > 300) {
        return "payload " + davRedactPayload(debuginfo) + davDecisionMarkers(debuginfo);
    }
    return davRedactURLsInText(debuginfo);
}

#endif /* DAVLogRedaction_hpp */

//
//  DAVAuthPolicy.hpp — komu i jak wolno wysłać poświadczenia CalDAV/CardDAV
//
//  Czyste funkcje (bez sieci, bez silnika), testowane w Tests/dav-auth-policy-test.cpp.
//  Zasady (audyt bezpieczeństwa 2026-11-16, analysis/130):
//   1. Żadnego nagłówka Authorization po niezaszyfrowanym http — dotyczy
//      wszystkich rodzajów danych (jawny login CalDAV, OAuth, hasło IMAP).
//   2. Hasło IMAP (konto bez osobnego loginu CalDAV i bez OAuth) wysyłamy tylko
//      do hostów, które mają z kontem zaufany związek: host IMAP, host z jawnych
//      ustawień CalDAV, hosty ręcznie dodanych kalendarzy oraz cele przekierowań
//      https z .well-known w tej samej domenie co któryś z nich. Host domeny
//      adresu e-mail (zwykle serwer WWW) NIE dostaje hasła IMAP — ani wprost,
//      ani jako cel przekierowania — chyba że sam jest kotwicą.
//   3. Jawne dane CalDAV i token OAuth — jak dotąd, do każdego hosta https.
//
//  Reguła „ta sama domena” (bez listy sufiksów publicznych): host jest równy
//  kotwicy, albo kończy się na "." + podstawa, gdzie podstawą jest kotwica lub
//  jej bezpośrednia domena nadrzędna, o ile ta ma co najmniej dwa człony i nie
//  wygląda na sufiks publiczny (dwa człony, krajowa domena najwyższego poziomu
//  i krótki człon drugi — com.pl, co.uk, net.pl, wp.pl…). Przy wątpliwości reguła
//  zawęża zaufanie (wtedy działa tylko dokładny host), nigdy go nie poszerza.
//  Adresy IP: tylko dokładna zgodność.
//
//  Tylko nagłówek: bez zmian w projekcie Xcode.
//

#ifndef DAVAuthPolicy_hpp
#define DAVAuthPolicy_hpp

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace std;

struct DAVCredentials {
    string davUser, davPass; // jawny login CalDAV (#268)
    bool oauth = false;      // konto z refresh tokenem (Bearer)
    string imapUser, imapPass;
};

inline string davLower(string s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
    return s;
}

/** Schemat adresu małymi literami ("https", "http") albo "" gdy go brak. */
inline string davURLScheme(const string & url) {
    size_t p = url.find("://");
    if (p == string::npos) return "";
    return davLower(url.substr(0, p));
}

/** Host z adresu (albo gołego hosta): małe litery, bez userinfo, portu i kropki końcowej. */
inline string davURLHost(const string & url) {
    size_t start = url.find("://");
    start = (start == string::npos) ? 0 : start + 3;
    size_t end = url.find_first_of("/?#", start);
    string auth = url.substr(start, end == string::npos ? string::npos : end - start);
    size_t at = auth.rfind('@');
    if (at != string::npos) auth = auth.substr(at + 1);
    string host;
    if (!auth.empty() && auth[0] == '[') {
        size_t close = auth.find(']');
        host = auth.substr(1, close == string::npos ? string::npos : close - 1);
    } else {
        size_t colon = auth.find(':');
        host = auth.substr(0, colon);
    }
    host = davLower(host);
    while (!host.empty() && host.back() == '.') host.pop_back();
    return host;
}

inline vector<string> davLabels(const string & host) {
    vector<string> out;
    size_t s = 0;
    while (true) {
        size_t d = host.find('.', s);
        out.push_back(host.substr(s, d == string::npos ? string::npos : d - s));
        if (d == string::npos) break;
        s = d + 1;
    }
    return out;
}

inline bool davIsIPLiteral(const string & host) {
    if (host.find(':') != string::npos) return true; // IPv6
    if (host.empty()) return false;
    for (char c : host) {
        if (!isdigit((unsigned char)c) && c != '.') return false;
    }
    return true;
}

/** Heurystyka bez listy sufiksów: "com.pl", "co.uk", "wp.pl" — dwa człony,
 *  dwuliterowa domena krajowa i człon drugi z maks. 3 znaków. */
inline bool davLooksLikePublicSuffix(const string & domain) {
    vector<string> l = davLabels(domain);
    if (l.size() < 2) return true;
    return l.size() == 2 && l[1].size() == 2 && l[0].size() <= 3;
}

/** Podstawy „tej samej domeny” dla kotwicy: ona sama i (jeśli bezpieczna) jej domena nadrzędna. */
inline vector<string> davSiteBases(const string & anchorRaw) {
    string anchor = davURLHost(anchorRaw);
    vector<string> out;
    if (anchor.empty()) return out;
    out.push_back(anchor);
    if (davIsIPLiteral(anchor)) return out;
    size_t dot = anchor.find('.');
    if (dot == string::npos) return out;
    string parent = anchor.substr(dot + 1);
    if (davLabels(parent).size() >= 2 && !davLooksLikePublicSuffix(parent)) out.push_back(parent);
    return out;
}

inline bool davSameSite(const string & hostRaw, const string & anchorRaw) {
    string host = davURLHost(hostRaw);
    string anchor = davURLHost(anchorRaw);
    if (host.empty() || anchor.empty()) return false;
    if (host == anchor) return true;
    if (davIsIPLiteral(host) || davIsIPLiteral(anchor)) return false;
    for (const string & base : davSiteBases(anchor)) {
        if (host == base) return true;
        if (host.size() > base.size() + 1 &&
            host.compare(host.size() - base.size() - 1, string::npos, "." + base) == 0) {
            return true;
        }
    }
    return false;
}

/** Kotwice zaufania konta: host IMAP, jawny host CalDAV, hosty ręcznie dodanych kalendarzy. */
inline set<string> davAnchorHosts(const string & imapHost, const string & caldavHost,
                                  const vector<string> & extraURLs) {
    set<string> out;
    for (const string & raw : vector<string>{imapHost, caldavHost}) {
        string h = davURLHost(raw);
        if (!h.empty()) out.insert(h);
    }
    for (const string & raw : extraURLs) {
        string h = davURLHost(raw);
        if (!h.empty()) out.insert(h);
    }
    return out;
}

/** Wynik przekierowania z .well-known przyjmujemy wyłącznie, gdy to https. */
inline string davAcceptWellKnownRedirect(const string & redirectURL) {
    if (davURLScheme(redirectURL) != "https" || davURLHost(redirectURL).empty()) return "";
    return redirectURL;
}

/** Czy host celu przekierowania https może dostać hasło IMAP (ta sama domena co kotwica).
 *  Host domeny adresu e-mail (zwykle serwer WWW, często u obcego dostawcy) NIGDY
 *  nie dostaje hasła IMAP przez przekierowanie — tylko gdy sam jest kotwicą
 *  (host IMAP, jawny caldav_host, host z caldav_extra_urls). */
inline bool davRedirectTargetTrusted(const string & redirectURL, const set<string> & anchors,
                                     const string & emailDomain = "") {
    if (davAcceptWellKnownRedirect(redirectURL).empty()) return false;
    string host = davURLHost(redirectURL);
    if (anchors.count(host)) return true;
    const string emailHost = davURLHost(emailDomain);
    if (!emailHost.empty() && host == emailHost) return false;
    for (const string & a : anchors) {
        if (davSameSite(host, a)) return true;
    }
    return false;
}

inline string davBase64(const string & in) {
    static const char * t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += t[v & 63];
    }
    if (i + 1 == in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += "==";
    } else if (i + 2 == in.size()) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += t[(v >> 18) & 63]; out += t[(v >> 12) & 63]; out += t[(v >> 6) & 63]; out += '=';
    }
    return out;
}

/** Nagłówek Authorization dla danego adresu albo "" gdy żadnych danych wysłać nie wolno.
 *  imapTrustedHosts: kotwice + przyjęte cele przekierowań (dokładne hosty).
 *  bearerToken wywoływany wyłącznie, gdy token faktycznie ma zostać wysłany. */
inline string davAuthorizationHeader(const string & url, const DAVCredentials & c,
                                     const set<string> & imapTrustedHosts,
                                     const function<string()> & bearerToken) {
    if (davURLScheme(url) != "https") return "";
    if (c.davUser != "" && c.davPass != "") {
        return "Authorization: Basic " + davBase64(c.davUser + ":" + c.davPass);
    }
    if (c.oauth) {
        return "Authorization: Bearer " + bearerToken();
    }
    if (c.imapPass == "") return "";
    if (!imapTrustedHosts.count(davURLHost(url))) return "";
    return "Authorization: Basic " + davBase64(c.imapUser + ":" + c.imapPass);
}

#endif /* DAVAuthPolicy_hpp */

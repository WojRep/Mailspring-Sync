//
//  dav-log-redaction-test.cpp — #275: logi CalDAV/CardDAV bez danych osobowych
//
//  Do logu nie może trafić treść karty vCard ani dokumentu ICS (w całości ani
//  we fragmencie), pełny URL zasobu ani jawny ETag. Diagnostyka zostaje: host,
//  stabilny uchwyt do korelacji wpisów, rozmiar, kody HTTP i znaczniki, na
//  których silnik podejmuje decyzje (valid-sync-token, invalid_grant…).
//

#include "DAVLogRedaction.hpp"

#include <cstdio>

static int failures = 0;
static void check(bool ok, const char * name, const string & detail = "") {
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) failures++;
}
static bool has(const string & s, const string & needle) { return s.find(needle) != string::npos; }

int main() {
    const string vcard =
        "BEGIN:VCARD\r\nVERSION:3.0\r\nUID:8c1f-jan\r\nFN:Jan Kowalski\r\n"
        "TEL;TYPE=CELL:+48 600 100 200\r\nEMAIL:jan.kowalski@example.pl\r\n"
        "ADR:;;ul. Długa 5;Gdańsk;;80-001;PL\r\nBDAY:1980-02-03\r\nEND:VCARD\r\n";
    const string ics =
        "BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nUID:ev-77\r\nSUMMARY:Wizyta u lekarza\r\n"
        "LOCATION:Przychodnia, ul. Krótka 1\r\nATTENDEE;CN=Anna Nowak:mailto:anna@example.pl\r\n"
        "END:VEVENT\r\nEND:VCALENDAR\r\n";
    const string resourceURL = "https://dav.example.pl/addressbooks/jan.kowalski/contacts/8c1f-jan.vcf";
    const string etag = "\"3f1c-55a2-jan\"";

    // --- treść vCard / ICS: tylko rozmiar
    string v = davRedactPayload(vcard);
    check(!has(v, "Kowalski") && !has(v, "600 100") && !has(v, "Długa") && !has(v, "BEGIN:VCARD") && !has(v, "1980"),
          "vCard: żadnego fragmentu karty w logu", v);
    check(has(v, to_string(vcard.size())), "vCard: zostaje rozmiar w bajtach", v);
    string i = davRedactPayload(ics);
    check(!has(i, "lekarza") && !has(i, "Krótka") && !has(i, "Nowak") && !has(i, "VEVENT"),
          "ICS: żadnego fragmentu wydarzenia w logu", i);
    check(davRedactPayload("") == "0 B", "pusty ładunek → 0 B", davRedactPayload(""));

    // --- URL zasobu: host + stabilny uchwyt, bez ścieżki
    string u = davRedactURL(resourceURL);
    check(has(u, "dav.example.pl"), "URL: host zostaje (diagnostyka)", u);
    check(!has(u, "jan.kowalski") && !has(u, "8c1f") && !has(u, "addressbooks"), "URL: bez ścieżki zasobu", u);
    check(davRedactURL(resourceURL) == u, "URL: uchwyt stabilny (korelacja wpisów)");
    check(davRedactURL("https://dav.example.pl/addressbooks/anna.nowak/contacts/1.vcf") != u,
          "URL: różne zasoby → różne uchwyty");
    check(davRedactURL("https://dav.example.pl/") == "dav.example.pl", "URL: sam host bez ścieżki → host",
          davRedactURL("https://dav.example.pl/"));
    string rel = davRedactURL("/calendars/jan.kowalski/home/ev-77.ics");
    check(!has(rel, "jan.kowalski") && !has(rel, "ev-77") && rel.size() > 0, "href względny: bez ścieżki", rel);
    string noScheme = davRedactURL("apidata.googleusercontent.com/caldav/v2/jan@gmail.com/events/");
    check(has(noScheme, "apidata.googleusercontent.com") && !has(noScheme, "jan@gmail.com"),
          "URL bez schematu (Gmail calHost + ścieżka): host tak, adres e-mail nie", noScheme);
    check(davRedactURL("") == "-", "URL: pusty → -");

    // --- ETag: nie w postaci jawnej, ale stabilnie
    string e = davRedactETag(etag);
    check(!has(e, "3f1c") && !has(e, "55a2"), "ETag: nie w postaci jawnej", e);
    check(davRedactETag(etag) == e && davRedactETag("\"inny\"") != e, "ETag: stabilny uchwyt, różne ETagi różne");
    check(davRedactETag("") == "-", "ETag: pusty → -");

    // --- uchwyt
    check(davLogHandle("abc").size() == 8, "uchwyt: 8 znaków hex", davLogHandle("abc"));

    // --- debuginfo wyjątku (NetworkRequestUtils: "<url> RETURNED <body>") — trafia do logu
    //     przez exceptions::logCurrentExceptionWithStackTrace i TaskProcessor (toJSON).
    string di = davRedactDebugInfo(resourceURL + " RETURNED " + vcard);
    check(!has(di, "Kowalski") && !has(di, "jan.kowalski") && !has(di, "BEGIN:VCARD"),
          "debuginfo: bez URL zasobu i treści odpowiedzi", di);
    check(has(di, "dav.example.pl") && has(di, " RETURNED "), "debuginfo: host i układ zostają", di);
    string diMarker = davRedactDebugInfo("https://x.example/cal/ RETURNED <d:error><d:valid-sync-token/></d:error>");
    check(has(diMarker, "valid-sync-token"), "debuginfo: znacznik valid-sync-token zachowany (decyzja o resync)", diMarker);
    string diGrant = davRedactDebugInfo("https://x.example/ RETURNED {\"error\":\"invalid_grant\"}");
    check(has(diGrant, "invalid_grant"), "debuginfo: znacznik invalid_grant zachowany (main.cpp)", diGrant);
    string diQuota = davRedactDebugInfo("https://x.example/ RETURNED {\"reason\":\"dailyLimitExceeded\"}");
    check(has(diQuota, "dailyLimitExceeded"), "debuginfo: znacznik dailyLimitExceeded zachowany (main.cpp)", diQuota);
    string diXml = davRedactDebugInfo("<?xml version=\"1.0\"?><d:multistatus><c:address-data>" + vcard);
    check(!has(diXml, "Kowalski") && !has(diXml, "VCARD"), "debuginfo: surowy XML (DavXML) → tylko rozmiar", diXml);
    string diUrlOnly = davRedactDebugInfo(resourceURL);
    check(has(diUrlOnly, "dav.example.pl") && !has(diUrlOnly, "jan.kowalski"), "debuginfo: sam URL (błąd curl) → host", diUrlOnly);
    check(davRedactDebugInfo("Calendar not found for event syncback") == "Calendar not found for event syncback",
          "debuginfo: zwykły komunikat bez danych zostaje");

    printf("%s (%d błędów)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}

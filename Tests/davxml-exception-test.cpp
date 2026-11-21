//
//  davxml-exception-test.cpp — #267
//
//  Minimalny test DavXML bez frameworka (mailsync nie ma harnessu testów C++).
//  Budowanie i uruchomienie: Tests/run-tests.sh
//
//  Błąd: gdy domena adresu e-mail (kandydat RFC 6764 w resolveCalendarHomeURL)
//  odpowiada na PROPFIND kodem 200 z HTML-em, DavXML nie parsuje odpowiedzi
//  i rzucał `throw new SyncException(...)` — WSKAŹNIK. Wszystkie wywołujące
//  łapią `SyncException &`, więc wyjątek uciekał do `catch (...)` w main.cpp
//  i wątek kalendarza i kontaktów stawał do restartu ActunaMail.
//
//  Kontrakt: błąd parsowania to SyncException rzucony przez wartość.
//

#include "DavXML.hpp"
#include "SyncException.hpp"

#include <cstdlib>
#include <unistd.h>

static int failures = 0;

static void check(bool ok, const char * name, const string & detail = "") {
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) failures++;
}

static void testHtmlResponseThrowsSyncExceptionByValue() {
    const char * name = "odpowiedź HTML zamiast XML -> SyncException przez wartość";
    string html = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>Example</title></head>"
                  "<body><h1>Witamy</h1></body></html>";
    try {
        DavXML doc(html, "https://example.com/");
        check(false, name, "nie rzucono wyjątku");
    } catch (SyncException & e) {
        check(e.key == "Unable to parse CalDav XML", name, "key=" + e.key);
    } catch (SyncException * e) {
        check(false, name, "rzucono WSKAŹNIK (throw new) — catch (SyncException &) go nie złapie");
        delete e;
    } catch (...) {
        check(false, name, "rzucono wyjątek innego typu");
    }
}

static void testValidMultistatusParses() {
    const char * name = "poprawna odpowiedź multistatus parsuje się bez wyjątku";
    string xml = "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\"><d:response>"
                 "<d:href>/dav/calendars/user/</d:href></d:response></d:multistatus>";
    try {
        DavXML doc(xml, "https://example.com/");
        check(doc.nodeContentAtXPath("//d:href/text()") == "/dav/calendars/user/", name);
    } catch (...) {
        check(false, name, "nieoczekiwany wyjątek");
    }
}

// #275 — zepsuta odpowiedź z kartą vCard: ani debuginfo wyjątku (trafia do logu
// przez exceptions::logCurrentExceptionWithStackTrace), ani komunikat parsera
// libxml2 na stderr (strumień silnika trafia do logu aplikacji) nie mogą
// zawierać fragmentu karty.
static void testBrokenPayloadDoesNotLeak() {
    string broken = "<?xml version=\"1.0\"?><d:multistatus xmlns:d=\"DAV:\"><d:response>"
                    "<c:address-data>BEGIN:VCARD\nFN:Jan Kowalski\nTEL:+48 600 100 200\nEND:VCARD"
                    "</d:respons></d:multistatus>";
    char tmpl[] = "/tmp/davxml-stderr-XXXXXX";
    int fd = mkstemp(tmpl);
    fflush(stderr);
    int saved = dup(2);
    dup2(fd, 2);
    string debuginfo;
    try {
        DavXML doc(broken, "https://dav.example.pl/addressbooks/jan.kowalski/");
    } catch (SyncException & e) {
        debuginfo = e.debuginfo;
    } catch (...) {
    }
    fflush(stderr);
    dup2(saved, 2);
    close(saved);
    lseek(fd, 0, SEEK_SET);
    string captured;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) captured.append(buf, (size_t)n);
    close(fd);
    unlink(tmpl);

    check(debuginfo.find("Kowalski") == string::npos && debuginfo.find("VCARD") == string::npos,
          "#275 debuginfo błędu parsowania bez treści odpowiedzi", debuginfo.substr(0, 120));
    check(captured.find("Kowalski") == string::npos && captured.find("jan.kowalski") == string::npos,
          "#275 parser nie wypisuje fragmentów odpowiedzi ani URL zasobu na stderr", captured.substr(0, 160));
}

int main() {
    testHtmlResponseThrowsSyncExceptionByValue();
    testValidMultistatusParses();
    testBrokenPayloadDoesNotLeak();
    printf("%s (%d failure(s))\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}

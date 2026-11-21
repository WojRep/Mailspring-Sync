//
//  dav-discovery-outcome-test.cpp — klasyfikacja wyniku discovery CalDAV (#341)
//
//  tech@actuna.pl: oba hosty-kandydaci (actuna.pl, kaszub.actuna.pl) to zwykłe
//  serwery WWW, które na PROPFIND odpowiadają 405. Silnik ma odróżnić „serwer
//  nie ma CalDAV" (same 404/405/406, bez 401 i bez 207) od sytuacji niepewnej
//  (inne kody, błąd bez kodu HTTP), żeby aplikacja mogła trwale pokazać
//  komunikat przy koncie — i nie pokazać go po chwilowej awarii.
//

#include "DAVDiscoveryOutcome.hpp"

#include <cstdio>

static int failures = 0;
static void check(bool ok, const char * name) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

int main() {
    // --- kod HTTP z klucza SyncException (NetworkRequestUtils.cpp: "Invalid Response Code: N")
    check(davHTTPCodeFromErrorKey("Invalid Response Code: 405") == 405, "kod: 405 z klucza wyjątku");
    check(davHTTPCodeFromErrorKey("Invalid Response Code: 401") == 401, "kod: 401 z klucza wyjątku");
    check(davHTTPCodeFromErrorKey("Invalid Response Code: 4050") == 0, "kod: śmieci po numerze → 0");
    check(davHTTPCodeFromErrorKey("Couldn't resolve host name") == 0, "kod: błąd bez kodu HTTP → 0");
    check(davHTTPCodeFromErrorKey("") == 0, "kod: pusty klucz → 0");

    // --- klasyfikacja
    check(classifyCalDAVDiscovery({405, 405}, false) == CalDAVDiscoveryVerdict::NoCalDAV,
          "tech@actuna.pl: wszyscy kandydaci 405 → serwer nie ma CalDAV");
    check(classifyCalDAVDiscovery({404, 406}, false) == CalDAVDiscoveryVerdict::NoCalDAV,
          "404 i 406 → serwer nie ma CalDAV");
    check(classifyCalDAVDiscovery({405}, true) == CalDAVDiscoveryVerdict::Found,
          "znaleziony calendar-home-set wygrywa z 405 innego kandydata");
    check(classifyCalDAVDiscovery({405, 401}, false) == CalDAVDiscoveryVerdict::Inconclusive,
          "401 → serwer DAV wymaga logowania, to nie brak CalDAV");
    check(classifyCalDAVDiscovery({405, 207}, false) == CalDAVDiscoveryVerdict::Inconclusive,
          "207 bez home-set → serwer mówi DAV, to nie brak CalDAV");
    check(classifyCalDAVDiscovery({405, 0}, false) == CalDAVDiscoveryVerdict::Inconclusive,
          "błąd bez kodu HTTP (timeout, TLS) → niepewne");
    check(classifyCalDAVDiscovery({503}, false) == CalDAVDiscoveryVerdict::Inconclusive,
          "5xx → przejściowe, niepewne");
    check(classifyCalDAVDiscovery({}, false) == CalDAVDiscoveryVerdict::Inconclusive,
          "brak kandydatów → niepewne (nie ma kogo pytać)");

    // --- nazwa stanu przekazywana aplikacji (src/caldav-settings.ts)
    check(calDAVDiscoveryStatusName(CalDAVDiscoveryVerdict::NoCalDAV) == "unsupported", "status: unsupported");
    check(calDAVDiscoveryStatusName(CalDAVDiscoveryVerdict::Found) == "available", "status: available");
    check(calDAVDiscoveryStatusName(CalDAVDiscoveryVerdict::Inconclusive).empty(),
          "status: niepewne → nic nie zgłaszamy");

    printf("%s (%d błędów)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}

//
//  DAVDiscoveryOutcome.hpp — klasyfikacja wyniku discovery CalDAV (#341)
//
//  Czyste funkcje (bez sieci, bez silnika), testowane w
//  Tests/dav-discovery-outcome-test.cpp.
//
//  Discovery pyta kolejnych kandydatów na host CalDAV (DAVWorker::
//  calendarHostCandidates). Dla każdego zapisujemy jeden kod:
//   - kod HTTP z wyjątku "Invalid Response Code: N" (NetworkRequestUtils.cpp),
//   - 207, gdy serwer odpowiedział poprawnym XML-em (mówi DAV), ale nie dał
//     calendar-home-set,
//   - 0, gdy błąd nie niesie kodu HTTP (timeout, TLS, parsowanie).
//  Awaria sieci (isOffline) nie trafia tutaj — przerywa discovery wcześniej
//  i jest ponawiana w kolejnym cyklu.
//
//  „Serwer nie ma CalDAV" tylko wtedy, gdy KAŻDY kandydat odpowiedział
//  404/405/406. 401 oznacza serwer DAV wymagający logowania, 207 — serwer DAV
//  bez kalendarza w tym miejscu, a wszystko inne jest niepewne. Przy
//  wątpliwości nic nie zgłaszamy: lepiej nie pokazać komunikatu, niż pokazać
//  fałszywy po chwilowej awarii.
//
//  Klasyfikacja nie wpływa na to, komu wysyłamy poświadczenia (DAVAuthPolicy.hpp).
//
//  Tylko nagłówek: bez zmian w projekcie Xcode.
//

#ifndef DAVDiscoveryOutcome_hpp
#define DAVDiscoveryOutcome_hpp

#include <cctype>
#include <string>
#include <vector>

using namespace std;

enum class CalDAVDiscoveryVerdict { Found, NoCalDAV, Inconclusive };

/** Kod HTTP z klucza SyncException ("Invalid Response Code: 405" → 405), inaczej 0. */
inline int davHTTPCodeFromErrorKey(const string & key) {
    static const string prefix = "Invalid Response Code: ";
    if (key.compare(0, prefix.size(), prefix) != 0) return 0;
    const string digits = key.substr(prefix.size());
    if (digits.size() != 3) return 0;
    for (char c : digits) {
        if (!isdigit(static_cast<unsigned char>(c))) return 0;
    }
    return stoi(digits);
}

inline CalDAVDiscoveryVerdict classifyCalDAVDiscovery(const vector<int> & candidateCodes, bool homeSetFound) {
    if (homeSetFound) return CalDAVDiscoveryVerdict::Found;
    if (candidateCodes.empty()) return CalDAVDiscoveryVerdict::Inconclusive;
    for (int code : candidateCodes) {
        if (code != 404 && code != 405 && code != 406) return CalDAVDiscoveryVerdict::Inconclusive;
    }
    return CalDAVDiscoveryVerdict::NoCalDAV;
}

/**
 * Stan przekazywany aplikacji w delcie `ProcessCalendarDiscovery`
 * (app/src/caldav-settings.ts). Pusty = nic nie zgłaszamy.
 */
inline string calDAVDiscoveryStatusName(CalDAVDiscoveryVerdict verdict) {
    switch (verdict) {
        case CalDAVDiscoveryVerdict::Found: return "available";
        case CalDAVDiscoveryVerdict::NoCalDAV: return "unsupported";
        default: return "";
    }
}

#endif /* DAVDiscoveryOutcome_hpp */

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

// ---- #286: discovery CardDAV na tych samych zasadach co CalDAV ---------------
//
// CardDAV korzysta z tej samej klasyfikacji (kody kandydatów, 404/405/406 = brak
// usługi) i tych samych kandydatów hostów. Nazwy wartości enum zostają z #341
// (NoCalDAV = „serwer nie ma usługi DAV tego rodzaju").

using DAVDiscoveryVerdict = CalDAVDiscoveryVerdict;

/**
 * Kandydaci na host DAV w kolejności prób (#267, RFC 6764): jawny host z
 * ustawień konta, domena adresu e-mail, host IMAP. Bez usług trzecich i bez
 * hostów zaszytych w kodzie — pytamy wyłącznie serwer użytkownika. Duplikaty
 * (bez względu na wielkość liter) pomijane. To NIE decyduje o hasłach:
 * domena e-mail dalej nie dostaje hasła IMAP (DAVAuthPolicy.hpp).
 */
inline vector<string> davDiscoveryHostCandidates(const string & explicitHost, const string & emailAddress,
                                                 const string & imapHost) {
    vector<string> out;
    auto add = [&out](string h) {
        size_t a = h.find_first_not_of(" \t\r\n");
        size_t b = h.find_last_not_of(" \t\r\n");
        if (a == string::npos) return;
        h = h.substr(a, b - a + 1);
        string key = h;
        for (auto & c : key) c = (char)tolower((unsigned char)c);
        for (const auto & existing : out) {
            string e = existing;
            for (auto & c : e) c = (char)tolower((unsigned char)c);
            if (e == key) return;
        }
        out.push_back(h);
    };
    add(explicitHost);
    size_t at = emailAddress.find('@');
    if (at != string::npos && at + 1 < emailAddress.size()) {
        string domain = emailAddress.substr(at + 1);
        for (auto & c : domain) c = (char)tolower((unsigned char)c);
        add(domain);
    }
    add(imapHost);
    return out;
}

inline DAVDiscoveryVerdict classifyDAVDiscovery(const vector<int> & candidateCodes, bool homeSetFound) {
    return classifyCalDAVDiscovery(candidateCodes, homeSetFound);
}

/**
 * Czy discovery jest rozstrzygnięte na czas życia procesu. Tylko awaria sieci
 * (isOffline) zostawia je otwarte do kolejnego cyklu — każdy inny wynik, także
 * niepewny, kończy je raz, żeby nie odpytywać serwera bez CardDAV co cykl.
 */
inline bool davDiscoveryCompletes(DAVDiscoveryVerdict verdict, bool offline) {
    (void)verdict;
    return !offline;
}

/**
 * Wybór książki adresowej z addressbook-home-set: zapisana wcześniej (jeśli
 * serwer dalej ją wystawia) — stabilnie między cyklami; inaczej pierwsza.
 */
inline string davPickAddressBook(const vector<string> & addressBookURLs, const string & existingURL) {
    if (addressBookURLs.empty()) return "";
    for (const auto & u : addressBookURLs) {
        if (!existingURL.empty() && u == existingURL) return u;
    }
    return addressBookURLs.front();
}

#endif /* DAVDiscoveryOutcome_hpp */

//
//  carddav-discovery-test.cpp — #286: discovery książki adresowej CardDAV
//
//  Zwarcie: resolveAddressBook() miał `string cardHost = ""` zaszyte na sztywno
//  (po usunięciu zapytania do usługi upstream WS2-F), więc dla każdego konta poza
//  Gmailem discovery kończyło się przed pierwszym zapytaniem, choć log pisał
//  „Performing full CardDAV address book discovery". Kontakty nie synchronizowały się.
//
//  Naprawa jak w CalDAV (#267/#341): kandydaci hostów z konta (jawny host DAV,
//  domena adresu e-mail wg RFC 6764, host IMAP — bez usług trzecich), na każdym
//  .well-known/carddav → current-user-principal → addressbook-home-set, a wynik
//  klasyfikowany: „serwer nie ma CardDAV" kończy discovery raz i się nie zapętla.
//
//  Ścieżka do katalogu MailSync: argv[1] (kontrola źródła resolveAddressBook()).
//

#include "DAVDiscoveryOutcome.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

static int failures = 0;
static void check(bool ok, const char * name, const string & detail = "") {
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) failures++;
}
static string join(const vector<string> & v) {
    string out;
    for (auto & s : v) out += (out.empty() ? "" : ",") + s;
    return "[" + out + "]";
}

int main(int argc, char ** argv) {
    // --- kandydaci hostów
    auto c1 = davDiscoveryHostCandidates("", "user@example.pl", "mail.example.pl");
    check(c1 == vector<string>({"example.pl", "mail.example.pl"}),
          "konto bez jawnego hosta: domena e-mail (RFC 6764), potem host IMAP", join(c1));
    auto c2 = davDiscoveryHostCandidates("dav.example.com", "jan@example.com", "imap.example.com");
    check(c2 == vector<string>({"dav.example.com", "example.com", "imap.example.com"}),
          "jawny host z ustawień konta wygrywa (pierwszy)", join(c2));
    auto c3 = davDiscoveryHostCandidates("", "jan@Example.COM", "example.com");
    check(c3 == vector<string>({"example.com"}), "duplikaty bez względu na wielkość liter → jeden kandydat", join(c3));
    auto c4 = davDiscoveryHostCandidates("", "bez-malpy", "");
    check(c4.empty(), "brak domeny i hosta IMAP → brak kandydatów (nic nie pytamy)", join(c4));
    auto c5 = davDiscoveryHostCandidates("  dav.example.com ", "jan@example.com", "");
    check(!c5.empty() && c5[0] == "dav.example.com", "jawny host bez spacji z ustawień", join(c5));
    bool noUpstream = true;
    for (auto & h : c2) {
        if (h.find("getmailspring") != string::npos || h.find("foundry") != string::npos) noUpstream = false;
    }
    check(noUpstream, "żadnych hostów upstream wśród kandydatów");

    // --- klasyfikacja wyniku (jak CalDAV, #341)
    check(classifyDAVDiscovery({405, 405}, false) == DAVDiscoveryVerdict::NoCalDAV,
          "konto jak w #341: wszyscy kandydaci 405 → serwer nie ma CardDAV");
    check(classifyDAVDiscovery({404, 406}, false) == DAVDiscoveryVerdict::NoCalDAV, "404/406 → serwer nie ma CardDAV");
    check(classifyDAVDiscovery({405}, true) == DAVDiscoveryVerdict::Found, "znaleziony addressbook-home-set wygrywa");
    check(classifyDAVDiscovery({401, 405}, false) == DAVDiscoveryVerdict::Inconclusive, "401 → serwer DAV, niepewne");
    check(classifyDAVDiscovery({0}, false) == DAVDiscoveryVerdict::Inconclusive, "błąd bez kodu HTTP → niepewne");

    // --- discovery kończy się raz (nie zapętla), poza awarią sieci
    check(davDiscoveryCompletes(DAVDiscoveryVerdict::NoCalDAV, false), "brak CardDAV → discovery zakończone na stałe");
    check(davDiscoveryCompletes(DAVDiscoveryVerdict::Found, false), "znaleziono → zakończone");
    check(davDiscoveryCompletes(DAVDiscoveryVerdict::Inconclusive, false),
          "niepewne → zakończone w tym procesie (bez pętli co cykl)");
    check(!davDiscoveryCompletes(DAVDiscoveryVerdict::Inconclusive, true),
          "awaria sieci → ponów w kolejnym cyklu (jak CalDAV)");

    // --- wybór książki adresowej z addressbook-home-set
    vector<string> books = {"https://dav.example.com/ab/jan/default/", "https://dav.example.com/ab/jan/work/"};
    check(davPickAddressBook(books, "") == books[0], "pierwsze discovery → pierwsza książka adresowa",
          davPickAddressBook(books, ""));
    check(davPickAddressBook(books, books[1]) == books[1], "zapisana wcześniej książka zostaje (stabilny wybór)",
          davPickAddressBook(books, books[1]));
    check(davPickAddressBook(books, "https://dav.example.com/ab/jan/old/") == books[0],
          "zapisanej już nie ma → pierwsza");
    check(davPickAddressBook({}, books[0]).empty(), "brak książek → pusto");

    // --- źródło: resolveAddressBook() nie ma już zaszytego pustego hosta
    if (argc >= 2) {
        ifstream in(string(argv[1]) + "/DAVWorker.cpp");
        stringstream ss;
        ss << in.rdbuf();
        const string src = ss.str();
        size_t a = src.find("shared_ptr<ContactBook> DAVWorker::resolveAddressBook() {");
        size_t b = a == string::npos ? string::npos : src.find("\n}\n", a);
        const string body = (a == string::npos || b == string::npos) ? "" : src.substr(a, b - a);
        check(!body.empty(), "źródło: znaleziono resolveAddressBook()");
        check(body.find("cardHost = \"\"") == string::npos,
              "źródło: resolveAddressBook() bez zaszytego `cardHost = \"\"` (zwarcie #286)");
        check(body.find("addressBookHostCandidates()") != string::npos,
              "źródło: resolveAddressBook() pyta kandydatów hostów konta");
    } else {
        check(false, "źródło: brak argumentu z katalogiem MailSync");
    }

    printf("%s (%d błędów)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}

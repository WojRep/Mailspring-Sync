//
//  uid-reconcile-test.cpp — #327
//
//  Czyste funkcje UIDReconcile.hpp: (1) zakresy VANISHED → specyfikacje zapytań
//  z WŁĄCZNĄ górną granicą; (2) kiedy uzgadniać cały folder z serwerem (serwery
//  CONDSTORE/QRESYNC nie robiły tego nigdy — utracone lokalnie wiadomości nie
//  wracały); (3) strażnik podejrzanie dużego VANISHED.
//

#include "UIDReconcile.hpp"

static int failures = 0;
static void check(bool ok, const char * name, const string & detail = "") {
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) failures++;
}

int main() {
    const uint64_t STAR = UINT64_MAX;
    auto specs = planVanishedQueries({{1, 588}, {589, 589}, {591, 622}, {8565, 8569}, {9000, STAR}}, 50);
    check(specs.size() == 3, "liczba zapytań (zakres, otwarty, jedna lista)", to_string(specs.size()));
    check(specs[0].kind == UIDRangeQuery::Bounded && specs[0].lo == 1 && specs[0].hi == 588, "1–588 → zakres włączny 1..588");
    check(specs[1].kind == UIDRangeQuery::Open && specs[1].lo == 9000, "9000–* → zakres otwarty");
    check(specs[2].kind == UIDRangeQuery::List, "małe zakresy → lista UID");
    vector<uint32_t> l = specs[2].uids;
    check(l.size() == 1 + 32 + 5 && l.front() == 589 && l.back() == 8569, "lista: 589, 591..622, 8565..8569", to_string(l.size()));
    auto bad = planVanishedQueries({{10, 5}, {STAR, STAR}}, 50);
    check(bad.empty(), "odwrócony i pusty zakres → brak zapytań", to_string(bad.size()));

    const time_t now = 2000000000;
    const time_t H = 3600;
    check(reconcileDue(now, 0, 0, 25, 2034, 5), "różnica liczby lokalnie/serwer → uzgadniaj");
    check(!reconcileDue(now, now - 2 * H, now - 10 * 60, 25, 2034, 5), "ale nie częściej niż co 30 min");
    check(!reconcileDue(now, now - 2 * H, now - 2 * H, 2034, 2034, 5), "liczby zgodne, świeże uzgodnienie → nie");
    check(reconcileDue(now, now - 25 * H, now - 25 * H, 2034, 2034, 5), "raz na dobę mimo zgodnych liczb");
    check(reconcileDue(now, now - 7 * H, now - 7 * H, 2034, 2034, 0), "przy starcie, jeśli >6 h");

    check(!vanishedLooksSuspicious(20, 2054, 2034), "20 zniknęło, serwer 2034 z 2054 → zwykłe");
    check(vanishedLooksSuspicious(1856, 2034, 2034), "odłączenie 1856 przy zgodnej liczbie → podejrzane");
    check(!vanishedLooksSuspicious(0, 25, 2034), "nic do odłączenia → nie podejrzane");

    printf("%s (%d błędów)\n", failures == 0 ? "OK" : "BŁĄD", failures);
    return failures == 0 ? 0 : 1;
}

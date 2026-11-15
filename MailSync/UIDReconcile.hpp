//
//  UIDReconcile.hpp — #327
//
//  Czyste funkcje (bez zależności od silnika) do bezpiecznej synchronizacji IMAP:
//  - planVanishedQueries: zakresy UID z VANISHED (QRESYNC) → specyfikacje zapytań
//    z WŁĄCZNĄ górną granicą (MailUtils::queriesForUIDRangesInIndexSet);
//  - reconcileDue: kiedy uzgodnić cały folder z serwerem — serwery CONDSTORE/
//    QRESYNC wcześniej nie były uzgadniane NIGDY, więc wiadomości utracone
//    lokalnie nie wracały (zgłoszenie właściciela 2026-11-15);
//  - vanishedLooksSuspicious: strażnik przed odłączeniem podejrzanie wielu
//    wiadomości naraz.
//  Tylko nagłówek: bez zmian w projekcie Xcode; test: Tests/uid-reconcile-test.cpp.
//

#ifndef UIDReconcile_hpp
#define UIDReconcile_hpp

#include <cstdint>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

using namespace std;

struct UIDRangeQuery {
    enum Kind { Open, Bounded, List };
    Kind kind;
    uint64_t lo = 0;
    uint64_t hi = 0;
    vector<uint32_t> uids;
};

/** Zakresy {lewy, prawy} (prawy = UINT64_MAX oznacza „*”) → zapytania. Małe zakresy
 *  (≤ listThreshold) łączone w listy UID po maks. 200 sztuk. */
inline vector<UIDRangeQuery> planVanishedQueries(const vector<pair<uint64_t, uint64_t>> & ranges,
                                                 uint64_t listThreshold = 50) {
    vector<UIDRangeQuery> out;
    vector<uint32_t> pile;
    for (const auto & r : ranges) {
        uint64_t left = r.first, right = r.second;
        if (left == UINT64_MAX || left > right) continue; // pusty albo niepoprawny
        if (left == 0) left = 1;
        if (right == UINT64_MAX) {
            UIDRangeQuery q; q.kind = UIDRangeQuery::Open; q.lo = left;
            out.push_back(q);
        } else if (right - left > listThreshold) {
            UIDRangeQuery q; q.kind = UIDRangeQuery::Bounded; q.lo = left; q.hi = right;
            out.push_back(q);
        } else {
            for (uint64_t x = left; x <= right; x++) pile.push_back((uint32_t)x);
        }
    }
    for (size_t i = 0; i < pile.size(); i += 200) {
        UIDRangeQuery q; q.kind = UIDRangeQuery::List;
        q.uids.assign(pile.begin() + i, pile.begin() + min(pile.size(), i + 200));
        out.push_back(q);
    }
    return out;
}

const time_t RECONCILE_MIN_RETRY = 30 * 60;       // nie częściej niż co 30 min
const time_t RECONCILE_INTERVAL = 24 * 60 * 60;   // co najmniej raz na dobę
const time_t RECONCILE_ON_LAUNCH = 6 * 60 * 60;   // przy starcie, gdy ostatnie > 6 h

/** Czy uzgodnić cały folder z serwerem (lista UID serwera vs lokalna). */
inline bool reconcileDue(time_t now, time_t lastReconcile, time_t lastAttempt, int64_t localCount,
                         int64_t remoteCount, int iterationsSinceLaunch) {
    if (now - lastAttempt < RECONCILE_MIN_RETRY) return false;
    if (localCount != remoteCount) return true;
    if (now - lastReconcile > RECONCILE_INTERVAL) return true;
    if (iterationsSinceLaunch == 0 && now - lastReconcile > RECONCILE_ON_LAUNCH) return true;
    return false;
}

/** Czy odłączenie `matched` wiadomości zostawiłoby lokalnie wyraźnie mniej niż ma
 *  serwer (tolerancja: większe z 50 i 5% liczby na serwerze). */
inline bool vanishedLooksSuspicious(int64_t matched, int64_t localCount, int64_t remoteCount) {
    if (matched <= 0) return false;
    int64_t tolerance = max<int64_t>(50, remoteCount / 20);
    return (localCount - matched) < (remoteCount - tolerance);
}

#endif /* UIDReconcile_hpp */
